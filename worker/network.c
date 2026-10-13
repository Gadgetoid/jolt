#include <windows.h>
#include <winsock.h>
#include <string.h>
#include <jornada/extras.h>
#include "worker.h"
#include "memory.h"
#include "process.h"
#include "syscalls.h"
#include "network.h"
#include "icmp.h"

#define WAIT_SLICE_MILLISECONDS 100
#define DNS_PORT 53
#define DNS_ADDRESS 0x7f000035UL
#define DNS_PACKET_LIMIT 512
#define DNS_HEADER_SIZE 12
#define DNS_NAME_LIMIT 255
#define DNS_LABEL_LIMIT 63
#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1
#define DNS_FLAGS_ANSWER 0x8180
#define DNS_FLAGS_MISSING 0x8183
#define DNS_ANSWER_LIMIT 4
#define DNS_TTL 60
#define DNS_NAME_POINTER 0xc00c
#define MESSAGE_VECTOR_LIMIT 16

struct dns_reply {
    int pending;
    DWORD length;
    BYTE data[DNS_PACKET_LIMIT];
};

static int winsock_started;
static struct dns_reply dns_replies[JOLT_DESCRIPTIONS];

static LONG map_winsock_error(int code) {
    switch (code) {
    case WSAEWOULDBLOCK: return error(LINUX_EAGAIN);
    case WSAEINPROGRESS: return error(LINUX_EINPROGRESS);
    case WSAEALREADY: return error(LINUX_EALREADY);
    case WSAENOTSOCK: return error(LINUX_ENOTSOCK);
    case WSAEDESTADDRREQ: return error(LINUX_EDESTADDRREQ);
    case WSAEMSGSIZE: return error(LINUX_EMSGSIZE);
    case WSAEPROTOTYPE: return error(LINUX_EPROTOTYPE);
    case WSAENOPROTOOPT: return error(LINUX_ENOPROTOOPT);
    case WSAEPROTONOSUPPORT: return error(LINUX_EPROTONOSUPPORT);
    case WSAEOPNOTSUPP: return error(LINUX_EOPNOTSUPP);
    case WSAEAFNOSUPPORT: return error(LINUX_EAFNOSUPPORT);
    case WSAEADDRINUSE: return error(LINUX_EADDRINUSE);
    case WSAEADDRNOTAVAIL: return error(LINUX_EADDRNOTAVAIL);
    case WSAENETDOWN: return error(LINUX_ENETDOWN);
    case WSAENETUNREACH: return error(LINUX_ENETUNREACH);
    case WSAECONNABORTED: return error(LINUX_ECONNABORTED);
    case WSAECONNRESET: return error(LINUX_ECONNRESET);
    case WSAENOBUFS: return error(LINUX_ENOBUFS);
    case WSAEISCONN: return error(LINUX_EISCONN);
    case WSAENOTCONN: return error(LINUX_ENOTCONN);
    case WSAESHUTDOWN: return error(LINUX_EPIPE);
    case WSAETIMEDOUT: return error(LINUX_ETIMEDOUT);
    case WSAECONNREFUSED: return error(LINUX_ECONNREFUSED);
    case WSAEHOSTUNREACH: return error(LINUX_EHOSTUNREACH);
    case WSAEINVAL: return error(LINUX_EINVAL);
    case WSAEFAULT: return error(LINUX_EFAULT);
    case WSANOTINITIALISED: return error(LINUX_ENETDOWN);
    default: return error(LINUX_EIO);
    }
}

static struct jolt_network_request *own_request(void) {
    return &kernel()->network[slot];
}

static LONG call_broker(struct jolt_network_request *request, DWORD operation, DWORD handle) {
    request->operation = operation;
    request->handle = handle;
    InterlockedExchange((LPLONG)&request->state, NETWORK_PENDING);
    SetEvent(jolt.network);
    while (request->state != NETWORK_DONE) block_until(WAIT_SLICE_MILLISECONDS);
    InterlockedExchange((LPLONG)&request->state, NETWORK_IDLE);
    return request->error ? map_winsock_error((int)request->error) : request->result;
}

