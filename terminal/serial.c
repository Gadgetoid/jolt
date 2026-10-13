#include <windows.h>
#include <string.h>
#include <wchar.h>
#include "serial.h"
#include "kernel.h"

#define READ_WAIT_MILLISECONDS 20
#define IDLE_MILLISECONDS 1000
#define STOP_MILLISECONDS 3000
#define QUEUE_SIZE 4096
#define PORT_NAME_CAPACITY 8
#define ACTIVE_DRIVERS_KEY L"Drivers\\Active"
#define DRIVER_KEY_CAPACITY 32

struct baud_rate {
    DWORD code, rate;
};

static const struct baud_rate baud_rates[] = {
    { 0x1, 50 }, { 0x2, 75 }, { 0x3, 110 }, { 0x4, 134 }, { 0x5, 150 }, { 0x6, 200 }, { 0x7, 300 },
    { 0x8, 600 }, { 0x9, 1200 }, { 0xa, 1800 }, { 0xb, 2400 }, { 0xc, 4800 }, { 0xd, 9600 },
    { 0xe, 19200 }, { 0xf, 38400 }, { 0x1001, 57600 }, { 0x1002, 115200 }, { 0x1003, 230400 },
    { 0x1004, 460800 },
};

static HANDLE service_thread;
static volatile int service_running;
static HANDLE handles[JOLT_SERIAL_PORTS];

static struct jolt_serial *port_state(int port) {
    return &jolt.kernel->serial[port];
}

static DWORD baud_for(DWORD control_flags) {
    DWORD code = control_flags & LINUX_CBAUD;
    for (size_t index = 0; index < sizeof baud_rates / sizeof baud_rates[0]; index++)
        if (baud_rates[index].code == code) return baud_rates[index].rate;
    return CBR_9600;
}

static void apply_settings(HANDLE handle, const struct linux_termios *settings, DWORD lines) {
    DCB control;
    memset(&control, 0, sizeof control);
    control.DCBlength = sizeof control;
    if (!GetCommState(handle, &control)) return;
    DWORD flags = settings->control_flags;
    int hardware_flow = (flags & LINUX_CRTSCTS) != 0;
    control.BaudRate = baud_for(flags);
    control.ByteSize = (BYTE)(5 + ((flags & LINUX_CSIZE) >> 4));
    control.fParity = (flags & LINUX_PARENB) != 0;
    control.Parity = !(flags & LINUX_PARENB) ? NOPARITY : (flags & LINUX_PARODD) ? ODDPARITY : EVENPARITY;
    control.StopBits = flags & LINUX_CSTOPB ? TWOSTOPBITS : ONESTOPBIT;
    control.fBinary = TRUE;
    control.fOutxCtsFlow = hardware_flow;
    control.fOutxDsrFlow = FALSE;
    control.fDsrSensitivity = FALSE;
    control.fRtsControl = hardware_flow ? RTS_CONTROL_HANDSHAKE :
        (lines & LINUX_TIOCM_RTS) ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
    control.fDtrControl = (lines & LINUX_TIOCM_DTR) ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    control.fOutX = (settings->input_flags & LINUX_IXON) != 0;
    control.fInX = (settings->input_flags & LINUX_IXOFF) != 0;
    control.fNull = FALSE;
    control.fAbortOnError = FALSE;
    SetCommState(handle, &control);
}

static void reset_buffers(struct jolt_serial *serial) {
    InterlockedExchange((LPLONG)&serial->received_read, serial->received_written);
    InterlockedExchange((LPLONG)&serial->sent_read, serial->sent_written);
}

static void set_state(struct jolt_serial *serial, LONG state, DWORD error) {
    jolt_lock();
    serial->state = state;
    serial->error = error;
    jolt_wake_all();
    jolt_unlock();
}

static int driver_named(HKEY drivers, const WCHAR *key_name, const WCHAR *port_name) {
    HKEY driver;
    WCHAR name[PORT_NAME_CAPACITY];
    DWORD type = 0, size = sizeof name;
    if (RegOpenKeyExW(drivers, key_name, 0, 0, &driver) != ERROR_SUCCESS) return 0;
    int found = RegQueryValueExW(driver, L"Name", NULL, &type, (BYTE *)name, &size) == ERROR_SUCCESS &&
        type == REG_SZ && !_wcsicmp(name, port_name);
    RegCloseKey(driver);
    return found;
}

static int port_loaded(const WCHAR *port_name) {
    HKEY drivers;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ACTIVE_DRIVERS_KEY, 0, 0, &drivers) != ERROR_SUCCESS) return 0;
    int found = 0;
    WCHAR key_name[DRIVER_KEY_CAPACITY];
    for (DWORD index = 0; !found; index++) {
        DWORD length = DRIVER_KEY_CAPACITY;
        if (RegEnumKeyExW(drivers, index, key_name, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        found = driver_named(drivers, key_name, port_name);
    }
    RegCloseKey(drivers);
    return found;
}

static void open_port(int port) {
    struct jolt_serial *serial = port_state(port);
    WCHAR name[PORT_NAME_CAPACITY] = L"COM1:";
    name[3] = (WCHAR)(L'1' + port);
    HANDLE handle = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        set_state(serial, SERIAL_FAILED, port_loaded(name) ? ERROR_SHARING_VIOLATION : code);
        return;
    }
    SetupComm(handle, QUEUE_SIZE, QUEUE_SIZE);
    COMMTIMEOUTS timeouts = { MAXDWORD, MAXDWORD, READ_WAIT_MILLISECONDS, 0, 0 };
    SetCommTimeouts(handle, &timeouts);
    apply_settings(handle, &serial->termios, serial->modem_lines);
    InterlockedExchange(&serial->settings_changed, 0);
    InterlockedExchange(&serial->modem_changed, 0);
    reset_buffers(serial);
    handles[port] = handle;
    set_state(serial, SERIAL_OPEN, 0);
}

