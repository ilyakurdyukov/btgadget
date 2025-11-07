/*
 * Mo Young, developer of firmware for smart watches.
 */

static int moyoung_handle[2];

static void moyoung_cmd(btio_t *io, const uint8_t *src, unsigned len) {
	io->buf[0] = 0x52; // Write Command
	WRITE16_LE(io->buf + 1, moyoung_handle[0]);
	io->buf[3] = 0xfe;
	io->buf[4] = 0xea;
	io->buf[5] = 0x10; // version (0x10, 0x20)
	io->buf[6] = len + 4;
	memcpy(io->buf + 7, src, len);
	bt_send(io, NULL, 7 + len);
}

static int moyoung_recv(btio_t *io) {
	int len = bt_recv(io), len2;
	if (!len) ERR_EXIT("no response\n");
	if (io->buf[0] != 0x1b || len < 6)
		ERR_EXIT("unexpected response\n");
	if (READ16_LE(io->buf + 1) != moyoung_handle[1]) 
		ERR_EXIT("unexpected handle\n");
	len -= 3;
	if (io->buf[3] != 0xfe || io->buf[4] != 0xea) ERR_EXIT("wrong magic\n");
	len2 = io->buf[6];
	if (len2 != len) {
		if (len2 < len) ERR_EXIT("wrong length\n");
		len = bt_recv_more(io, len, len2);
		if (len2 != len) ERR_EXIT("wrong length\n");
	}
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
		static const uint8_t cmd[] = { 0x5a,0x00 };
		int len;
		moyoung_cmd(io, cmd, sizeof(cmd));
		len = moyoung_recv(io);
		if (len <= 6 || memcmp(io->buf + 7, cmd, 2))
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
			static const uint8_t cmd[] = { 0x5a,0x01 };
			int len;
			moyoung_cmd(io, cmd, sizeof(cmd));
			len = moyoung_recv(io);
			if (len <= 6 || memcmp(io->buf + 7, cmd, 2))
				ERR_EXIT("unexpected response\n");
			DBG_LOG("fw_name = \"");
			print_esc_str(stderr, io->buf + 3 + 6, len - 6);
			DBG_LOG("\"\n");
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "getlanguage")) {
			static const uint8_t cmd[] = { 0x2b };
			int i, n, len;
			moyoung_cmd(io, cmd, sizeof(cmd));
			len = moyoung_recv(io);
			if (len != 14 || io->buf[7] != cmd[0])
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
			if (argc <= 2) ERR_EXIT("bad command\n");
			{
				int val = strtol(argv[2], NULL, 0);
				uint8_t cmd[] = { 0x1b, val };
				moyoung_cmd(io, cmd, sizeof(cmd));
			}
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "getautolock")) {
			static const uint8_t cmd[] = { 0x8d };
			int i, n, len;
			moyoung_cmd(io, cmd, sizeof(cmd));
			len = moyoung_recv(io);
			if (len != 7 || io->buf[7] != cmd[0])
				ERR_EXIT("unexpected response\n");
			DBG_LOG("lock_time = %u\n", io->buf[8]);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "setautolock")) {
			int val;
			if (argc <= 2) ERR_EXIT("bad command\n");
			val = strtol(argv[2], NULL, 0);
			val = val < 5 ? 5 : val > 30 ? 30 : val;
			{
				uint8_t cmd[] = { 0x7d, val };
				moyoung_cmd(io, cmd, sizeof(cmd));
			}
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "gettimeformat")) {
			static const uint8_t cmd[] = { 0x27 };
			int i, n, len;
			moyoung_cmd(io, cmd, sizeof(cmd));
			len = moyoung_recv(io);
			if (len != 6 || io->buf[7] != cmd[0] )
				ERR_EXIT("unexpected response\n");
			DBG_LOG("time_format = %u\n", io->buf[8] ? 24 : 12);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "settimeformat")) {
			int val;
			if (argc <= 2) ERR_EXIT("bad command\n");
			val = strtol(argv[2], NULL, 0);
			if (val != 12 && val != 24)
				ERR_EXIT("unknown time format\n");
			{
				uint8_t cmd[] = { 0x17, val == 24 };
				moyoung_cmd(io, cmd, sizeof(cmd));
			}
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "getecardlist")) {
			static const uint8_t cmd[] = { 0xb9,0x12,0x00,0x02,0x00 };
			int i, len;
			moyoung_cmd(io, cmd, sizeof(cmd));
			len = moyoung_recv(io);
			if (len < 10 || memcmp(io->buf + 7, cmd, 4))
				ERR_EXIT("unexpected response\n");
			len -= 10;
			DBG_LOG("max_items = %u\n", io->buf[11]);
			DBG_LOG("max_data = %u\n", io->buf[12]);
			DBG_LOG("ecard_list:");
			for (i = 0; i < len; i++) {
				int a = io->buf[13 + i];
				DBG_LOG("%s %u", i ? "," : "", a);
			}
			DBG_LOG("\n");
			argc -= 1; argv += 1;

		// There is no way to delete E-Cards? Da Fit doesn't do that.
		} else if (!strcmp(argv[1], "setecardlist")) {
			uint8_t cmd[255 - 4];
			int n = 0; char *s;
			if (argc <= 2) ERR_EXIT("bad command\n");
			s = argv[2];
			if (*s)
			for (;;) {
				int a = strtol(s, &s, 0);
				if ((int)sizeof(cmd) < 5 + n)
					ERR_EXIT("too big ecard data\n");
				cmd[4 + n++] = a;
				a = *s++;
				if (!a) break;
				if (a != ',') ERR_EXIT("invalid separator\n");
			}
			cmd[0] = 0xb9;
			cmd[1] = 2; cmd[2] = 0; cmd[3] = 4;
			moyoung_cmd(io, cmd, 4 + n);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "getecard")) {
			int idx, len, n1, n2;
			if (argc <= 2) ERR_EXIT("bad command\n");
			idx = strtol(argv[2], NULL, 0);
			{
				uint8_t cmd[] = { 0xb9,0x12,0x00,0x03, idx };
				moyoung_cmd(io, cmd, sizeof(cmd));
				len = moyoung_recv(io);
				if (len < 11 || memcmp(io->buf + 7, cmd, 5))
					ERR_EXIT("unexpected response\n");
			}
			len -= 11;
			n1 = io->buf[12];
			if (len < n1) ERR_EXIT("malformed ecard\n");
			len -= n1;
			n2 = io->buf[13 + n1];
			if (len != n2) ERR_EXIT("malformed ecard\n");
			DBG_LOG("ecard[%u].name = \"", idx);
			print_esc_str(stderr, io->buf + 13, n1);
			DBG_LOG("\"\n");
			DBG_LOG("ecard[%u].data = \"", idx);
			print_esc_str(stderr, io->buf + 14 + n1, n2);
			DBG_LOG("\"\n");
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "setecard")) {
			uint8_t cmd[255 - 4];
			int idx, len, n1, n2;
			const char *ecard_name, *ecard_data;
			if (argc <= 4) ERR_EXIT("bad command\n");
			idx = strtol(argv[2], NULL, 0);
			ecard_name = argv[3];
			ecard_data = argv[4];
			n1 = strlen(ecard_name);
			n2 = strlen(ecard_data);
			if ((int)sizeof(cmd) < 8 + n1 + n2)
				ERR_EXIT("too big ecard data\n");
			cmd[0] = 0xb9;
			cmd[1] = 2; cmd[2] = 0; cmd[3] = 0;
			cmd[4] = idx; cmd[5] = n1;
			memcpy(cmd + 6, ecard_name, n1);
			cmd[6 + n1] = n2;
			memcpy(cmd + 7 + n1, ecard_data, n2);
			moyoung_cmd(io, cmd, 7 + n1 + n2);
			argc -= 4; argv += 4;

		} else if (!strcmp(argv[1], "finddev")) {
			static const uint8_t cmd[] = { 0x61 };
			moyoung_cmd(io, cmd, sizeof(cmd));
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "timesync")) {
			uint8_t cmd[6];
			time_t t = time(NULL);
			// ugly, but portable
			int gmtoff = difftime(t, mktime(gmtime(&t)));
			t += gmtoff - 8 * 3600;
			cmd[0] = 0x31;
			// seconds since 1970.01.01
			WRITE32_BE(cmd + 1, t);
			cmd[5] = 0x08; // timezone
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