static LONG simple_call(DWORD operation, DWORD handle, DWORD first, DWORD second, DWORD third) {
    struct jolt_network_request *request = own_request();
    request->arguments[0] = first;
    request->arguments[1] = second;
    request->arguments[2] = third;
    request->has_address = 0;
    return call_broker(request, operation, handle);
}

static int cancel_wait(struct jolt_network_request *request) {
    LONG previous = InterlockedTestExchange((LPLONG)&request->state, NETWORK_PENDING, NETWORK_IDLE);
    if (previous == NETWORK_PENDING) return 1;
    InterlockedExchange((LPLONG)&request->state, NETWORK_IDLE);
    return 0;
}

static LONG wait_ready(DWORD handle, DWORD wanted) {
    struct jolt_network_request *request = own_request();
    request->arguments[0] = wanted;
    request->operation = NETWORK_WAIT;
    request->handle = handle;
    InterlockedExchange((LPLONG)&request->state, NETWORK_PENDING);
    SetEvent(jolt.network);
    for (;;) {
        if (request->state == NETWORK_DONE) {
            InterlockedExchange((LPLONG)&request->state, NETWORK_IDLE);
            return 0;
        }
        jolt_lock();
        int interrupted = signal_ready();
        self->waiting = interrupted ? WAIT_NONE : WAIT_POLL;
        jolt_unlock();
        if (interrupted && cancel_wait(request)) return error(LINUX_EINTR);
        if (interrupted) return 0;
        block_until(WAIT_SLICE_MILLISECONDS);
    }
}

static void finish_wait(void) {
    jolt_lock();
    self->waiting = WAIT_NONE;
    jolt_unlock();
}

static LONG socket_of(DWORD fd, int *index, DWORD *handle) {
    *index = fd_description(fd);
    if (*index < 0) return error(LINUX_EBADF);
    const struct jolt_description *description = description_at(*index);
    if (description->type != DESCRIPTION_SOCKET) return error(LINUX_ENOTSOCK);
    *handle = description->socket;
    return 0;
}

static int nonblocking(int index, DWORD flags) {
    return (description_at(index)->flags & LINUX_O_NONBLOCK) || (flags & LINUX_MSG_DONTWAIT);
}

static int copy_address_in(DWORD pointer, DWORD length, struct sockaddr_in *address) {
    struct linux_sockaddr_in guest;
    if (length < offsetof(struct linux_sockaddr_in, zero) || length > sizeof guest) return 0;
    memset(&guest, 0, sizeof guest);
    if (!guest_copy_in(&guest, pointer, length) || guest.family != LINUX_AF_INET) return 0;
    memset(address, 0, sizeof *address);
    address->sin_family = AF_INET;
    address->sin_port = guest.port;
    address->sin_addr.s_addr = guest.address;
    return 1;
}

static LONG copy_address_out(DWORD pointer, DWORD length_pointer, const struct sockaddr_in *address) {
    if (!pointer || !length_pointer) return 0;
    DWORD capacity;
    if (!guest_copy_in(&capacity, length_pointer, sizeof capacity)) return error(LINUX_EFAULT);
    struct linux_sockaddr_in guest;
    memset(&guest, 0, sizeof guest);
    guest.family = LINUX_AF_INET;
    guest.port = address->sin_port;
    guest.address = address->sin_addr.s_addr;
    DWORD size = sizeof guest, copied = capacity < size ? capacity : size;
    if (!guest_copy_out(pointer, &guest, copied) || !guest_copy_out(length_pointer, &size, sizeof size)) return error(LINUX_EFAULT);
    return 0;
}

static int is_dns_target(const struct sockaddr_in *address) {
    return address->sin_addr.s_addr == htonl(DNS_ADDRESS) && address->sin_port == htons(DNS_PORT);
}

