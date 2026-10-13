#include <windows.h>
#include <winsock.h>
#include <string.h>
#include <jornada/extras.h>
#include "network.h"
#include "kernel.h"

#define IDLE_MILLISECONDS 1000
#define WAITING_MILLISECONDS 10
#define STOP_MILLISECONDS 3000
#define OPTION_SIZE sizeof(int)

static HANDLE service_thread;
static volatile int service_running;

static void complete(int slot, struct jolt_network_request *request) {
    if (InterlockedTestExchange((LPLONG)&request->state, NETWORK_PENDING, NETWORK_DONE) == NETWORK_PENDING) jolt_wake(slot);
}

static void fail(struct jolt_network_request *request) {
    request->result = -1;
    request->error = WSAGetLastError();
}

static void set_nonblocking(SOCKET handle) {
    u_long enabled = 1;
    ioctlsocket(handle, FIONBIO, &enabled);
}

static int readiness(SOCKET handle) {
    fd_set readers, writers, errors;
    FD_ZERO(&readers);
    FD_ZERO(&writers);
    FD_ZERO(&errors);
    FD_SET(handle, &readers);
    FD_SET(handle, &writers);
    FD_SET(handle, &errors);
    struct timeval zero = { 0, 0 };
    if (select(0, &readers, &writers, &errors, &zero) == SOCKET_ERROR) return NETWORK_FAILED;
    int ready = 0;
    if (FD_ISSET(handle, &readers)) ready |= NETWORK_READABLE;
    if (FD_ISSET(handle, &writers)) ready |= NETWORK_WRITABLE;
    if (FD_ISSET(handle, &errors)) ready |= NETWORK_FAILED;
    return ready;
}

static struct sockaddr_in *request_address(struct jolt_network_request *request) {
    return (struct sockaddr_in *)request->address;
}

static void perform_send(struct jolt_network_request *request, SOCKET handle) {
    int length = (int)(request->length < JOLT_NETWORK_BUFFER ? request->length : JOLT_NETWORK_BUFFER);
    int sent = request->has_address ?
        sendto(handle, (const char *)request->data, length, 0, (const struct sockaddr *)request_address(request), sizeof(struct sockaddr_in)) :
        send(handle, (const char *)request->data, length, 0);
    if (sent == SOCKET_ERROR) fail(request);
    else request->result = sent;
}

static void perform_receive(struct jolt_network_request *request, SOCKET handle) {
    int length = (int)(request->length < JOLT_NETWORK_BUFFER ? request->length : JOLT_NETWORK_BUFFER);
    int flags = (int)request->arguments[0];
    int size = sizeof(struct sockaddr_in);
    int want_address = request->arguments[1] != 0;
    int received = want_address ?
        recvfrom(handle, (char *)request->data, length, flags, (struct sockaddr *)request_address(request), &size) :
        recv(handle, (char *)request->data, length, flags);
    if (received == SOCKET_ERROR) {
        fail(request);
        return;
    }
    request->result = received;
    request->has_address = want_address && size == sizeof(struct sockaddr_in);
}

static void perform_accept(struct jolt_network_request *request, SOCKET handle) {
    int size = sizeof(struct sockaddr_in);
    SOCKET accepted = accept(handle, (struct sockaddr *)request_address(request), &size);
    if (accepted == INVALID_SOCKET) {
        fail(request);
        return;
    }
    set_nonblocking(accepted);
    request->result = (LONG)accepted;
}

static void perform_name(struct jolt_network_request *request, SOCKET handle, int peer) {
    int size = sizeof(struct sockaddr_in);
    struct sockaddr *address = (struct sockaddr *)request_address(request);
    int failed = peer ? getpeername(handle, address, &size) : getsockname(handle, address, &size);
    if (failed) fail(request);
    else request->result = 0;
}

