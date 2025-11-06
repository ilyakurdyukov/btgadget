/*
 * Mo Young, developer of firmware for smart watches.
 */

static int moyoung_handle[2];

static void moyoung_cmd(btio_t *io, const uint8_t *src, unsigned len) {
	io->buf[0] = 0x52; // Write Command
	WRITE16_LE(io->buf + 1, moyoung_handle[0]);
	io->buf[3] = 0xfe;
	io->buf[4] = 0xea;
	io->buf[5] = src[0]; // version (0x10, 0x20)
	io->buf[6] = len + 3;
	memcpy(io->buf + 7, src + 1, len - 1);
	bt_send(io, NULL, 6 + len);
}

static int moyoung_recv(btio_t *io) {
	int len = bt_recv(io);
	if (!len) ERR_EXIT("no response\n");
	if (io->buf[0] != 0x1b || len < 6)
		ERR_EXIT("unexpected response\n");
	if (READ16_LE(io->buf + 1) != moyoung_handle[1]) 
		ERR_EXIT("unexpected handle\n");
	len -= 3;
	if (io->buf[3] != 0xfe || io->buf[4] != 0xea) ERR_EXIT("wrong magic\n");
	if (io->buf[6] != len) ERR_EXIT("wrong length\n");
	return len;
}

static void moyoung_init(btio_t *io) {
	static const int uuid[] = { 0xfee2, 0xfee3 };
	int ret, start, end;

	bt_filter_notify = BT_FILTER_NOTIFY_ALL;
	start = bt_get_type_range(io, 0xfeea, &end);
	ret = bt_find_char(io, start, end, 2, uuid, moyoung_handle);
	if (ret != 2) ERR_EXIT("can't find char handle\n");
	if (io->verbose >= 1)
		DBG_LOG("write = 0x%x, read = 0x%x\n",
				moyoung_handle[0], moyoung_handle[1]);
	bt_write_req_desc(io, moyoung_handle[1], end);
	bt_filter_notify = moyoung_handle[1];
}

static void moyoung_main(btio_t *io, int argc, char **argv) {
	int ver;
	if (0) {
		int len, timeout_old = io->timeout;
		io->timeout = 10;
		len = bt_recv(io);
		if (len) {
			// Handle Value Notification
			static const uint8_t data[] = { 0x1b,0x42,0x00 }; // handle = 0x42
			if (len != 12 || memcmp(io->buf, data, 3))
				ERR_EXIT("unexpected response\n");
			if (io->verbose >= 1)
				DBG_LOG("activity: steps = %u, unknown = %u, calories = %u\n",
						io->buf[3] | io->buf[3 + 1] << 8 | io->buf[3 + 2] << 16,
						io->buf[6] | io->buf[6 + 1] << 8 | io->buf[6 + 2] << 16,
						io->buf[9] | io->buf[9 + 1] << 8 | io->buf[9 + 2] << 16);
		}
		io->timeout = timeout_old;
	}
	moyoung_init(io);

	{
		static const uint8_t cmd[] = { 0x10,0x5a,0x00 };
		int len;
		moyoung_cmd(io, cmd, sizeof(cmd));
		len = moyoung_recv(io);
		if (len <= 6 || io->buf[7] != 0x5a || io->buf[8] != 0x00)
			ERR_EXIT("unexpected response\n");
		ver = io->buf[5];
		if (io->verbose >= 1) {
			DBG_LOG("api_ver = %u.%u\n", ver >> 4, ver & 15);
			DBG_LOG("api_name = \"");
			print_esc_str(stderr, io->buf + 3 + 6, len - 6);
			DBG_LOG("\"\n");
		}
	}

	while (argc > 1) {
		if (!strcmp(argv[1], "verbose")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			io->verbose = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "info")) {
			static const uint8_t cmd[] = { 0x10,0x5a,0x01 };
			int len;
			moyoung_cmd(io, cmd, sizeof(cmd));
			len = moyoung_recv(io);
			if (len <= 6 || io->buf[7] != 0x5a || io->buf[8] != 0x01)
				ERR_EXIT("unexpected response\n");
			DBG_LOG("fw_name = \"");
			print_esc_str(stderr, io->buf + 3 + 6, len - 6);
			DBG_LOG("\"\n");
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "getlanguage")) {
			static const uint8_t cmd[] = { 0x10,0x2b };
			int i, n, len;
			moyoung_cmd(io, cmd, sizeof(cmd));
			len = moyoung_recv(io);
			if (len != 14 || io->buf[7] != 0x2b )
				ERR_EXIT("unexpected response\n");
			DBG_LOG("language = %u\n", io->buf[8]);
			DBG_LOG("supported languages:");
			for (i = n = 0; i < 64; i++) {
				int a = io->buf[9 + ((i >> 3) ^ 3)];
				if (a >> (i & 7) & 1)
					DBG_LOG("%s %u", n++ ? "," : "", i);
			}
			DBG_LOG("\n");
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "setlanguage")) {
			uint8_t cmd[] = { 0x10,0x1b,0x00 };
			if (argc <= 2) ERR_EXIT("bad command\n");
			cmd[2] = strtol(argv[2], NULL, 0);
			moyoung_cmd(io, cmd, sizeof(cmd));
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "getautolock")) {
			static const uint8_t cmd[] = { 0x10,0x8d };
			int i, n, len;
			moyoung_cmd(io, cmd, sizeof(cmd));
			len = moyoung_recv(io);
			if (len != 7 || io->buf[7] != 0x8d )
				ERR_EXIT("unexpected response\n");
			DBG_LOG("lock_time = %u\n", io->buf[8]);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "setautolock")) {
			uint8_t cmd[] = { 0x10,0x7d,0x00 };
			int val;
			if (argc <= 2) ERR_EXIT("bad command\n");
			val = strtol(argv[2], NULL, 0);
			val = val < 5 ? 5 : val > 30 ? 30 : val;
			cmd[2] = val;
			moyoung_cmd(io, cmd, sizeof(cmd));
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "gettimeformat")) {
			static const uint8_t cmd[] = { 0x10,0x27 };
			int i, n, len;
			moyoung_cmd(io, cmd, sizeof(cmd));
			len = moyoung_recv(io);
			if (len != 6 || io->buf[7] != 0x27 )
				ERR_EXIT("unexpected response\n");
			DBG_LOG("time_format = %u\n", io->buf[8] ? 24 : 12);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "settimeformat")) {
			uint8_t cmd[] = { 0x10,0x17,0x00 };
			int val;
			if (argc <= 2) ERR_EXIT("bad command\n");
			val = strtol(argv[2], NULL, 0);
			switch (val) {
			case 12: val = 0; break;
			case 24: val = 1; break;
			default: ERR_EXIT("unknown time format\n");
			}
			cmd[2] = val;
			moyoung_cmd(io, cmd, sizeof(cmd));
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "finddev")) {
			static const uint8_t cmd[] = { 0x10,0x61 };
			moyoung_cmd(io, cmd, sizeof(cmd));
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "timesync")) {
			uint8_t cmd[7];
			time_t t = time(NULL);
			// ugly, but portable
			int gmtoff = difftime(t, mktime(gmtime(&t)));
			t += gmtoff - 8 * 3600;
			cmd[0] = 0x10;
			cmd[1] = 0x31;
			// seconds since 1970.01.01
			WRITE32_BE(cmd + 2, t);
			cmd[6] = 0x08; // timezone
			moyoung_cmd(io, cmd, sizeof(cmd));
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "timeout")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			io->timeout = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else {
			ERR_EXIT("unknown command\n");
		}
	}
}