static void put_word(BYTE *packet, DWORD *length, WORD value) {
    packet[(*length)++] = (BYTE)(value >> 8);
    packet[(*length)++] = (BYTE)value;
}

static DWORD question_end(const BYTE *query, DWORD length, char *name) {
    DWORD position = DNS_HEADER_SIZE, used = 0;
    while (position < length && query[position]) {
        DWORD label = query[position++];
        if (label > DNS_LABEL_LIMIT || position + label > length || used + label + 1 > DNS_NAME_LIMIT) return 0;
        if (used) name[used++] = '.';
        memcpy(name + used, query + position, label);
        used += label;
        position += label;
    }
    name[used] = 0;
    position++;
    return position + 4 <= length ? position + 4 : 0;
}

static const struct hostent *lookup_host(const char *name) {
    if (!winsock_started) {
        WSADATA data;
        winsock_started = WSAStartup(MAKEWORD(1, 1), &data) == 0;
    }
    return winsock_started ? gethostbyname(name) : NULL;
}

static void answer_dns(int index, const BYTE *query, DWORD length) {
    struct dns_reply *reply = &dns_replies[index];
    char name[DNS_NAME_LIMIT + 1];
    DWORD end = length >= DNS_HEADER_SIZE ? question_end(query, length, name) : 0;
    if (!end || end > DNS_PACKET_LIMIT) return;
    WORD type = (WORD)((query[end - 4] << 8) | query[end - 3]);
    const struct hostent *host = type == DNS_TYPE_A ? lookup_host(name) : NULL;
    int answers = 0;
    if (host) while (answers < DNS_ANSWER_LIMIT && host->h_addr_list[answers]) answers++;
    int missing = type == DNS_TYPE_A && !host;
    memcpy(reply->data, query, end);
    reply->length = 2;
    put_word(reply->data, &reply->length, missing ? DNS_FLAGS_MISSING : DNS_FLAGS_ANSWER);
    put_word(reply->data, &reply->length, 1);
    put_word(reply->data, &reply->length, (WORD)answers);
    put_word(reply->data, &reply->length, 0);
    put_word(reply->data, &reply->length, 0);
    reply->length = end;
    for (int answer = 0; answer < answers; answer++) {
        put_word(reply->data, &reply->length, DNS_NAME_POINTER);
        put_word(reply->data, &reply->length, DNS_TYPE_A);
        put_word(reply->data, &reply->length, DNS_CLASS_IN);
        put_word(reply->data, &reply->length, 0);
        put_word(reply->data, &reply->length, DNS_TTL);
        put_word(reply->data, &reply->length, 4);
        memcpy(reply->data + reply->length, host->h_addr_list[answer], 4);
        reply->length += 4;
    }
    reply->pending = 1;
}

static LONG take_dns_reply(int index, char *buffer, DWORD length, DWORD from, DWORD from_length) {
    struct dns_reply *reply = &dns_replies[index];
    DWORD count = reply->length < length ? reply->length : length;
    memcpy(buffer, reply->data, count);
    reply->pending = 0;
    struct sockaddr_in source;
    memset(&source, 0, sizeof source);
    source.sin_family = AF_INET;
    source.sin_port = htons(DNS_PORT);
    source.sin_addr.s_addr = htonl(DNS_ADDRESS);
    LONG result = copy_address_out(from, from_length, &source);
    return result ? result : (LONG)count;
}

int network_readiness(int index, int events) {
    if (icmp_owns(index)) return icmp_readiness(index, events);
    if (dns_replies[index].pending) return LINUX_POLLIN & events;
    LONG ready = simple_call(NETWORK_READINESS, description_at(index)->socket, 0, 0, 0);
    if (ready < 0) return LINUX_POLLERR;
    int result = 0;
    if ((events & LINUX_POLLIN) && (ready & NETWORK_READABLE)) result |= LINUX_POLLIN;
    if ((events & LINUX_POLLOUT) && (ready & NETWORK_WRITABLE)) result |= LINUX_POLLOUT;
    if (ready & NETWORK_FAILED) result |= LINUX_POLLERR;
    return result;
}

