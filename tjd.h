/*
 * TJD (TENG JINDA), developer of firmware for cheap fitness bracelets.
 * Seems to be associated with the Lefun brand, but can also be found
 * on noname devices.
 * Also on fake smartwatches with a bracelet circuit board inside.
 */

static int tjd_handle[2] = { 0x1b, 0x1e };

static int tjd_crc8(const uint8_t *s, unsigned n) {
	unsigned j, c = 0;
	while (n--) {
		c ^= *s++;
		for (j = 0; j < 8; j++)
			c = c >> 1 ^ ((0u - (c & 1)) & 0x8c);
	}
	return c;
}

static void tjd_cmd(btio_t *io, const uint8_t *src, unsigned len, int flags) {
	int pos = 4;
	io->buf[0] = 0x52; // Write Command
	WRITE16_LE(io->buf + 1, tjd_handle[0]);
	io->buf[3] = 0xab;
	if (flags & 1) io->buf[pos++] = len + 3;
	memcpy(io->buf + pos, src, len);
	pos += len;
	if (flags & 2) {
		io->buf[pos] = tjd_crc8(io->buf + 3, pos - 3);
		pos++;
	}
	bt_send(io, NULL, pos);
}

static int tjd_recv(btio_t *io) {
	int len = bt_recv(io);
	if (!len) ERR_EXIT("no response\n");
	if (io->buf[0] != 0x1b || len < 6)
		ERR_EXIT("unexpected response\n");
	if (READ16_LE(io->buf + 1) != tjd_handle[1]) 
		ERR_EXIT("unexpected handle\n");
	len -= 3;
	if (io->buf[3] != 0x5a) ERR_EXIT("wrong magic\n");
	if (io->buf[4] != len) ERR_EXIT("wrong length\n");
	if (io->buf[3 + len - 1] != tjd_crc8(io->buf + 3, len - 1)) 
		ERR_EXIT("wrong checksum\n");
	return len;
}

static int tjd_recv_timer(btio_t *io, int timeout) {
	int timeout_old = io->timeout;
	int len;
	io->timeout = timeout; /* very slow response */
	len = tjd_recv(io);
	io->timeout = timeout_old;
	return len;
}

static void tjd_dialpush(btio_t *io, const char *fn) {
	uint8_t cmd[20];
	uint8_t *mem; size_t size = 0;
	int i, n, len;

	mem = loadfile(fn, &size, 0xffff0);
	if (!mem) ERR_EXIT("loadfile failed\n");
	n = (size + 15) >> 4;
	cmd[0] = 0x28;
	WRITE16_BE(cmd + 1, n);
	cmd[3] = 1;
	tjd_cmd(io, cmd, 4, 3);
	/* very slow response, ~6 sec */
	len = tjd_recv_timer(io, 10000);
	if (len != 5 || memcmp(io->buf + 5, "\x28\x01", 2))
		ERR_EXIT("dialpush failed\n");

	cmd[1] = 0x29;
	for (i = 0; i < n; i++) {
		int nn = size - i * 16;
		if (nn >= 16) nn = 16;
		else memset(cmd + 4, 0, 16);
		memcpy(cmd + 4, mem + i * 16, nn);
		WRITE16_BE(cmd + 2, i);
		tjd_cmd(io, cmd + 1, 19, 0);
		usleep(10 * 1000); /* take it slow */
	}
	free(mem);
}

static void tjd_init(btio_t *io) {
	static const int uuid[] = { 0x2d01, 0x2d00 };
	int ret, start, end;

	start = bt_get_type_range(io, 0x18d0, &end);
	ret = bt_find_char(io, start, end, 2, uuid, tjd_handle);
	if (ret != 2) ERR_EXIT("can't find char handle\n");
	if (io->verbose >= 1)
		DBG_LOG("write = 0x%x, read = 0x%x\n",
				tjd_handle[0], tjd_handle[1]);

	bt_write_req_desc(io, tjd_handle[1], end);
	// ret = bt_find_char_desc(io, start, end, 0x2902);
	// if (ret < 0) ERR_EXIT("can't find char desc\n");
	// bt_write_req(io, ret);
}

