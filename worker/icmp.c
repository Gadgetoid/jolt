#include <windows.h>
#include <winsock.h>
#include <string.h>
#include "icmp.h"
#include "optional.h"
#include "worker.h"
#include "memory.h"

#define WSCONTROL_TCP 6
#define WSCONTROL_ICMP_ECHO 2
#define ECHO_TIMEOUT_MILLISECONDS 1000
#define PACKET_LIMIT 576
#define QUEUE_LIMIT 8
#define REPLY_BUFFER 2048
#define DEFAULT_TTL 64
#define IP_HEADER_SIZE 20
#define ICMP_HEADER_SIZE 8
#define ICMP_ECHO_REPLY_TYPE 0
#define ICMP_UNREACHABLE_TYPE 3
#define ICMP_ECHO_REQUEST_TYPE 8
#define ICMP_TIME_EXCEEDED_TYPE 11
#define IP_VERSION_AND_LENGTH 0x45
#define IP_PROTOCOL_ICMP 1
#define STATUS_SUCCESS 0
#define STATUS_NET_UNREACHABLE 11002
#define STATUS_HOST_UNREACHABLE 11003
#define STATUS_PROTOCOL_UNREACHABLE 11004
#define STATUS_PORT_UNREACHABLE 11005
#define STATUS_TTL_EXPIRED_TRANSIT 11013
#define STATUS_TTL_EXPIRED_REASSEMBLY 11014
#define STATUS_TIMED_OUT 11010

typedef DWORD (WINAPI *ws_control_function)(DWORD, DWORD, LPVOID, LPDWORD, LPVOID, LPDWORD);

struct echo_request {
    DWORD address, timeout;
    WORD data_offset, data_size;
    BYTE options_valid, ttl, tos, flags;
    WORD options_offset;
    BYTE options_size, padding;
};

struct echo_reply {
    DWORD address, status, round_trip;
    WORD data_size, reserved;
    DWORD data;
    BYTE ttl, tos, flags, options_size;
    DWORD options;
};

struct queued_packet {
    DWORD source;
    DWORD length;
    BYTE data[PACKET_LIMIT];
};

_Static_assert(sizeof(struct echo_request) == 20, "WsControl echo request size");
_Static_assert(sizeof(struct echo_reply) == 28, "WsControl echo reply size");

static struct queued_packet queue[QUEUE_LIMIT];
static DWORD queue_head, queue_count;
static BYTE time_to_live[JOLT_DESCRIPTIONS];
static struct sockaddr_in peers[JOLT_DESCRIPTIONS];
static int winsock_ready;

int icmp_owns(int index) {
    return (description_at(index)->socket_type & JOLT_SOCKET_ICMP) != 0;
}

static int raw_socket(int index) {
    return (description_at(index)->socket_type & LINUX_SOCK_TYPE_MASK) == LINUX_SOCK_RAW;
}

LONG icmp_socket(DWORD type, DWORD flags) {
    jolt_lock();
    int index = jolt_description_alloc(DESCRIPTION_SOCKET, LINUX_O_RDWR | (flags & LINUX_SOCK_NONBLOCK), "socket");
    LONG fd = index < 0 ? error(LINUX_ENFILE) : jolt_fd_install(self, 0, index, (flags & LINUX_SOCK_CLOEXEC) != 0);
    if (index >= 0) {
        description_at(index)->socket = 0;
        description_at(index)->socket_type = type | JOLT_SOCKET_ICMP;
        time_to_live[index] = DEFAULT_TTL;
        peers[index].sin_family = 0;
        if (fd < 0) jolt_description_release(index);
    }
    jolt_unlock();
    return fd;
}