DWORD network_pending(int index) {
    if (icmp_owns(index)) return icmp_pending(index);
    if (dns_replies[index].pending) return dns_replies[index].length;
    LONG count = simple_call(NETWORK_PENDING_BYTES, description_at(index)->socket, 0, 0, 0);
    return count > 0 ? (DWORD)count : 0;
}

void network_release(int index) {
    const struct jolt_description *description = description_at(index);
    if (description->type != DESCRIPTION_SOCKET || description->references != 1 || icmp_owns(index)) return;
    simple_call(NETWORK_CLOSE, description->socket, 0, 0, 0);
    dns_replies[index].pending = 0;
}

static LONG install_socket(DWORD handle, DWORD type, DWORD flags) {
    jolt_lock();
    int index = jolt_description_alloc(DESCRIPTION_SOCKET, LINUX_O_RDWR | (flags & LINUX_SOCK_NONBLOCK), "socket");
    LONG fd = index < 0 ? error(LINUX_ENFILE) : jolt_fd_install(self, 0, index, (flags & LINUX_SOCK_CLOEXEC) != 0);
    if (index >= 0) {
        description_at(index)->socket = handle;
        description_at(index)->socket_type = type;
        dns_replies[index].pending = 0;
        if (fd < 0) jolt_description_release(index);
    }
    jolt_unlock();
    if (fd < 0) simple_call(NETWORK_CLOSE, handle, 0, 0, 0);
    return fd;
}

LONG sys_socket(const DWORD *arguments) {
    DWORD domain = arguments[0], type = arguments[1] & LINUX_SOCK_TYPE_MASK, flags = arguments[1] & ~LINUX_SOCK_TYPE_MASK;
    if (domain != LINUX_AF_INET) return error(LINUX_EAFNOSUPPORT);
    int icmp = arguments[2] == LINUX_IPPROTO_ICMP && (type == LINUX_SOCK_RAW || type == LINUX_SOCK_DGRAM);
    if (icmp) return icmp_socket(type, flags);
    if (type != LINUX_SOCK_STREAM && type != LINUX_SOCK_DGRAM) return error(LINUX_EPROTONOSUPPORT);
    LONG result = simple_call(NETWORK_SOCKET, 0, type == LINUX_SOCK_STREAM ? SOCK_STREAM : SOCK_DGRAM, 0, 0);
    if (own_request()->error) return result;
    return install_socket((DWORD)own_request()->result, type, flags);
}

static LONG address_call(DWORD operation, DWORD handle, const struct sockaddr_in *address) {
    struct jolt_network_request *request = own_request();
    memcpy(request->address, address, sizeof *address);
    request->has_address = 1;
    return call_broker(request, operation, handle);
}

LONG sys_bind(const DWORD *arguments) {
    int index;
    DWORD handle;
    struct sockaddr_in address;
    LONG result = socket_of(arguments[0], &index, &handle);
    if (result) return result;
    if (icmp_owns(index)) return 0;
    if (!copy_address_in(arguments[1], arguments[2], &address)) return error(LINUX_EINVAL);
    result = address_call(NETWORK_BIND, handle, &address);
    return result < 0 ? result : 0;
}

static LONG connection_error(DWORD handle) {
    LONG code = simple_call(NETWORK_GET_OPTION, handle, SOL_SOCKET, SO_ERROR, 0);
    if (code < 0) return code;
    return code ? map_winsock_error((int)code) : 0;
}

LONG sys_connect(const DWORD *arguments) {
    int index;
    DWORD handle;
    struct sockaddr_in address;
    LONG result = socket_of(arguments[0], &index, &handle);
    if (result) return result;
    if (!copy_address_in(arguments[1], arguments[2], &address)) return error(LINUX_EINVAL);
    if (icmp_owns(index)) return icmp_connect(index, &address);
    if (description_at(index)->socket_type == LINUX_SOCK_DGRAM && is_dns_target(&address)) return 0;
    result = address_call(NETWORK_CONNECT, handle, &address);
    if (result >= 0) return 0;
    if (result != error(LINUX_EAGAIN) && result != error(LINUX_EINPROGRESS)) return result;
    if (nonblocking(index, 0)) return error(LINUX_EINPROGRESS);
    result = wait_ready(handle, NETWORK_WRITABLE);
    finish_wait();
    return result ? result : connection_error(handle);
}