static void perform_option(struct jolt_network_request *request, SOCKET handle, int setting) {
    int level = (int)request->arguments[0], name = (int)request->arguments[1];
    int value = (int)request->arguments[2], size = OPTION_SIZE;
    int failed = setting ? setsockopt(handle, level, name, (const char *)&value, size) :
        getsockopt(handle, level, name, (char *)&value, &size);
    if (failed && !setting && level == SOL_SOCKET && name == SO_ERROR) {
        request->result = (readiness(handle) & NETWORK_FAILED) ? WSAECONNREFUSED : 0;
        return;
    }
    if (failed) {
        fail(request);
        return;
    }
    request->result = setting ? 0 : value;
}

static void perform(struct jolt_network_request *request) {
    SOCKET handle = (SOCKET)request->handle;
    u_long pending = 0;
    request->error = 0;
    request->result = 0;
    switch (request->operation) {
    case NETWORK_SOCKET:
        handle = socket(AF_INET, (int)request->arguments[0], 0);
        if (handle == INVALID_SOCKET) {
            fail(request);
            break;
        }
        set_nonblocking(handle);
        request->result = (LONG)handle;
        break;
    case NETWORK_CLOSE:
        if (closesocket(handle)) fail(request);
        break;
    case NETWORK_BIND:
        if (bind(handle, (const struct sockaddr *)request_address(request), sizeof(struct sockaddr_in))) fail(request);
        break;
    case NETWORK_CONNECT:
        if (connect(handle, (const struct sockaddr *)request_address(request), sizeof(struct sockaddr_in))) fail(request);
        break;
    case NETWORK_LISTEN:
        if (listen(handle, (int)request->arguments[0])) fail(request);
        break;
    case NETWORK_ACCEPT:
        perform_accept(request, handle);
        break;
    case NETWORK_SEND:
        perform_send(request, handle);
        break;
    case NETWORK_RECEIVE:
        perform_receive(request, handle);
        break;
    case NETWORK_SHUTDOWN:
        if (shutdown(handle, (int)request->arguments[0])) fail(request);
        break;
    case NETWORK_SET_OPTION:
        perform_option(request, handle, 1);
        break;
    case NETWORK_GET_OPTION:
        perform_option(request, handle, 0);
        break;
    case NETWORK_LOCAL_NAME:
        perform_name(request, handle, 0);
        break;
    case NETWORK_PEER_NAME:
        perform_name(request, handle, 1);
        break;
    case NETWORK_PENDING_BYTES:
        if (ioctlsocket(handle, FIONREAD, &pending)) fail(request);
        else request->result = (LONG)pending;
        break;
    case NETWORK_READINESS:
        request->result = readiness(handle);
        break;
    default:
        request->result = -1;
        request->error = WSAEOPNOTSUPP;
        break;
    }
}

static int serve_requests(void) {
    int waiting = 0;
    for (int slot = 0; slot < JOLT_PROCESSES; slot++) {
        struct jolt_network_request *request = &jolt.kernel->network[slot];
        if (request->state != NETWORK_PENDING) continue;
        if (request->operation != NETWORK_WAIT) {
            perform(request);
            complete(slot, request);
            continue;
        }
        int ready = readiness((SOCKET)request->handle);
        if (ready & ((int)request->arguments[0] | NETWORK_FAILED)) {
            request->result = ready;
            request->error = 0;
            complete(slot, request);
        } else {
            waiting = 1;
        }
    }
    return waiting;
}

static DWORD WINAPI serve(LPVOID unused) {
    (void)unused;
    WSADATA data;
    int started = WSAStartup(MAKEWORD(1, 1), &data) == 0;
    int waiting = 0;
    while (service_running) {
        WaitForSingleObject(jolt.network, waiting ? WAITING_MILLISECONDS : IDLE_MILLISECONDS);
        if (started) waiting = serve_requests();
    }
    if (started) WSACleanup();
    return 0;
}

void network_start(void) {
    service_running = 1;
    service_thread = CreateThread(NULL, 0, serve, NULL, 0, NULL);
}

void network_stop(void) {
    if (!service_thread) return;
    service_running = 0;
    SetEvent(jolt.network);
    WaitForSingleObject(service_thread, STOP_MILLISECONDS);
    CloseHandle(service_thread);
    service_thread = NULL;
}
