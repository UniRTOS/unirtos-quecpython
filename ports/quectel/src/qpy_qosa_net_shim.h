#ifndef QPY_QOSA_NET_SHIM_H
#define QPY_QOSA_NET_SHIM_H

#include <stdint.h>

#ifndef __QOSA_SOCKETS_H__
#define __QOSA_SOCKETS_H__
struct in_addr {
    uint32_t s_addr;
};
struct in6_addr {
    uint8_t s6_addr[16];
};
#endif

#endif