LONG sys_listen(const DWORD *arguments) {
    int index;
    DWORD handle;
    LONG result = socket_of(arguments[0], &index, &handle);
    if (result) return result;
    result = simple_call(NETWORK_LISTEN, handle, arguments[1], 0, 0);
    return result < 0 ? result : 0;
}

static LONG accept_connection(const DWORD *arguments, DWORD flags) {
    int index;
    DWORD handle;
    LONG result = socket_of(arguments[0], &index, &handle);
    if (result) return result;
    for (;;) {
        struct jolt_network_request *request = own_request();
        request->has_address = 0;
        LONG accepted = call_broker(request, NETWORK_ACCEPT, handle);
        if (!request->error) {
            struct sockaddr_in peer;
            memcpy(&peer, request->address, sizeof peer);
            finish_wait();
            LONG fd = install_socket((DWORD)request->result, LINUX_SOCK_STREAM, flags);
            if (fd >= 0) copy_address_out(arguments[1], arguments[2], &peer);
            return fd;
        }
        if (accepted != error(LINUX_EAGAIN)) return accepted;
        if (nonblocking(index, 0)) return error(LINUX_EAGAIN);
        result = wait_ready(handle, NETWORK_READABLE);
        if (result) {
            finish_wait();
            return result;
        }
    }
}

LONG sys_accept(const DWORD *arguments) {
    return accept_connection(arguments, 0);
}

LONG sys_accept4(const DWORD *arguments) {
    return accept_connection(arguments, arguments[3]);
}

static LONG name_of(const DWORD *arguments, DWORD operation) {
    int index;
    DWORD handle;
    LONG result = socket_of(arguments[0], &index, &handle);
    if (result) return result;
    if (icmp_owns(index)) {
        struct sockaddr_in local;
        memset(&local, 0, sizeof local);
        local.sin_family = AF_INET;
        return copy_address_out(arguments[1], arguments[2], &local);
    }
    struct jolt_network_request *request = own_request();
    request->has_address = 0;
    result = call_broker(request, operation, handle);
    if (result < 0) return result;
    struct sockaddr_in address;
    memcpy(&address, request->address, sizeof address);
    return copy_address_out(arguments[1], arguments[2], &address);
}

LONG sys_getsockname(const DWORD *arguments) {
    return name_of(arguments, NETWORK_LOCAL_NAME);
}

LONG sys_getpeername(const DWORD *arguments) {
    return name_of(arguments, NETWORK_PEER_NAME);
}

static void raise_broken_pipe(DWORD flags) {
    if (flags & LINUX_MSG_NOSIGNAL) return;
    jolt_lock();
    jolt_signal_process(slot, LINUX_SIGPIPE);
    jolt_unlock();
}

static LONG send_chunk(DWORD handle, const char *buffer, DWORD length, const struct sockaddr_in *target) {
    struct jolt_network_request *request = own_request();
    if (length > JOLT_NETWORK_BUFFER) length = JOLT_NETWORK_BUFFER;
    memcpy(request->data, buffer, length);
    request->length = length;
    request->has_address = target != NULL;
    if (target) memcpy(request->address, target, sizeof *target);
    return call_broker(request, NETWORK_SEND, handle);
}

