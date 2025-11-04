/*
 * Copyright (c) 2024, Ilya Kurdyukov
 *
 * Command line tool for various Bluetooth gadgets.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE
 * FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY
 * DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

#include <sys/socket.h>
#if 1
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/rfcomm.h>
#else
#include "bt_mini.h"
#endif

#define ATT_CID 4

static void print_esc_str(FILE *f, const uint8_t *buf, size_t len) {
	size_t i; int a;
	for (i = 0; i < len; i++) {
		a = buf[i];
		if (a > 0x20 && a < 0x7f) {
			if (a == '"' || a == '\\') fputc('\\', f);
			fputc(a, f);
		} else fprintf(f, "\\x%02x", a);
	}
}

static void print_mem(FILE *f, const uint8_t *buf, size_t len) {
	size_t i; int a, j, n;
	for (i = 0; i < len; i += 16) {
		n = len - i;
		if (n > 16) n = 16;
		for (j = 0; j < n; j++) fprintf(f, "%02x ", buf[i + j]);
		for (; j < 16; j++) fprintf(f, "   ");
		fprintf(f, " |");
		for (j = 0; j < n; j++) {
			a = buf[i + j];
			fprintf(f, "%c", a > 0x20 && a < 0x7f ? a : '.');
		}
		fprintf(f, "|\n");
	}
}

static uint8_t* loadfile(const char *fn, size_t *num, size_t lim) {
	size_t n, j = 0; uint8_t *buf = 0;
	FILE *fi = fopen(fn, "rb");
	if (fi) {
		fseek(fi, 0, SEEK_END);
		n = ftell(fi);
		fseek(fi, 0, SEEK_SET);
		if (n && n <= lim) {
			buf = (uint8_t*)malloc(n);
			if (buf) j = fread(buf, 1, n, fi);
		}
		fclose(fi);
	}
	if (num) *num = j;
	return buf;
}

#define L2CAP_ADDR \
	struct sockaddr_l2 addr = { 0 }; \
	addr.l2_family = AF_BLUETOOTH; \
	memcpy(&addr.l2_bdaddr, bdaddr, sizeof(bdaddr_t)); \
	if (cid) addr.l2_cid = htobs(cid); \
	else addr.l2_psm = htobs(psm); \
	addr.l2_bdaddr_type = type;

static int l2cap_bind(int sock, const bdaddr_t *bdaddr,
		int type, int psm, int cid) {
	L2CAP_ADDR
	if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
		return -errno;
	return 0;
}

static int l2cap_connect(int sock, const bdaddr_t *bdaddr,
		int type, int psm, int cid) {
	int err;
	L2CAP_ADDR
	err = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
	if (err < 0 && errno != EAGAIN && errno != EINPROGRESS)
		return -errno;
	return 0;
}
#undef L2CAP_ADDR

static int rfcomm_connect(int sock, const bdaddr_t *bdaddr,
		int channel) {
	int err;
	struct sockaddr_rc addr = { 0 };
	addr.rc_family = AF_BLUETOOTH;
	memcpy(&addr.rc_bdaddr, bdaddr, sizeof(bdaddr_t));
	addr.rc_channel = channel;
	err = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
	if (err < 0 && errno != EAGAIN && errno != EINPROGRESS)
		return -errno;
	return 0;
}

#define PERROR_EXIT(name) do { perror(#name " failed"); exit(1); } while (0)

#define ERR_EXIT(...) \
	do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)

#define DBG_LOG(...) fprintf(stderr, __VA_ARGS__)

#define WRITE16_LE(p, a) do { \
	uint32_t __tmp = a; \
	((uint8_t*)(p))[0] = (uint8_t)(a); \
	((uint8_t*)(p))[1] = (uint8_t)(__tmp >> 8); \
} while (0)

#define WRITE16_BE(p, a) do { \
	uint32_t __tmp = a; \
	((uint8_t*)(p))[0] = (uint8_t)(__tmp >> 8); \
	((uint8_t*)(p))[1] = (uint8_t)(a); \
} while (0)

#define WRITE32_BE(p, a) do { \
	uint32_t __tmp = a; \
	((uint8_t*)(p))[0] = (uint8_t)(__tmp >> 24); \
	((uint8_t*)(p))[1] = (uint8_t)(__tmp >> 16); \
	((uint8_t*)(p))[2] = (uint8_t)(__tmp >> 8); \
	((uint8_t*)(p))[3] = (uint8_t)(a); \
} while (0)

#define READ16_LE(p) ( \
	((uint8_t*)(p))[1] << 8 | \
	((uint8_t*)(p))[0])

#define READ16_BE(p) ( \
	((uint8_t*)(p))[0] << 8 | \
	((uint8_t*)(p))[1])

#define READ24_BE(p) ( \
	((uint8_t*)(p))[0] << 16 | \
	((uint8_t*)(p))[1] << 8 | \
	((uint8_t*)(p))[2])

#define READ32_LE(p) ( \
	((uint8_t*)(p))[3] << 24 | \
	((uint8_t*)(p))[2] << 16 | \
	((uint8_t*)(p))[1] << 8 | \
	((uint8_t*)(p))[0])

#define READ32_BE(p) ( \
	((uint8_t*)(p))[0] << 24 | \
	((uint8_t*)(p))[1] << 16 | \
	((uint8_t*)(p))[2] << 8 | \
	((uint8_t*)(p))[3])

#define IO_BUFSIZE 256

typedef struct {
	int sock, verbose, timeout, type;
	uint8_t buf[IO_BUFSIZE];
} btio_t;

static int bt_send(btio_t *io, const void *data, int len);

#define BT_FILTER_NOTIFY_NONE -1
#define BT_FILTER_NOTIFY_ALL -2
static int bt_filter_notify = BT_FILTER_NOTIFY_NONE;

static int bt_recv(btio_t *io) {
	int ret, len;
loop:
	if (io->timeout >= 0) {
		struct pollfd fds = { 0 };
		fds.fd = io->sock;
		fds.events = POLLIN;
		ret = poll(&fds, 1, io->timeout);
		if (ret < 0) PERROR_EXIT(poll);
		if (fds.revents & POLLHUP)
			ERR_EXIT("connection closed\n");
		if (!ret) return 0;
	}
	len = read(io->sock, io->buf, sizeof(io->buf));
	if (io->verbose >= 2 && len > 0) {
		DBG_LOG("recv (%d):\n", len);
		print_mem(stderr, io->buf, len);
	}
	if (io->type != 0) return len;
	// handle Exchange MTU Request
	if (len == 3 && io->buf[0] == 0x02) {
		// send Error Response
		static const uint8_t cmd[] = { 0x01,0x02,0x00,0x00,0x06 };
		bt_send(io, cmd, sizeof(cmd));
		goto loop;
	}
	// Handle Value Notification
	if (bt_filter_notify != BT_FILTER_NOTIFY_NONE &&
			len > 3 && io->buf[0] == 0x1b) {
		int handle = READ16_LE(io->buf + 1);
		if (handle != bt_filter_notify) goto loop;
	}
	return len;
}

static int bt_send(btio_t *io, const void *data, int len) {
	const uint8_t *buf = (const uint8_t*)data;
	int ret;

	if (!buf) buf = io->buf;
	if (!len) ERR_EXIT("empty message\n");
	if (io->verbose >= 2) {
		DBG_LOG("send (%d):\n", len);
		print_mem(stderr, buf, len);
	}

	ret = write(io->sock, buf, len);
	if (ret < 0) PERROR_EXIT(write);
	return ret;
}

static void bt_write_req(btio_t *io, int handle) {
	io->buf[0] = 0x12;
	WRITE16_LE(io->buf + 1, handle);
	io->buf[3] = 1;
	io->buf[4] = 0;
	bt_send(io, NULL, 5);
	bt_recv(io);
}

// 06  01 00  ff ff  00 28  d0 18
// 07  19 00  20 00

static int bt_get_type_range(btio_t *io, int value, int *end) {
	int len, start;
	io->buf[0] = 0x06;
	WRITE16_LE(io->buf + 1, 1);
	WRITE16_LE(io->buf + 3, 0xffff);
	WRITE16_LE(io->buf + 5, 0x2800);
	WRITE16_LE(io->buf + 7, value);
	bt_send(io, NULL, 9);
	len = bt_recv(io);
	if (len != 5 || io->buf[0] != 0x07) {
		ERR_EXIT("unexpected response\n");
	}
	start = READ16_LE(io->buf + 1);
	*end = READ16_LE(io->buf + 3);
	return start;
}

enum { ENUM_PRIMARY, ENUM_CHARS, ENUM_CHAR_DESC };

static int enum_handles(btio_t *io, int start, int end, int mode,
		int (*cb)(void*, const uint8_t*, int), void *data) {
	int i, j, len, n;
	while (start <= end) {
		if (mode == ENUM_PRIMARY) {
			io->buf[0] = 0x10; // Read By Group Type Request
			WRITE16_LE(io->buf + 1, start);
			WRITE16_LE(io->buf + 3, end);
			WRITE16_LE(io->buf + 5, 0x2800);
			n = 7;
		} else if (mode == ENUM_CHARS) {
			io->buf[0] = 0x08; // Read By Type Request
			WRITE16_LE(io->buf + 1, start);
			WRITE16_LE(io->buf + 3, end);
			WRITE16_LE(io->buf + 5, 0x2803);
			n = 7;
		} else if (mode == ENUM_CHAR_DESC) {
			io->buf[0] = 0x04; // Find Information Request
			WRITE16_LE(io->buf + 1, start);
			WRITE16_LE(io->buf + 3, end);
			n = 5;
		} else break;
		j = io->buf[0];
		bt_send(io, NULL, n);
		len = bt_recv(io);
		if (len <= 2) {
			DBG_LOG("unexpected length\n");
			break;
		}
		if (io->buf[0] == 0x01) {
			if (len != 5 || io->buf[1] != j || READ16_LE(io->buf + 2) != start || io->buf[4] != 0x0a)
				DBG_LOG("unexpected error response\n");
			else start = end + 1;
			break;
		}
		if (io->buf[0] != j + 1) {
			DBG_LOG("unexpected opcode (0x%02x)\n", io->buf[0]);
			break;
		}
		n = io->buf[1]; j = 0;
		if (mode == ENUM_PRIMARY) {
			if (n == 4 + 2 || n == 4 + 16) j = 4;
		} else if (mode == ENUM_CHARS) {
			if (n == 5 + 2 || n == 5 + 16) j = 5;
		} else if (mode == ENUM_CHAR_DESC) {
			if (n == 1) j = 2, n = 2 + 2;
			else if (n == 2) j = 2, n = 2 + 16;
		}
		if (!j) {
			DBG_LOG("unexpected type (%u)\n", n);
			break;
		}
		if ((len - 2) % n) {
			DBG_LOG("unexpected remainder\n");
			break;
		}
		for (i = 2; i < len; i += n) {
			int h = READ16_LE(io->buf + i);
			if (h < start || h > end) {
				DBG_LOG("handle out of range\n");
				return 1;
			}
			start = h + 1;
			j = cb(data, io->buf + i, n);
			if (j) return j;
		}
	}
	return start <= end;
}

struct bt_find_char_data {
	int len; const int *uuid; int *dest;
};

static int bt_find_char_cb(void *data, const uint8_t *buf, int n) {
	struct bt_find_char_data *x = data;
	if (n != 7) return 0;
	int k, j, len = x->len;
	const int *uuid = x->uuid;
	int *dest = x->dest;
	int a = READ16_LE(buf + 5);
	for (k = j = 0; j < len; j++) {
		if (dest[j] == -1 && uuid[j] == a)
			dest[j] = READ16_LE(buf + 3);
		k += dest[j] != -1;
	}
	return k == len ? 2 : 0;
}

static int bt_find_char(btio_t *io, int start, int end,
		int n, const int *uuid, int *dest) {
	int i, k;
	struct bt_find_char_data data = { n, uuid, dest };
	memset(dest, -1, n * sizeof(*dest));
	i = enum_handles(io, start, end, ENUM_CHARS,
			&bt_find_char_cb, &data);
	if (i == 2) return n;
	for (k = i = 0; i < n; i++) k += dest[i] != -1;
	return k;
}

static int bt_find_char_desc_cb(void *data, const uint8_t *buf, int n) {
	int *x = data;
	if (n == 4 && *x == READ16_LE(buf + 2)) {
		*x = READ16_LE(buf);
		return 2;
	}
	return 0;
}

static int bt_find_char_desc(btio_t *io, int start, int end, int value) {
	int i, data = value;
	i = enum_handles(io, start, end, ENUM_CHAR_DESC,
			&bt_find_char_desc_cb, &data);
	if (i == 2) return data;
	return -1;
}

static void bt_write_req_desc(btio_t *io, int desc_handle, int end) {
	int ret = desc_handle + 1;
	if (ret <= end) {
		ret = bt_find_char_desc(io, ret, ret, 0x2902);
		if (ret >= 0) {
			bt_write_req(io, ret);
			return;
		}
	}
	ERR_EXIT("can't find char desc\n");
}

static int bt_recv_more(btio_t *io, int pos, int n) {
	uint8_t buf[IO_BUFSIZE];
	int len, handle = READ16_LE(io->buf + 1);
	if (pos >= n) return pos;
	if (n > IO_BUFSIZE - 3) return -1;
	memcpy(buf, io->buf + 3, pos);
	for (; pos < n; pos += len) {
		len = bt_recv(io);
		if (len < 3) return -1;
		if (io->buf[0] != 0x1b) return -1;
		if (READ16_LE(io->buf + 1) != handle) return -1;
		len -= 3;
		if (n - pos > len) return -1;
		memcpy(buf + pos, io->buf + 3, len);
	}
	memcpy(io->buf + 3, buf, n);
	return n;
}

#include "uuid_info.h"

static int list_handles_cb(void *data, const uint8_t *buf, int n) {
	int j, mode = (uintptr_t)data & 0xffff;
	int verbose = (uintptr_t)data >> 16;
	int h = READ16_LE(buf);
	if (mode == ENUM_PRIMARY) {
		DBG_LOG("0x%04x: end = 0x%04x, uuid = ",
				h, READ16_LE(buf + 2));
		j = 4;
	} else if (mode == ENUM_CHARS) {
		DBG_LOG("0x%04x: prop = 0x%02x, val = 0x%04x, uuid = ",
				h, buf[2], READ16_LE(buf + 3));
		j = 5;
	} else if (mode == ENUM_CHAR_DESC) {
		DBG_LOG("0x%04x: uuid = ", h);
		j = 2;
	} else return -1;
	buf += j;
	if (n == j + 2)
		DBG_LOG("%08x-0000-1000-8000-00805f9b34fb\n",
				READ16_LE(buf));
	else
		DBG_LOG("%08x-%02x-%02x-%02x-%02x%04x\n",
				READ32_LE(buf + 12), READ16_LE(buf + 10),
				READ16_LE(buf + 8), READ16_LE(buf + 6),
				READ16_LE(buf + 4), READ32_LE(buf));

	if (verbose >= 1 && n == j + 2) {
		int i, uuid = READ16_LE(buf);
		for (i = 0; gatt_uuid_info[i].info; i++)
			if (gatt_uuid_info[i].uuid == uuid) {
				DBG_LOG("info: %s\n", gatt_uuid_info[i].info);
				break;
			}
	}
	return 0;
}

static inline void list_handles(btio_t *io, int mode) {
	enum_handles(io, 1, 0xffff, mode,
			&list_handles_cb, (void*)(intptr_t)(mode | io->verbose << 16));
}

static int pnm_next(FILE *f) {
	int n = -1;
	for (;;) {
		int a = fgetc(f);
		if (a == '#') do a = fgetc(f); while (a != '\n' && a != '\r' && a != EOF);
		if ((unsigned)a - '0' < 10) {
			if (n < 0) n = 0;
			if ((n = n * 10 + a - '0') >= 1 << 16) break;
		} else if (a == ' ' || a == '\n' || a == '\r' || a == '\t') {
			if (n >= 0) return n;
		} else if (a == EOF) return n;
		else break;
	}
	return -1;
}

#include "tjd.h"
#include "moyoung.h"
#include "atorch.h"
#include "yhk_print.h"

static int ch2hex(unsigned a) {
	const char *tab = "abcdef0123456789ABCDEF";
	const char *p = strchr(tab, a);
	return p ? (p - tab + 10) & 15 : -1;
}

static int str2bdaddr(const char *s, bdaddr_t *d) {
	int i;
	for (i = 0; i < 6; i++) {
		int h = ch2hex(*s++), a;
		if (h < 0 || (a = ch2hex(*s++)) < 0) break;
		d->b[5 - i] = h << 4 | a;
		if (*s++ != (i == 5 ? 0 : ':')) break;
	}
	return i - 6;
}

int main(int argc, char **argv) {
	const char *src_str = "00:00:00:00:00:00"; // BDADDR_ANY
	const char *dst_str = NULL;
	bdaddr_t sba, dba;
	int stype = BDADDR_LE_PUBLIC;
	int dtype = BDADDR_LE_PUBLIC;
	int ret;
	btio_t io_buf, *io = &io_buf;
	int verbose = 0;

	while (argc > 1) {
		if (!strcmp(argv[1], "--src")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			src_str = argv[2];
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--dst")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			dst_str = argv[2];
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--stype")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			stype = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--dtype")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			dtype = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--verbose")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			verbose = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else if (argv[1][0] == '-') {
			ERR_EXIT("unknown option\n");
		} else break;
	}

	if (str2bdaddr(src_str, &sba))
		ERR_EXIT("malformed src addr\n");
	if (!dst_str) ERR_EXIT("dst addr required\n");
	if (str2bdaddr(dst_str, &dba))
		ERR_EXIT("malformed dst addr\n");

	io->timeout = 1000;
	io->verbose = verbose;

	if (argc > 1 && !strcmp(argv[1], "yhk_print")) {
		argc -= 1; argv += 1;
		io->type = 2;
		io->sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
		if (io->sock < 0) PERROR_EXIT(socket);
		ret = rfcomm_connect(io->sock, &dba, 2);
		if (ret) PERROR_EXIT(connect);
		yhk_print_main(io, argc, argv);
		goto end;
	}

	io->type = 0;
	io->sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
	if (io->sock < 0) PERROR_EXIT(socket);
	ret = l2cap_bind(io->sock, &sba, stype, 0, ATT_CID);
	if (ret) PERROR_EXIT(bind);
	ret = l2cap_connect(io->sock, &dba, dtype, 0, ATT_CID);
	if (ret) PERROR_EXIT(connect);

	while (argc > 1) {
		if (!strcmp(argv[1], "verbose")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			io->verbose = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "primary")) {
			list_handles(io, ENUM_PRIMARY);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "chars")) {
			list_handles(io, ENUM_CHARS);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "char_desc")) {
			list_handles(io, ENUM_CHAR_DESC);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "tjd")) {
			argc -= 1; argv += 1;
			tjd_main(io, argc, argv);
			break;

		} else if (!strcmp(argv[1], "moyoung")) {
			argc -= 1; argv += 1;
			moyoung_main(io, argc, argv);
			break;

		} else if (!strcmp(argv[1], "atorch")) {
			atorch_loop(io);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "batlevel")) {
			int len;
			io->buf[0] = 0x08; // Read By Type Request
			WRITE16_LE(io->buf + 1, 1);
			WRITE16_LE(io->buf + 3, 0xffff);
			WRITE16_LE(io->buf + 5, 0x2a19);
			bt_send(io, NULL, 7);
			len = bt_recv(io);
			if (len == 5 && io->buf[0] == 0x09 && io->buf[1] == 3) {
				if (io->verbose >= 1)
					DBG_LOG("Handle = 0x%04x (Battery Level)\n", READ16_LE(io->buf + 2));
				DBG_LOG("Battery Level = %u%%\n", io->buf[4]);
			}
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "timeout")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			io->timeout = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else {
			ERR_EXIT("unknown command\n");
		}
	}
end:
	close(io->sock);
}

