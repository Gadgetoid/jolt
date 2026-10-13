#include <windows.h>
#include <mmsystem.h>
#include <string.h>
#include "sound.h"
#include "kernel.h"

#define SAMPLE_RATE 22050
#define FALLBACK_SAMPLE_RATE 11025
#define CHUNK_MILLISECONDS 40
#define CHUNK_LIMIT (SAMPLE_RATE * CHUNK_MILLISECONDS / 1000)
#define CHUNK_COUNT 2
#define IDLE_CLOSE_MILLISECONDS 2000
#define STOP_MILLISECONDS 1000
#define HIGH_LEVEL 0xa0
#define LOW_LEVEL 0x60
#define MINIMUM_FREQUENCY 20
#define NO_OWNER (-1)

struct tone_request {
    DWORD frequency;
    DWORD deadline;
    int timed;
    int slot;
    LONG pid;
};

struct chunk {
    WAVEHDR header;
    BYTE samples[CHUNK_LIMIT];
};

static HANDLE sound_thread;
static HANDLE request_event;
static HANDLE done_event;
static volatile int sound_running;
static CRITICAL_SECTION request_lock;
static struct tone_request pending;
static volatile LONG pending_changed;

static HWAVEOUT device;
static DWORD sample_rate;
static struct chunk chunks[CHUNK_COUNT];
static DWORD phase;
static DWORD idle_since;

static int open_rate(DWORD rate) {
    WAVEFORMATEX format;
    memset(&format, 0, sizeof format);
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = rate;
    format.wBitsPerSample = 8;
    format.nBlockAlign = 1;
    format.nAvgBytesPerSec = rate;
    if (waveOutOpen(&device, WAVE_MAPPER, &format, (DWORD)done_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        device = NULL;
        return 0;
    }
    sample_rate = rate;
    return 1;
}

static int open_device(void) {
    if (device) return 1;
    if (!open_rate(SAMPLE_RATE) && !open_rate(FALLBACK_SAMPLE_RATE)) return 0;
    for (int index = 0; index < CHUNK_COUNT; index++) {
        memset(&chunks[index].header, 0, sizeof chunks[index].header);
        chunks[index].header.lpData = (LPSTR)chunks[index].samples;
        chunks[index].header.dwFlags = WHDR_DONE;
    }
    phase = 0;
    return 1;
}

static void close_device(void) {
    if (!device) return;
    waveOutReset(device);
    for (int index = 0; index < CHUNK_COUNT; index++)
        if (chunks[index].header.dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(device, &chunks[index].header, sizeof chunks[index].header);
    waveOutClose(device);
    device = NULL;
}

static void silence(void) {
    if (!device) return;
    waveOutReset(device);
    phase = 0;
    idle_since = GetTickCount();
}

static int owner_alive(const struct tone_request *tone) {
    if (tone->slot == NO_OWNER) return 1;
    const struct jolt_process *process = &jolt.kernel->processes[tone->slot];
    return process->pid == tone->pid && (process->state == PROCESS_RUNNING || process->state == PROCESS_STARTING);
}

static int tone_active(const struct tone_request *tone) {
    if (!tone->frequency) return 0;
    if (tone->timed && (LONG)(GetTickCount() - tone->deadline) >= 0) return 0;
    return owner_alive(tone);
}

static DWORD wait_time(const struct tone_request *tone) {
    if (!tone->timed) return CHUNK_MILLISECONDS;
    LONG left = (LONG)(tone->deadline - GetTickCount());
    if (left <= 0) return 0;
    return left < CHUNK_MILLISECONDS ? (DWORD)left : CHUNK_MILLISECONDS;
}

static void fill(struct chunk *chunk, DWORD frequency) {
    DWORD count = sample_rate * CHUNK_MILLISECONDS / 1000;
    DWORD half_period = sample_rate / 2;
    if (frequency > half_period) frequency = half_period;
    for (DWORD index = 0; index < count; index++) {
        chunk->samples[index] = phase < half_period ? HIGH_LEVEL : LOW_LEVEL;
        phase += frequency;
        if (phase >= sample_rate) phase -= sample_rate;
    }
    chunk->header.dwBufferLength = count;
}

static void queue_chunks(DWORD frequency) {
    for (int index = 0; index < CHUNK_COUNT; index++) {
        WAVEHDR *header = &chunks[index].header;
        if (!(header->dwFlags & WHDR_DONE)) continue;
        if (header->dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(device, header, sizeof *header);
        fill(&chunks[index], frequency);
        header->dwFlags = 0;
        if (waveOutPrepareHeader(device, header, sizeof *header) != MMSYSERR_NOERROR) continue;
        waveOutWrite(device, header, sizeof *header);
    }
}

static void take_request(struct tone_request *tone) {
    if (!InterlockedExchange((LPLONG)&pending_changed, 0)) return;
    EnterCriticalSection(&request_lock);
    *tone = pending;
    LeaveCriticalSection(&request_lock);
    if (!tone->frequency) silence();
}

static DWORD WINAPI play(LPVOID unused) {
    (void)unused;
    struct tone_request tone;
    memset(&tone, 0, sizeof tone);
    HANDLE events[2] = { request_event, done_event };
    while (sound_running) {
        take_request(&tone);
        if (tone_active(&tone)) {
            if (open_device()) queue_chunks(tone.frequency);
            WaitForMultipleObjects(2, events, FALSE, wait_time(&tone));
            continue;
        }
        if (tone.frequency) {
            tone.frequency = 0;
            silence();
        }
        if (device && GetTickCount() - idle_since > IDLE_CLOSE_MILLISECONDS) close_device();
        WaitForSingleObject(request_event, device ? IDLE_CLOSE_MILLISECONDS : INFINITE);
    }
    close_device();
    return 0;
}

void sound_start(void) {
    InitializeCriticalSection(&request_lock);
    request_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    done_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    sound_running = 1;
    sound_thread = CreateThread(NULL, 0, play, NULL, 0, NULL);
}

void sound_stop(void) {
    if (!sound_thread) return;
    sound_running = 0;
    SetEvent(request_event);
    WaitForSingleObject(sound_thread, STOP_MILLISECONDS);
    CloseHandle(sound_thread);
    CloseHandle(request_event);
    CloseHandle(done_event);
    DeleteCriticalSection(&request_lock);
    sound_thread = NULL;
}

void sound_bell(void) {
    MessageBeep(MB_OK);
}

void sound_tone(DWORD frequency, DWORD milliseconds, int slot) {
    if (!sound_thread) return;
    if (frequency && frequency < MINIMUM_FREQUENCY) frequency = 0;
    if (slot < 0 || slot >= JOLT_PROCESSES) slot = NO_OWNER;
    EnterCriticalSection(&request_lock);
    pending.frequency = frequency;
    pending.timed = milliseconds != 0;
    pending.deadline = GetTickCount() + milliseconds;
    pending.slot = slot;
    pending.pid = slot == NO_OWNER ? 0 : jolt.kernel->processes[slot].pid;
    LeaveCriticalSection(&request_lock);
    InterlockedExchange((LPLONG)&pending_changed, 1);
    SetEvent(request_event);
}