static LONG send_bytes(int index, DWORD handle, const char *buffer, DWORD length, DWORD flags,
    const struct sockaddr_in *target) {
    if (target && is_dns_target(target)) {
        answer_dns(index, (const BYTE *)buffer, length);
        return (LONG)length;
    }
    DWORD done = 0;
    for (;;) {
        LONG sent = send_chunk(handle, buffer + done, length - done, target);
        if (sent >= 0) {
            done += (DWORD)sent;
            if (done == length || target) {
                finish_wait();
                return (LONG)done;
            }
            continue;
        }
        if (sent != error(LINUX_EAGAIN)) {
            finish_wait();
            if (sent == error(LINUX_EPIPE)) raise_broken_pipe(flags);
            return done ? (LONG)done : sent;
        }
        if (nonblocking(index, flags)) return done ? (LONG)done : error(LINUX_EAGAIN);
        LONG waited = wait_ready(handle, NETWORK_WRITABLE);
        if (waited) {
            finish_wait();
            return done ? (LONG)done : waited;
        }
    }
}

static LONG receive_bytes(int index, DWORD handle, char *buffer, DWORD length, DWORD flags, struct sockaddr_in *source,
    int *source_known) {
    struct jolt_network_request *request = own_request();
    *source_known = 0;
    if (length > JOLT_NETWORK_BUFFER) length = JOLT_NETWORK_BUFFER;
    for (;;) {
        request->length = length;
        request->has_address = 0;
        request->arguments[0] = (flags & LINUX_MSG_PEEK) ? MSG_PEEK : 0;
        request->arguments[1] = source != NULL;
        LONG received = call_broker(request, NETWORK_RECEIVE, handle);
        if (received >= 0) {
            memcpy(buffer, request->data, (DWORD)received);
            if (source && request->has_address) {
                memcpy(source, request->address, sizeof *source);
                *source_known = 1;
            }
            finish_wait();
            return received;
        }
        if (received != error(LINUX_EAGAIN)) {
            finish_wait();
            return received;
        }
        if (nonblocking(index, flags)) return error(LINUX_EAGAIN);
        LONG waited = wait_ready(handle, NETWORK_READABLE);
        if (waited) {
            finish_wait();
            return waited;
        }
    }
}

LONG network_write(int index, const char *buffer, DWORD length) {
    return send_bytes(index, description_at(index)->socket, buffer, length, 0, NULL);
}

LONG network_read(int index, char *buffer, DWORD length) {
    if (dns_replies[index].pending) return take_dns_reply(index, buffer, length, 0, 0);
    int known;
    return receive_bytes(index, description_at(index)->socket, buffer, length, 0, NULL, &known);
}

LONG sys_sendto(const DWORD *arguments) {
    DWORD buffer = arguments[1], length = arguments[2], flags = arguments[3], target = arguments[4], target_length = arguments[5];
    int index;
    DWORD handle;
    LONG result = socket_of(arguments[0], &index, &handle);
    if (result) return result;
    if (!guest_readable(buffer, length)) return error(LINUX_EFAULT);
    struct sockaddr_in address;
    if (target && !copy_address_in(target, target_length, &address)) return error(LINUX_EINVAL);
    if (icmp_owns(index)) return icmp_send(index, (const BYTE *)buffer, length, target ? &address : icmp_peer(index));
    return send_bytes(index, handle, (const char *)buffer, length, flags, target ? &address : NULL);
}

LONG sys_send(const DWORD *arguments) {
    DWORD extended[6] = { arguments[0], arguments[1], arguments[2], arguments[3], 0, 0 };
    return sys_sendto(extended);
}

LONG sys_recvfrom(const DWORD *arguments) {
    DWORD buffer = arguments[1], length = arguments[2], flags = arguments[3], from = arguments[4], from_length = arguments[5];
    int index;
    DWORD handle;
    LONG result = socket_of(arguments[0], &index, &handle);
    if (result) return result;
    if (length > JOLT_NETWORK_BUFFER) length = JOLT_NETWORK_BUFFER;
    if (!guest_writable(buffer, length)) return error(LINUX_EFAULT);
    if (dns_replies[index].pending) return take_dns_reply(index, (char *)buffer, length, from, from_length);
    struct sockaddr_in source;
    int known = 1;
    LONG received = icmp_owns(index) ?
        icmp_receive(index, (BYTE *)buffer, length, nonblocking(index, flags), &source) :
        receive_bytes(index, handle, (char *)buffer, length, flags, from ? &source : NULL, &known);
    if (received >= 0 && known) {
        LONG copied = copy_address_out(from, from_length, &source);
        if (copied) return copied;
    }
    return received;
}

