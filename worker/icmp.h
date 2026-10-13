#pragma once
#include <windows.h>
#include <winsock.h>

#define JOLT_SOCKET_ICMP 0x10000

int icmp_owns(int index);
LONG icmp_socket(DWORD type, DWORD flags);
LONG icmp_send(int index, const BYTE *packet, DWORD length, const struct sockaddr_in *target);
LONG icmp_receive(int index, BYTE *buffer, DWORD length, int nonblocking, struct sockaddr_in *source);
int icmp_readiness(int index, int events);
DWORD icmp_pending(int index);
LONG icmp_set_option(int index, DWORD level, DWORD option, LONG value);
LONG icmp_connect(int index, const struct sockaddr_in *address);
const struct sockaddr_in *icmp_peer(int index);