static WORD checksum(const BYTE *data, DWORD length) {
    DWORD sum = 0;
    for (DWORD offset = 0; offset + 1 < length; offset += 2) sum += (DWORD)(data[offset] << 8 | data[offset + 1]);
    if (length & 1) sum += (DWORD)data[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (WORD)~sum;
}

static void put_checksum(BYTE *data, DWORD length, DWORD offset) {
    data[offset] = data[offset + 1] = 0;
    WORD value = checksum(data, length);
    data[offset] = (BYTE)(value >> 8);
    data[offset + 1] = (BYTE)value;
}

static void write_ip_header(BYTE *header, DWORD total, BYTE ttl, DWORD source, DWORD destination) {
    memset(header, 0, IP_HEADER_SIZE);
    header[0] = IP_VERSION_AND_LENGTH;
    header[2] = (BYTE)(total >> 8);
    header[3] = (BYTE)total;
    header[8] = ttl;
    header[9] = IP_PROTOCOL_ICMP;
    memcpy(header + 12, &source, sizeof source);
    memcpy(header + 16, &destination, sizeof destination);
    put_checksum(header, IP_HEADER_SIZE, 10);
}

static struct queued_packet *queue_slot(void) {
    if (queue_count == QUEUE_LIMIT) {
        queue_head = (queue_head + 1) % QUEUE_LIMIT;
        queue_count--;
    }
    return &queue[(queue_head + queue_count++) % QUEUE_LIMIT];
}

static void queue_echo_reply(const struct echo_reply *reply, const BYTE *request, const BYTE *payload, DWORD payload_size) {
    struct queued_packet *packet = queue_slot();
    DWORD icmp_length = ICMP_HEADER_SIZE + payload_size;
    if (IP_HEADER_SIZE + icmp_length > PACKET_LIMIT) icmp_length = PACKET_LIMIT - IP_HEADER_SIZE;
    BYTE *icmp = packet->data + IP_HEADER_SIZE;
    memcpy(icmp, request, ICMP_HEADER_SIZE);
    icmp[0] = ICMP_ECHO_REPLY_TYPE;
    memcpy(icmp + ICMP_HEADER_SIZE, payload, icmp_length - ICMP_HEADER_SIZE);
    put_checksum(icmp, icmp_length, 2);
    write_ip_header(packet->data, IP_HEADER_SIZE + icmp_length, reply->ttl, reply->address, 0);
    packet->source = reply->address;
    packet->length = IP_HEADER_SIZE + icmp_length;
}

static int error_type(DWORD status, BYTE *code) {
    switch (status) {
    case STATUS_NET_UNREACHABLE: *code = 0; return ICMP_UNREACHABLE_TYPE;
    case STATUS_HOST_UNREACHABLE: *code = 1; return ICMP_UNREACHABLE_TYPE;
    case STATUS_PROTOCOL_UNREACHABLE: *code = 2; return ICMP_UNREACHABLE_TYPE;
    case STATUS_PORT_UNREACHABLE: *code = 3; return ICMP_UNREACHABLE_TYPE;
    case STATUS_TTL_EXPIRED_TRANSIT: *code = 0; return ICMP_TIME_EXCEEDED_TYPE;
    case STATUS_TTL_EXPIRED_REASSEMBLY: *code = 1; return ICMP_TIME_EXCEEDED_TYPE;
    default: return -1;
    }
}

static void queue_error(const struct echo_reply *reply, int type, BYTE code, const BYTE *request, DWORD target, BYTE ttl) {
    struct queued_packet *packet = queue_slot();
    BYTE *icmp = packet->data + IP_HEADER_SIZE;
    DWORD icmp_length = ICMP_HEADER_SIZE + IP_HEADER_SIZE + ICMP_HEADER_SIZE;
    memset(icmp, 0, ICMP_HEADER_SIZE);
    icmp[0] = (BYTE)type;
    icmp[1] = code;
    write_ip_header(icmp + ICMP_HEADER_SIZE, IP_HEADER_SIZE + ICMP_HEADER_SIZE, ttl, 0, target);
    memcpy(icmp + ICMP_HEADER_SIZE + IP_HEADER_SIZE, request, ICMP_HEADER_SIZE);
    put_checksum(icmp, icmp_length, 2);
    write_ip_header(packet->data, IP_HEADER_SIZE + icmp_length, reply->ttl, reply->address, 0);
    packet->source = reply->address;
    packet->length = IP_HEADER_SIZE + icmp_length;
}

static int start_winsock(void) {
    WSADATA data;
    if (!winsock_ready) winsock_ready = WSAStartup(MAKEWORD(1, 1), &data) == 0 ? 1 : -1;
    return winsock_ready > 0;
}

LONG icmp_send(int index, const BYTE *packet, DWORD length, const struct sockaddr_in *target) {
    if (!target) return error(LINUX_EDESTADDRREQ);
    if (length < ICMP_HEADER_SIZE || packet[0] != ICMP_ECHO_REQUEST_TYPE) return error(LINUX_EINVAL);
    if (length > PACKET_LIMIT - IP_HEADER_SIZE) return error(LINUX_EMSGSIZE);
    if (!start_winsock()) return error(LINUX_ENETDOWN);
    static BYTE request_buffer[REPLY_BUFFER], reply_buffer[REPLY_BUFFER];
    DWORD payload_size = length - ICMP_HEADER_SIZE;
    struct echo_request *request = (struct echo_request *)request_buffer;
    memset(request, 0, sizeof *request);
    request->address = target->sin_addr.s_addr;
    request->timeout = ECHO_TIMEOUT_MILLISECONDS;
    request->options_valid = 1;
    request->ttl = time_to_live[index];
    request->options_offset = sizeof *request;
    request->data_offset = sizeof *request;
    request->data_size = (WORD)payload_size;
    memcpy(request_buffer + sizeof *request, packet + ICMP_HEADER_SIZE, payload_size);
    DWORD request_length = sizeof *request + payload_size, reply_length = sizeof reply_buffer;
    memset(reply_buffer, 0, sizeof reply_buffer);
    static ws_control_function ws_control;
    if (!ws_control) ws_control = (ws_control_function)jolt_optional_in(L"winsock.dll", L"WsControl");
    if (!ws_control) ws_control = (ws_control_function)jolt_optional(L"WsControl");
    if (!ws_control) return error(LINUX_ENETUNREACH);
    DWORD failure = ws_control(WSCONTROL_TCP, WSCONTROL_ICMP_ECHO, request_buffer, &request_length, reply_buffer,
        &reply_length);
    const struct echo_reply *reply = (const struct echo_reply *)reply_buffer;
    if (failure) return error(LINUX_ENETUNREACH);
    if (!reply->reserved) return (LONG)length;
    BYTE code = 0;
    int type = error_type(reply->status, &code);
    if (reply->status != STATUS_SUCCESS && reply->status != STATUS_TIMED_OUT && type < 0) {
        trace("icmp status %lu\r\n", reply->status);
        return error(LINUX_ENETUNREACH);
    }
    if (reply->status == STATUS_SUCCESS) {
        DWORD data_size = reply->data_size;
        if (reply->data + data_size > reply_length) data_size = 0;
        queue_echo_reply(reply, packet, reply_buffer + reply->data, data_size);
    } else if (type >= 0) {
        queue_error(reply, type, code, packet, target->sin_addr.s_addr, time_to_live[index]);
    }
    return (LONG)length;
}

LONG icmp_receive(int index, BYTE *buffer, DWORD length, int nonblocking, struct sockaddr_in *source) {
    while (!queue_count) {
        if (nonblocking) return error(LINUX_EAGAIN);
        if (signal_ready()) return error(LINUX_EINTR);
        block_until(ECHO_TIMEOUT_MILLISECONDS);
    }
    const struct queued_packet *packet = &queue[queue_head];
    DWORD skip = raw_socket(index) ? 0 : IP_HEADER_SIZE;
    DWORD count = packet->length - skip;
    if (count > length) count = length;
    memcpy(buffer, packet->data + skip, count);
    if (source) {
        memset(source, 0, sizeof *source);
        source->sin_family = AF_INET;
        source->sin_addr.s_addr = packet->source;
    }
    queue_head = (queue_head + 1) % QUEUE_LIMIT;
    queue_count--;
    return (LONG)count;
}

int icmp_readiness(int index, int events) {
    (void)index;
    return events & ((queue_count ? LINUX_POLLIN : 0) | LINUX_POLLOUT);
}

DWORD icmp_pending(int index) {
    if (!queue_count) return 0;
    return queue[queue_head].length - (raw_socket(index) ? 0 : IP_HEADER_SIZE);
}

LONG icmp_set_option(int index, DWORD level, DWORD option, LONG value) {
    if (level == LINUX_IPPROTO_IP && option == LINUX_IP_TTL) {
        if (value < 1 || value > 255) return error(LINUX_EINVAL);
        time_to_live[index] = (BYTE)value;
    }
    return 0;
}

LONG icmp_connect(int index, const struct sockaddr_in *address) {
    peers[index] = *address;
    return 0;
}

const struct sockaddr_in *icmp_peer(int index) {
    return peers[index].sin_family ? &peers[index] : NULL;
}