LONG sys_recv(const DWORD *arguments) {
    DWORD extended[6] = { arguments[0], arguments[1], arguments[2], arguments[3], 0, 0 };
    return sys_recvfrom(extended);
}

static LONG load_message(DWORD pointer, struct linux_msghdr *message, struct linux_iovec *vectors) {
    if (!guest_copy_in(message, pointer, sizeof *message)) return error(LINUX_EFAULT);
    if (message->vector_count > MESSAGE_VECTOR_LIMIT) return error(LINUX_EMSGSIZE);
    if (!guest_copy_in(vectors, message->vectors, message->vector_count * sizeof *vectors)) return error(LINUX_EFAULT);
    return 0;
}

LONG sys_sendmsg(const DWORD *arguments) {
    struct linux_msghdr message;
    struct linux_iovec vectors[MESSAGE_VECTOR_LIMIT];
    LONG result = load_message(arguments[1], &message, vectors);
    if (result) return result;
    char gathered[JOLT_NETWORK_BUFFER];
    DWORD used = 0;
    for (DWORD index = 0; index < message.vector_count && used < sizeof gathered; index++) {
        DWORD length = vectors[index].length;
        if (length > sizeof gathered - used) length = sizeof gathered - used;
        if (!guest_copy_in(gathered + used, vectors[index].base, length)) return error(LINUX_EFAULT);
        used += length;
    }
    int fd_index;
    DWORD handle;
    result = socket_of(arguments[0], &fd_index, &handle);
    if (result) return result;
    struct sockaddr_in address;
    if (message.name && !copy_address_in(message.name, message.name_length, &address)) return error(LINUX_EINVAL);
    if (icmp_owns(fd_index)) return icmp_send(fd_index, (const BYTE *)gathered, used, message.name ? &address : icmp_peer(fd_index));
    return send_bytes(fd_index, handle, gathered, used, arguments[2], message.name ? &address : NULL);
}

LONG sys_recvmsg(const DWORD *arguments) {
    struct linux_msghdr message;
    struct linux_iovec vectors[MESSAGE_VECTOR_LIMIT];
    LONG result = load_message(arguments[1], &message, vectors);
    if (result) return result;
    DWORD capacity = 0;
    for (DWORD index = 0; index < message.vector_count; index++) capacity += vectors[index].length;
    if (capacity > JOLT_NETWORK_BUFFER) capacity = JOLT_NETWORK_BUFFER;
    int fd_index;
    DWORD handle;
    result = socket_of(arguments[0], &fd_index, &handle);
    if (result) return result;
    char scattered[JOLT_NETWORK_BUFFER];
    DWORD name_length_pointer = arguments[1] + offsetof(struct linux_msghdr, name_length);
    LONG received;
    if (dns_replies[fd_index].pending) {
        received = take_dns_reply(fd_index, scattered, capacity, message.name, message.name ? name_length_pointer : 0);
    } else if (icmp_owns(fd_index)) {
        struct sockaddr_in source;
        received = icmp_receive(fd_index, (BYTE *)scattered, capacity, nonblocking(fd_index, arguments[2]), &source);
        if (received >= 0 && message.name) copy_address_out(message.name, name_length_pointer, &source);
    } else {
        struct sockaddr_in source;
        int known;
        received = receive_bytes(fd_index, handle, scattered, capacity, arguments[2], message.name ? &source : NULL, &known);
        if (received >= 0 && known) copy_address_out(message.name, name_length_pointer, &source);
    }
    if (received < 0) return received;
    DWORD offset = 0;
    for (DWORD index = 0; index < message.vector_count && offset < (DWORD)received; index++) {
        DWORD length = vectors[index].length;
        if (length > (DWORD)received - offset) length = (DWORD)received - offset;
        if (!guest_copy_out(vectors[index].base, scattered + offset, length)) return error(LINUX_EFAULT);
        offset += length;
    }
    return received;
}