static void close_port(int port) {
    if (handles[port]) {
        PurgeComm(handles[port], PURGE_RXCLEAR | PURGE_TXCLEAR);
        CloseHandle(handles[port]);
        handles[port] = NULL;
    }
    reset_buffers(port_state(port));
    set_state(port_state(port), SERIAL_CLOSED, 0);
}

static void update_control(int port) {
    struct jolt_serial *serial = port_state(port);
    HANDLE handle = handles[port];
    if (InterlockedExchange(&serial->settings_changed, 0) | InterlockedExchange(&serial->modem_changed, 0))
        apply_settings(handle, &serial->termios, serial->modem_lines);
    LONG purge = InterlockedExchange(&serial->purge, 0);
    if (purge & PURGE_RXCLEAR) InterlockedExchange((LPLONG)&serial->received_read, serial->received_written);
    if (purge & PURGE_TXCLEAR) InterlockedExchange((LPLONG)&serial->sent_read, serial->sent_written);
    if (purge) PurgeComm(handle, (DWORD)purge);
    LONG breaking = serial->break_state;
    if (breaking) SetCommBreak(handle);
    else ClearCommBreak(handle);
    DWORD status = 0;
    if (GetCommModemStatus(handle, &status)) {
        DWORD lines = 0;
        if (status & MS_CTS_ON) lines |= LINUX_TIOCM_CTS;
        if (status & MS_DSR_ON) lines |= LINUX_TIOCM_DSR;
        if (status & MS_RING_ON) lines |= LINUX_TIOCM_RNG;
        if (status & MS_RLSD_ON) lines |= LINUX_TIOCM_CAR;
        serial->modem_status = lines;
    }
}

static int transmit(int port) {
    struct jolt_serial *serial = port_state(port);
    DWORD read = (DWORD)serial->sent_read, pending = (DWORD)serial->sent_written - read;
    if (!pending) return 0;
    DWORD offset = read % JOLT_SERIAL_BUFFER;
    DWORD count = pending < JOLT_SERIAL_BUFFER - offset ? pending : JOLT_SERIAL_BUFFER - offset;
    DWORD written = 0;
    if (!WriteFile(handles[port], serial->sent + offset, count, &written, NULL) || !written) return 0;
    InterlockedExchange((LPLONG)&serial->sent_read, (LONG)(read + written));
    return 1;
}

static int receive(int port) {
    struct jolt_serial *serial = port_state(port);
    DWORD written = (DWORD)serial->received_written, space = JOLT_SERIAL_BUFFER - (written - (DWORD)serial->received_read);
    if (!space) return 0;
    DWORD offset = written % JOLT_SERIAL_BUFFER;
    DWORD count = space < JOLT_SERIAL_BUFFER - offset ? space : JOLT_SERIAL_BUFFER - offset;
    DWORD received = 0;
    if (!ReadFile(handles[port], serial->received + offset, count, &received, NULL) || !received) return 0;
    InterlockedExchange((LPLONG)&serial->received_written, (LONG)(written + received));
    return 1;
}

static int service_port(int port) {
    struct jolt_serial *serial = port_state(port);
    int wanted = serial->users > 0;
    if (wanted && !handles[port] && serial->state != SERIAL_FAILED) open_port(port);
    if (!wanted && (handles[port] || serial->state != SERIAL_CLOSED)) close_port(port);
    if (!handles[port]) return 0;
    update_control(port);
    int moved = transmit(port);
    moved |= receive(port);
    if (moved) {
        jolt_lock();
        jolt_wake_all();
        jolt_unlock();
    }
    return 1;
}

static DWORD WINAPI serve(LPVOID unused) {
    (void)unused;
    while (service_running) {
        int active = 0;
        for (int port = 0; port < JOLT_SERIAL_PORTS; port++) active |= service_port(port);
        if (!active) WaitForSingleObject(jolt.serial, IDLE_MILLISECONDS);
    }
    for (int port = 0; port < JOLT_SERIAL_PORTS; port++) if (handles[port]) close_port(port);
    return 0;
}

void serial_start(void) {
    service_running = 1;
    service_thread = CreateThread(NULL, 0, serve, NULL, 0, NULL);
}

void serial_stop(void) {
    if (!service_thread) return;
    service_running = 0;
    SetEvent(jolt.serial);
    WaitForSingleObject(service_thread, STOP_MILLISECONDS);
    CloseHandle(service_thread);
    service_thread = NULL;
}
