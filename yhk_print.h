/*
 * YHK mini printer
 */

static int yhk_print_readstr(btio_t *io, uint8_t *buf, int n) {
	int i;
	for (i = 0; i < n; i++) {
		if (read(io->sock, buf + i, 1) != 1) return -1;
		if (!buf[i]) break;
	}
	if (i == n) return -1;
	return i;
}

static uint8_t* yhk_print_read_pbm(const char *fn, unsigned width, unsigned *height) {
	FILE *f = NULL; uint8_t *data = NULL;
	do {
		unsigned t, w, h;
		if (!fn || !(f = fopen(fn, "rb"))) break;
		if (fgetc(f) != 'P') break;
		if (fgetc(f) != '4') break;
		if ((w = pnm_next(f)) != width)
			ERR_EXIT("image width must be %u\n", width);
		if (((h = pnm_next(f)) - 1) >> 14) break;
		w = (w + 7) >> 3;
		t = w * h;
		if (!(data = malloc(t))) break;
		if (fread(data, 1, t, f) != t) goto err;
		*height = h;
		goto ok;
	} while (0);
err:
	if (data) { free(data); data = NULL; }
ok:
	if (f) fclose(f);
	return data;
}

static void yhk_print_main(btio_t *io, int argc, char **argv) {
	uint8_t buf[256];
	int yhk_width = 384;

	while (argc > 1) {
		if (!strcmp(argv[1], "verbose")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			io->verbose = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "serial")) {
			static const uint8_t cmd[] = { 0x1d,0x67,0x39 };
			int len;
			bt_send(io, cmd, 3);
			len = yhk_print_readstr(io, buf, sizeof(buf));
			if (len < 0) ERR_EXIT("readstr failed\n");
			fprintf(stderr, "serial: \"");
			print_esc_str(stderr, buf, len);
			fprintf(stderr, "\"\n");
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "err")) {
			static const uint8_t cmd[] = { 0x1d,0x67,0x53 };
			int len; const char *s = "unknown";
			bt_send(io, cmd, 3);
			if (read(io->sock, buf, 6) != 6)
				ERR_EXIT("read failed\n");
			if (memcmp(buf, "err:", 4) || buf[5] != '.')
				ERR_EXIT("unexpected response\n");
			switch (buf[4]) {
			case 0: s = "ok"; break;
			case 2: s = "no paper"; break;
			}
			fprintf(stderr, "error: %u (%s)\n", buf[4], s);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "info")) {
			static const uint8_t cmd[] = { 0x1e,0x47,0x03 };
			int len;
			bt_send(io, cmd, 3);
			len = yhk_print_readstr(io, buf, sizeof(buf));
			if (len < 0) ERR_EXIT("readstr failed\n");
			/* dumb logic from the application code */
			if (!strstr((char*)buf, "DPI=384,")) yhk_width = 576;
			fprintf(stderr, "info: \"");
			print_esc_str(stderr, buf, len);
			fprintf(stderr, "\"\n");
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "dpi")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			yhk_width = atoi(argv[2]);
			if ((yhk_width & 7) || (yhk_width >> 11) || yhk_width <= 0)
				ERR_EXIT("unexpected DPI\n");
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "id")) {
			static const uint8_t cmd[] = { 0x1d,0x67,0x69 };
			int len;
			bt_send(io, cmd, 3);
			len = yhk_print_readstr(io, buf, sizeof(buf));
			if (len < 0) ERR_EXIT("readstr failed\n");
			fprintf(stderr, "id: \"");
			print_esc_str(stderr, buf, len);
			fprintf(stderr, "\"\n");
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "print")) {
			uint8_t cmd1[] = { 0x1d,0x49,0xf0, 0 };
			static const uint8_t cmd2[] = { 0x1b,0x40 };
			uint8_t cmd3[] = { 0x1d,0x76,0x30,0, 0,0,0,0 };
			static const uint8_t cmd4[] = { 0x0a,0x0a,0x0a,0x0a };
			const char *fn; unsigned y, height, st;
			uint8_t *image;
			if (argc <= 2) ERR_EXIT("bad command\n");
			image = yhk_print_read_pbm(argv[2], yhk_width, &height);
			if (!image) ERR_EXIT("read_pbm failed\n");

			cmd1[3] = 0x19; // unknown setting
			bt_send(io, cmd1, 4);
			bt_send(io, cmd2, 2);
			//usleep(500000); // 0.5s wait

			st = (yhk_width + 7) >> 3;
			cmd3[4] = st;
			cmd3[6] = height;
			cmd3[7] = height >> 8;
			bt_send(io, cmd3, 8);
			for (y = 0; y < height; y++)
				bt_send(io, image + y * st, st);
			bt_send(io, cmd4, 4);
			free(image);
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

