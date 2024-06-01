/*
 * ATorch, developer of various AC/DC/USB tester devices.
 * Tested: J7-c (USB tester).
 */

static int atorch_handle = 0xc;

static void atorch_init(btio_t *io) {
	static const int uuid[] = { 0xffe1 };
	int ret, start, end;

	start = bt_get_type_range(io, 0xffe0, &end);
	ret = bt_find_char(io, start, end, 1, uuid, &atorch_handle);
	if (ret != 1) ERR_EXIT("can't find char handle\n");
	if (io->verbose >= 1)
		DBG_LOG("handle = 0x%x\n", atorch_handle);
	bt_write_req_desc(io, atorch_handle, end);
}

static int atorch_next(btio_t *io) {
	int len = bt_recv(io);
	if (len < 3) return -1;
	if (io->buf[0] != 0x1b) return -1;
	if (READ16_LE(io->buf + 1) != atorch_handle) return -1;
	return len - 3;
}

static int atorch_checksum(const uint8_t *s, unsigned n) {
	unsigned c = 0;
	while (n--) c += *s++;
	return (c ^ 0x44) & 0xff;
}

static void atorch_loop(btio_t *io) {
	atorch_init(io);
	io->timeout = 3000;
	for (;;) {
		int len, n, chk;
		len = atorch_next(io);
		if (len < 3) break;
		if (io->buf[3] != 0xff || io->buf[4] != 0x55) break;
		if (io->buf[5] == 0x01) {
			if (io->buf[6] != 0x03) continue; // USB tester
			len = bt_recv_more(io, len, n = 4 + 32);
			if (len != n) break;
			uint8_t *buf = io->buf + 3;
			// print_mem(stderr, buf, n);
/*
ff 55 01 03 00 01 f3 00 00 06 00 00 28 00 00 00
01 00 08 00 08 00 12 00 00 05 28 3c 0c 80 00 00
03 20 00 24
*/
			chk = atorch_checksum(buf + 3, n - 4);
			if (buf[n - 1] != chk)
				ERR_EXIT("bad checksum (expected 0x%02x, got 0x%02x)\n", chk, buf[n - 1]);
			{
				DBG_LOG("\n");
				int vol = READ24_BE(buf + 4);
				DBG_LOG("Vol:%d.%02uV\n", vol / 100, vol % 100);
				int cur = READ24_BE(buf + 7);
				DBG_LOG("Cur:%d.%02uA\n", cur / 100, cur % 100);
				int cap = READ24_BE(buf + 10);
				DBG_LOG("Cap:%dmAh\n", cap);
				int ene = READ32_BE(buf + 13);
				DBG_LOG("Ene:%d.%02uWh\n", ene / 100, ene % 100);
				int dm = READ16_BE(buf + 17);
				DBG_LOG("D-:%d.%02uV\n", dm / 100, dm % 100);
				int dp = READ16_BE(buf + 19);
				DBG_LOG("D+:%d.%02uV\n", dp / 100, dp % 100);
				int cpu = READ16_BE(buf + 0x15);
				DBG_LOG("CPU:%d\u00b0C\n", cpu);
				int hour = READ16_BE(buf + 0x17);
				DBG_LOG("Tme:%04u-%02u-%02u\n", hour, buf[0x19], buf[0x1a]);
			}
		}
	}
}