LONG sys_shutdown(const DWORD *arguments) {
    int index = fd_description(arguments[0]);
    if (index >= 0 && description_at(index)->type == DESCRIPTION_STREAM) return stream_shutdown(index, arguments[1]);
    DWORD handle;
    LONG result = socket_of(arguments[0], &index, &handle);
    if (result) return result;
    result = simple_call(NETWORK_SHUTDOWN, handle, arguments[1], 0, 0);
    return result < 0 ? result : 0;
}

static int winsock_option(DWORD level, DWORD option, DWORD *winsock_level, DWORD *winsock_name) {
    if (level == LINUX_IPPROTO_TCP && option == LINUX_TCP_NODELAY) {
        *winsock_level = IPPROTO_TCP;
        *winsock_name = TCP_NODELAY;
        return 1;
    }
    if (level != LINUX_SOL_SOCKET) return 0;
    *winsock_level = SOL_SOCKET;
    switch (option) {
    case LINUX_SO_REUSEADDR: *winsock_name = SO_REUSEADDR; return 1;
    case LINUX_SO_KEEPALIVE: *winsock_name = SO_KEEPALIVE; return 1;
    case LINUX_SO_BROADCAST: *winsock_name = SO_BROADCAST; return 1;
    case LINUX_SO_SNDBUF: *winsock_name = SO_SNDBUF; return 1;
    case LINUX_SO_RCVBUF: *winsock_name = SO_RCVBUF; return 1;
    default: return 0;
    }
}

LONG sys_setsockopt(const DWORD *arguments) {
    DWORD level = arguments[1], option = arguments[2], value_pointer = arguments[3], length = arguments[4];
    int index;
    DWORD handle, winsock_level, winsock_name;
    LONG result = socket_of(arguments[0], &index, &handle);
    if (result) return result;
    LONG value = 0;
    if (icmp_owns(index)) {
        if (length < sizeof value || !guest_copy_in(&value, value_pointer, sizeof value)) return 0;
        return icmp_set_option(index, level, option, value);
    }
    if (!winsock_option(level, option, &winsock_level, &winsock_name)) return 0;
    if (length < sizeof value || !guest_copy_in(&value, value_pointer, sizeof value)) return error(LINUX_EINVAL);
    result = simple_call(NETWORK_SET_OPTION, handle, winsock_level, winsock_name, (DWORD)value);
    return result < 0 ? result : 0;
}

LONG sys_getsockopt(const DWORD *arguments) {
    DWORD level = arguments[1], option = arguments[2], value_pointer = arguments[3], length_pointer = arguments[4];
    int index;
    DWORD handle, winsock_level, winsock_name;
    LONG result = socket_of(arguments[0], &index, &handle);
    if (result) return result;
    LONG value = 0;
    if (icmp_owns(index) && !(level == LINUX_SOL_SOCKET && option == LINUX_SO_TYPE)) {
        value = 0;
    } else if (level == LINUX_SOL_SOCKET && option == LINUX_SO_ERROR) {
        value = -connection_error(handle);
    } else if (level == LINUX_SOL_SOCKET && option == LINUX_SO_TYPE) {
        value = (LONG)(description_at(index)->socket_type & LINUX_SOCK_TYPE_MASK);
    } else if (winsock_option(level, option, &winsock_level, &winsock_name)) {
        value = simple_call(NETWORK_GET_OPTION, handle, winsock_level, winsock_name, 0);
        if (value < 0) return value;
    }
    DWORD size = sizeof value;
    if (!guest_copy_out(value_pointer, &value, sizeof value) || !guest_copy_out(length_pointer, &size, sizeof size))
        return error(LINUX_EFAULT);
    return 0;
}