static void tjd_main(btio_t *io, int argc, char **argv) {

	tjd_init(io);

	while (argc > 1) {
		if (!strcmp(argv[1], "verbose")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			io->verbose = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "info")) {
			static const uint8_t cmd1[] = { 0x00 };
			static const uint8_t cmd2[] = { 0x39,0x00 };
			int len;
			tjd_cmd(io, cmd1, sizeof(cmd1), 3);
			len = tjd_recv(io);
			if (len == 0x14 && io->buf[5] == 0x00) {
				DBG_LOG("DevInfo:\n");
				DBG_LOG("Support = 0x%04x\n", READ16_LE(io->buf + 6));
				DBG_LOG("DevTypeReserve = \"%04x\"\n", READ16_BE(io->buf + 8));
				DBG_LOG("Type = \"%08x\"\n", READ32_BE(io->buf + 10));
				DBG_LOG("HWVer = %u.%u\n", io->buf[14], io->buf[15]);
				DBG_LOG("SWVer = %u.%u\n", io->buf[16], io->buf[17]);
				DBG_LOG("Vendor = \"%08x\"\n", READ32_BE(io->buf + 18));
				DBG_LOG("\n");
			}
			tjd_cmd(io, cmd2, sizeof(cmd2), 3);
			len = tjd_recv(io);
			if ((len == 11 || len == 12) && io->buf[5] == 0x39) {
				DBG_LOG("DialPara:\n");
				DBG_LOG("Type = %u\n", io->buf[6]);
				DBG_LOG("Width = %u\n", READ16_BE(io->buf + 7));
				DBG_LOG("Height = %u\n", READ16_BE(io->buf + 9));
				DBG_LOG("Size = %u\n", READ16_BE(io->buf + 11));
				DBG_LOG("\n");
			}
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "batlevel")) {
			static const uint8_t cmd[] = { 0x03 };
			int len;
			tjd_cmd(io, cmd, sizeof(cmd), 3);
			len = tjd_recv(io);
			if (len == 5 && io->buf[5] == 0x03)
				DBG_LOG("BatteryLevel = %u%%\n", io->buf[6]);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "finddev")) {
			static const uint8_t cmd[] = { 0x09 };
			tjd_cmd(io, cmd, sizeof(cmd), 3);
			tjd_recv_timer(io, 5000); /* ~2 sec */
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "timesync")) {
			uint8_t cmd[8];
			time_t t = time(NULL);
			struct tm *tm = localtime(&t);
			cmd[0] = 0x04;
			cmd[1] = 0x01;
			cmd[2] = (tm->tm_year + 1900) % 100;
			cmd[3] = tm->tm_mon + 1;
			cmd[4] = tm->tm_mday;
			cmd[5] = tm->tm_hour;
			cmd[6] = tm->tm_min;
			cmd[7] = tm->tm_sec;
			tjd_cmd(io, cmd, sizeof(cmd), 3);
			tjd_recv(io);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "dialpush")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			tjd_dialpush(io, argv[2]);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "setlanguage")) {
			uint8_t cmd[] = { 0x21,0x00 };
			if (argc <= 2) ERR_EXIT("bad command\n");
			cmd[1] = strtol(argv[2], NULL, 0);
			tjd_cmd(io, cmd, sizeof(cmd), 3);
			tjd_recv(io);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "langget")) {
			static const uint8_t cmd[] = { 0x02,0x00 };
			int len;
			tjd_cmd(io, cmd, sizeof(cmd), 3);
			len = tjd_recv(io);
			if (len == 8 && io->buf[5] == 0x02 && io->buf[6] == 0x00) {
				DBG_LOG("LangGet:\n");
				// DBG_LOG("Language = %u (unused)\n", io->buf[7]);
				DBG_LOG("TimeFormat = %u (%s)\n", io->buf[8], io->buf[8] ? "12" : "24");
				DBG_LOG("UnitSystem = %u (%s)\n", io->buf[9], io->buf[9] ? "Imperial" : "Metric");
				DBG_LOG("\n");
			}
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "uiget")) {
			static const uint8_t cmd[] = { 0x07,0x00 };
			int len;
			tjd_cmd(io, cmd, sizeof(cmd), 3);
			len = tjd_recv(io);
			if (len == 7 && io->buf[5] == 0x07 && io->buf[6] == 0x00) {
				int mask = READ16_BE(io->buf + 7);
				DBG_LOG("UIGet:\n");
				DBG_LOG("Mask = 0x%04x\n", mask);
				DBG_LOG("\n");
			}
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "uiset")) {
			uint8_t cmd[4] = { 0x07,0x01 };
			int mask;
			if (argc <= 2) ERR_EXIT("bad command\n");
			mask = strtol(argv[2], NULL, 0);
			WRITE16_BE(cmd + 2, mask);
			tjd_cmd(io, cmd, sizeof(cmd), 3);
			tjd_recv(io);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "funcget")) {
			static const uint8_t cmd[] = { 0x08,0x00 };
			int len;
			tjd_cmd(io, cmd, sizeof(cmd), 3);
			len = tjd_recv(io);
			if (len == 7 && io->buf[5] == 0x08 && io->buf[6] == 0x00) {
				int mask = READ16_BE(io->buf + 7);
				DBG_LOG("FuncGet:\n");
				DBG_LOG("Mask = 0x%04x\n", mask);
				DBG_LOG("\n");
			}
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "funcset")) {
			uint8_t cmd[4] = { 0x08,0x01 };
			int mask;
			if (argc <= 2) ERR_EXIT("bad command\n");
			mask = strtol(argv[2], NULL, 0);
			WRITE16_BE(cmd + 2, mask);
			tjd_cmd(io, cmd, sizeof(cmd), 3);
			tjd_recv(io);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "timeout")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			io->timeout = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else {
			ERR_EXIT("unknown command\n");
		}
	}
}

