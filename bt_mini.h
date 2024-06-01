#ifndef __L2CAP_H
#define __L2CAP_H

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH	31
#endif

#define BTPROTO_L2CAP	0

/* BD Address */
typedef struct {
	uint8_t b[6];
} __attribute__((packed)) bdaddr_t;

/* BD Address type */
#define BDADDR_BREDR           0x00
#define BDADDR_LE_PUBLIC       0x01
#define BDADDR_LE_RANDOM       0x02

/* Byte order conversions */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htobs(d)  (d)
#define htobl(d)  (d)
#define htobll(d) (d)
#elif __BYTE_ORDER == __BIG_ENDIAN
#include <byteswap.h>
#define htobs(d)  bswap_16(d)
#define htobl(d)  bswap_32(d)
#define htobll(d) bswap_64(d)
#else
#error "Unknown byte order"
#endif

#define btohs htobs
#define btohl htobl
#define btohll htobll

/* L2CAP socket address */
struct sockaddr_l2 {
	sa_family_t	l2_family;
	unsigned short	l2_psm;
	bdaddr_t	l2_bdaddr;
	unsigned short	l2_cid;
	uint8_t		l2_bdaddr_type;
};

#endif
