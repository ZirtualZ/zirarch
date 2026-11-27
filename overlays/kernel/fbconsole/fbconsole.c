#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "font-8x16.h"

#define WIDTH 720
#define HEIGHT 1280
#define BPP 4
#define STRIDE (WIDTH * BPP)
#define MAX_COLS 90
#define MAX_ROWS 56
#define SCALE_MIN 1
#define SCALE_MAX 2
#define CELL_W (8 * text_scale)
#define CELL_H (16 * text_scale)
#define COLS (WIDTH / CELL_W)
#define KEYBOARD_ROWS 5
#define KEY_HEIGHT 76
#define KEYBOARD_HEIGHT (KEYBOARD_ROWS * KEY_HEIGHT)
#define ROWS (text_rows())
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOPAN_DISPLAY 0x4606
#define TIOCSPTLCK 0x40045431
#define TIOCGPTN 0x80045430
#define EV_KEY 0x01
#define EV_ABS 0x03
#define EV_SYN 0x00
#define KEY_VOLUMEDOWN 114
#define KEY_VOLUMEUP 115
#define KEY_POWER 116
#define BTN_TOUCH 0x14a
#define ABS_MT_SLOT 0x2f
#define ABS_MT_TRACKING_ID 0x39
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36

struct input_absinfo {
	int32_t value;
	int32_t minimum;
	int32_t maximum;
	int32_t fuzz;
	int32_t flat;
	int32_t resolution;
};

struct arm_input_event {
	int32_t tv_sec;
	int32_t tv_usec;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

static uint8_t *framebuffer;
static uint8_t cells[MAX_ROWS][MAX_COLS];
static uint16_t cursor_row, cursor_col;
static int fbfd = -1, master = -1;
static size_t stride = STRIDE;
static unsigned char var_screeninfo[160];
static int touchfd = -1;
static int powerfd = -1;
static int touch_x, touch_y, touch_active, touch_slot, touch_handled;
static int touch_x_min, touch_x_max = 720, touch_y_min, touch_y_max = 1280;
static int shift_locked, symbols_mode, ctrl_armed;
static int display_awake = 1;
static int text_scale = 2;

static int text_rows(void)
{
	return (HEIGHT - KEYBOARD_HEIGHT) / (16 * text_scale);
}

static int refresh(void);

static void trace(const char *text)
{
	int fd = open("/tmp/zirarch-fbconsole.log", O_WRONLY | O_CREAT | O_APPEND,
		0600);
	if (fd >= 0) {
		write(fd, text, strlen(text));
		close(fd);
	}
}

static void pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
	uint8_t *p = framebuffer + y * stride + x * BPP;
	p[0] = b;
	p[1] = g;
	p[2] = r;
	p[3] = 0xff;
}

static void rectangle(int x, int y, int width, int height,
	uint8_t r, uint8_t g, uint8_t b)
{
	int row, col;
	for (row = y; row < y + height; row++)
		for (col = x; col < x + width; col++)
			pixel(col, row, r, g, b);
}

static void glyph_at(int x, int y, unsigned char c, int scale)
{
	const uint8_t *glyph = font_8x16 + (uint32_t)c * 16;
	int row, col, sx, sy;
	for (row = 0; row < 16; row++)
		for (col = 0; col < 8; col++)
			if (glyph[row] & (0x80 >> col))
				for (sy = 0; sy < scale; sy++)
					for (sx = 0; sx < scale; sx++)
						pixel(x + col * scale + sx,
							y + row * scale + sy,
							255, 255, 255);
}

static void label_at(int x, int y, int width, const char *label)
{
	int length = strlen(label);
	int text_scale = 2;
	int text_width = length * 8 * text_scale;
	int start = x + (width - text_width) / 2;
	int i;
	for (i = 0; i < length; i++)
		glyph_at(start + i * 8 * text_scale,
			y + (KEY_HEIGHT - 16 * text_scale) / 2,
			label[i], text_scale);
}

static void text_at(int x, int y, const char *text, int scale)
{
	while (*text) {
		glyph_at(x, y, *text++, scale);
		x += 8 * scale;
	}
}

static void draw_boot_splash(void)
{
	const char *title = "ZIRARCH // SM-G550T";
	const char *subtitle = "+ Alpine armv7";
	int title_scale = 3;
	int subtitle_scale = 2;
	int title_width = strlen(title) * 8 * title_scale;
	int subtitle_width = strlen(subtitle) * 8 * subtitle_scale;

	rectangle(0, 0, WIDTH, HEIGHT, 4, 8, 14);
	rectangle((WIDTH - 512) / 2, HEIGHT / 2 - 66, 512, 3,
		50, 160, 220);
	text_at((WIDTH - title_width) / 2, HEIGHT / 2 - 42,
		title, title_scale);
	text_at((WIDTH - subtitle_width) / 2, HEIGHT / 2 + 18,
		subtitle, subtitle_scale);
	rectangle((WIDTH - 512) / 2, HEIGHT / 2 + 67, 512, 3,
		50, 160, 220);
	refresh();
}

static void draw_cell(int col, int row)
{
	const uint8_t *glyph = font_8x16 + (uint32_t)cells[row][col] * 16;
	int base_x = col * CELL_W;
	int base_y = row * CELL_H;
	int x, y, sx, sy;

	for (y = 0; y < 16; y++) {
		for (x = 0; x < 8; x++) {
			int on = glyph[y] & (0x80 >> x);
			uint8_t v = on ? 0xff : 0;
			for (sy = 0; sy < text_scale; sy++)
				for (sx = 0; sx < text_scale; sx++)
					pixel(base_x + x * text_scale + sx,
						base_y + y * text_scale + sy, v, v, v);
		}
	}
}

static int refresh(void)
{
	return ioctl(fbfd, FBIOPAN_DISPLAY, &var_screeninfo);
}

static int row_key_count(int row)
{
	static const int counts[] = { 10, 10, 9, 9, 6 };
	return counts[row];
}

static const char *key_label(int row, int col)
{
	static const char *normal[][10] = {
		{ "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
		{ "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
		{ "a", "s", "d", "f", "g", "h", "j", "k", "l", NULL },
		{ "SHF", "z", "x", "c", "v", "b", "n", "m", "BSP" },
		{ "CTRL", "SYM", "A-", "A+", "SPACE", "ENTER", NULL, NULL, NULL, NULL },
	};
	static const char *shifted[][10] = {
		{ "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
		{ "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" },
		{ "A", "S", "D", "F", "G", "H", "J", "K", "L", NULL },
		{ "SHF", "Z", "X", "C", "V", "B", "N", "M", "BSP" },
		{ "CTRL", "SYM", "A-", "A+", "SPACE", "ENTER", NULL, NULL, NULL, NULL },
	};
	static const char *symbols[][10] = {
		{ "!", "@", "#", "$", "%", "^", "&", "*", "(", ")" },
		{ "-", "_", "=", "+", "[", "]", "{", "}", "\\", "|" },
		{ ";", ":", "'", "\"", ",", ".", "/", "?", "`", NULL },
		{ "ABC", "<", ">", "~", "/", "\\", "|", "_", "BSP" },
		{ "CTRL", "ABC", "A-", "A+", "SPACE", "ENTER", NULL, NULL, NULL, NULL },
	};
	if (row == 4 && col == 0)
		return ctrl_armed ? "CTRL*" : "CTRL";
	if (symbols_mode)
		return symbols[row][col];
	return shift_locked ? shifted[row][col] : normal[row][col];
}

static void key_geometry(int row, int col, int *x, int *width)
{
	int count = row_key_count(row);
	if (row == 4) {
		static const int widths[] = { 96, 96, 96, 96, 240, 96 };
		static const int starts[] = { 0, 96, 192, 288, 384, 624 };
		*x = starts[col];
		*width = widths[col];
		return;
	}
	*width = WIDTH / count;
	*x = col * *width;
	if (row == 2)
		*x += (WIDTH - count * *width) / 2;
}

static void draw_keyboard(void)
{
	int row, col, x, width, y;
	for (row = 0; row < KEYBOARD_ROWS; row++) {
		y = ROWS * CELL_H + row * KEY_HEIGHT;
		for (col = 0; col < row_key_count(row); col++) {
			key_geometry(row, col, &x, &width);
			rectangle(x, y, width, KEY_HEIGHT, 24, 24, 24);
			rectangle(x, y, width, 2, 90, 130, 190);
			rectangle(x, y, 2, KEY_HEIGHT, 90, 130, 190);
			rectangle(x + width - 2, y, 2, KEY_HEIGHT,
				90, 130, 190);
			rectangle(x, y + KEY_HEIGHT - 2, width, 2,
				90, 130, 190);
			label_at(x, y, width, key_label(row, col));
		}
	}
}

static void redraw(void)
{
	int row, col;

	for (row = 0; row < ROWS; row++)
		for (col = 0; col < COLS; col++)
			draw_cell(col, row);
	draw_keyboard();
}

static void clear_screen(void)
{
	memset(cells, ' ', sizeof(cells));
	cursor_row = cursor_col = 0;
	redraw();
}

static void scroll(void)
{
	int rows = text_rows();
	int cols = COLS;
	for (int row = 0; row + 1 < rows; row++)
		memcpy(cells[row], cells[row + 1], (size_t)cols);
	memset(cells[rows - 1], ' ', (size_t)cols);
	redraw();
}

static void newline(void)
{
	cursor_col = 0;
	if (++cursor_row >= ROWS) {
		cursor_row = ROWS - 1;
		scroll();
	}
}

static void put_character(unsigned char c)
{
	if (c == '\r') {
		cursor_col = 0;
		return;
	}
	if (c == '\n') {
		newline();
		return;
	}
	if (c == '\b') {
		if (cursor_col) {
			cursor_col--;
			cells[cursor_row][cursor_col] = ' ';
			draw_cell(cursor_col, cursor_row);
		}
		return;
	}
	if (c == '\t') {
		cursor_col = (cursor_col + 8) & ~7;
		if (cursor_col >= COLS)
			newline();
		return;
	}
	if (c < 32 || c > 126)
		return;

	cells[cursor_row][cursor_col] = c;
	draw_cell(cursor_col, cursor_row);
	if (++cursor_col >= COLS)
		newline();
}

static int parse_number(const char **p)
{
	int value = 0;
	while (**p >= '0' && **p <= '9') {
		value = value * 10 + (**p - '0');
		(*p)++;
	}
	return value;
}

static void put_escape(const unsigned char **p, const unsigned char *end)
{
	const char *s = (const char *)(*p + 1);
	const char *parameters;

	if (*s != '[') {
		*p = (const unsigned char *)s + 2;
		return;
	}
	parameters = ++s;
	while (s < (const char *)end && !(*s >= '@' && *s <= '~'))
		s++;
	if (s >= (const char *)end)
		return;

	if (*s == 'H' || *s == 'f') {
		const char *n = parameters;
		int row = parse_number(&n);
		int col = 1;
		if (*n == ';') {
			n++;
			col = parse_number(&n);
		}
		cursor_row = row > 0 ? row - 1 : 0;
		cursor_col = col > 0 ? col - 1 : 0;
		if (cursor_row >= ROWS)
			cursor_row = ROWS - 1;
		if (cursor_col >= COLS)
			cursor_col = COLS - 1;
	} else if (*s == 'J') {
		if (parse_number(&parameters) >= 2)
			clear_screen();
	} else if (*s == 'K') {
		int col;
		for (col = cursor_col; col < COLS; col++)
			cells[cursor_row][col] = ' ';
		redraw();
	} else if (*s == 'A' && cursor_row) {
		const int rows = parse_number(&parameters);
		cursor_row -= rows > 0 ? rows : 1;
		if (cursor_row >= ROWS)
			cursor_row = 0;
	} else if (*s == 'B' && cursor_row + 1 < ROWS) {
		const int rows = parse_number(&parameters);
		cursor_row += rows > 0 ? rows : 1;
		if (cursor_row >= ROWS)
			cursor_row = ROWS - 1;
	} else if (*s == 'C') {
		const int columns = parse_number(&parameters);
		cursor_col += columns > 0 ? columns : 1;
		if (cursor_col >= COLS)
			cursor_col = COLS - 1;
	} else if (*s == 'D') {
		const int columns = parse_number(&parameters);
		cursor_col -= columns > 0 ? columns : 1;
		if (cursor_col > COLS)
			cursor_col = 0;
	}
	*p = (const unsigned char *)(s + 1);
}

static void render(const unsigned char *data, size_t length)
{
	const unsigned char *p = data;
	const unsigned char *end = data + length;

	while (p < end) {
		if (*p == 0x1b && p + 1 < end && (p[1] == '[' || p[1] == '(')) {
			put_escape(&p, end);
			continue;
		}
		put_character(*p++);
	}
}

static void send_key(const char *label)
{
	char value;
	if (!strcmp(label, "A-")) {
		if (text_scale > SCALE_MIN) {
			text_scale--;
			cursor_row = cursor_col = 0;
			clear_screen();
			struct winsize size = {
				.ws_row = ROWS,
				.ws_col = COLS,
			};
			if (master >= 0)
				ioctl(master, TIOCSWINSZ, &size);
			draw_keyboard();
			refresh();
		}
		return;
	}
	if (!strcmp(label, "A+")) {
		if (text_scale < SCALE_MAX) {
			text_scale++;
			cursor_row = cursor_col = 0;
			clear_screen();
			struct winsize size = {
				.ws_row = ROWS,
				.ws_col = COLS,
			};
			if (master >= 0)
				ioctl(master, TIOCSWINSZ, &size);
			draw_keyboard();
			refresh();
		}
		return;
	}
	if (!strcmp(label, "CTRL")) {
		ctrl_armed = !ctrl_armed;
		draw_keyboard();
		refresh();
		return;
	}
	if (!strcmp(label, "SYM") || !strcmp(label, "ABC")) {
		symbols_mode = !symbols_mode;
		shift_locked = 0;
		draw_keyboard();
		refresh();
		return;
	}
	if (!strcmp(label, "SHF")) {
		shift_locked = !shift_locked;
		draw_keyboard();
		refresh();
		return;
	}
	if (!strcmp(label, "BSP"))
		value = 0x7f;
	else if (!strcmp(label, "ENTER"))
		value = '\n';
	else if (!strcmp(label, "SPACE"))
		value = ' ';
	else
		value = label[0];
	if (ctrl_armed && (value >= 'a' && value <= 'z'))
		value = value - 'a' + 1;
	else if (ctrl_armed && (value >= 'A' && value <= 'Z'))
		value = value - 'A' + 1;
	write(master, &value, 1);
	if (shift_locked || ctrl_armed) {
		ctrl_armed = 0;
		shift_locked = 0;
		draw_keyboard();
		refresh();
	}
}

static void touch_release(void)
{
	int row, col, x, width;
	int keyboard_y = ROWS * CELL_H;
	int event_x = touch_x;
	int event_y = touch_y;

	if (!touch_active || touch_handled || event_y < keyboard_y)
		return;
	touch_handled = 1;
	event_y -= keyboard_y;
	row = event_y / KEY_HEIGHT;
	if (row < 0 || row >= KEYBOARD_ROWS)
		return;
	for (col = 0; col < row_key_count(row); col++) {
		key_geometry(row, col, &x, &width);
		if (event_x >= x && event_x < x + width) {
			send_key(key_label(row, col));
			break;
		}
	}
}

static void touch_event(const struct arm_input_event *event)
{
	if (!display_awake)
		return;
	if (event->type == EV_ABS) {
		switch (event->code) {
		case ABS_MT_SLOT:
			touch_slot = event->value;
			break;
		case ABS_MT_TRACKING_ID:
			if (touch_slot != 0)
				break;
			if (event->value >= 0) {
				touch_active = 1;
				touch_handled = 0;
			} else {
				touch_release();
				touch_active = 0;
			}
			break;
		case ABS_MT_POSITION_X:
			if (touch_slot != 0)
				break;
			touch_x = (event->value - touch_x_min) * WIDTH /
				(touch_x_max - touch_x_min);
			break;
		case ABS_MT_POSITION_Y:
			if (touch_slot != 0)
				break;
			touch_y = (event->value - touch_y_min) * HEIGHT /
				(touch_y_max - touch_y_min);
			break;
		}
	} else if (event->type == EV_KEY && event->code == BTN_TOUCH) {
		if (event->value) {
			touch_active = 1;
			touch_handled = 0;
		} else {
			touch_release();
			touch_active = 0;
		}
	}
}

static int open_touchscreen(void)
{
	struct input_absinfo axis;
	char path[32], sysfs_path[64], name[96];
	int event, namefd, count;
	unsigned long evio_read = 2UL << 30;

	touchfd = -1;
	for (event = 0; event < 32; event++) {
		snprintf(sysfs_path, sizeof(sysfs_path),
			"/sys/class/input/event%d/device/name", event);
		namefd = open(sysfs_path, O_RDONLY);
		if (namefd < 0)
			continue;
		memset(name, 0, sizeof(name));
		count = read(namefd, name, sizeof(name) - 1);
		close(namefd);
		if (count <= 0 || strstr(name, "sec_touchscreen") == NULL)
			continue;
		snprintf(path, sizeof(path), "/dev/input/event%d", event);
		touchfd = open(path, O_RDONLY | O_NONBLOCK);
		break;
	}
	if (touchfd < 0)
		touchfd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
	if (touchfd < 0)
		return -1;
	evio_read |= (unsigned long)sizeof(axis) << 16;
	evio_read |= 'E' << 8;
	if (ioctl(touchfd, evio_read | (0x40 + ABS_MT_POSITION_X), &axis) == 0) {
		touch_x_min = axis.minimum;
		touch_x_max = axis.maximum;
	}
	if (ioctl(touchfd, evio_read | (0x40 + ABS_MT_POSITION_Y), &axis) == 0) {
		touch_y_min = axis.minimum;
		touch_y_max = axis.maximum;
	}
	if (touch_x_max <= touch_x_min || touch_y_max <= touch_y_min) {
		close(touchfd);
		touchfd = -1;
		return -1;
	}
	return 0;
}

static void close_touchscreen(void)
{
	if (touchfd >= 0)
		close(touchfd);
	touchfd = -1;
	touch_active = 0;
	touch_handled = 0;
	touch_slot = 0;
}

static int input_supports_key(int fd, int code)
{
	unsigned char bits[96] = { 0 };
	unsigned long request = 2UL << 30;

	request |= (unsigned long)sizeof(bits) << 16;
	request |= 'E' << 8;
	request |= 0x20 + EV_KEY;
	return code >= 0 && (size_t)code / 8 < sizeof(bits) &&
		ioctl(fd, request, bits) == 0 &&
		(bits[code / 8] & (1U << (code % 8))) != 0;
}

static int open_power_button(void)
{
	char path[32];
	int event, fd;
	int fallback = -1;

	for (event = 0; event < 32; event++) {
		char name[96] = { 0 };
		char sysfs_path[64];
		int count, namefd;
		int named_gpio_keys = 0;

		snprintf(path, sizeof(path), "/dev/input/event%d", event);
		snprintf(sysfs_path, sizeof(sysfs_path),
			"/sys/class/input/event%d/device/name", event);
		namefd = open(sysfs_path, O_RDONLY);
		if (namefd >= 0) {
			count = read(namefd, name, sizeof(name) - 1);
			close(namefd);
			named_gpio_keys =
				count > 0 && (strstr(name, "gpio_keys") != NULL ||
				strstr(name, "gpio-keys") != NULL);
		}
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		if (named_gpio_keys) {
			return fd;
		}
		if (!input_supports_key(fd, KEY_POWER)) {
			if (fd >= 0)
				close(fd);
			continue;
		}
		if (fallback < 0)
			fallback = fd;
		else
			close(fd);

	}
	return fallback;
}

static int write_sysfs(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY);
	ssize_t count;

	if (fd < 0) {
		trace("sysfs-open-failed\n");
		return -1;
	}
	count = write(fd, value, strlen(value));
	close(fd);
	if (count != (ssize_t)strlen(value)) {
		trace("sysfs-write-failed\n");
		return -1;
	}
	return 0;
}

static void sleep_display(void)
{
	display_awake = 0;
	close_touchscreen();
	write_sysfs(
		"/sys/devices/14800000.dsim/backlight/panel/brightness", "0\n");
	write_sysfs("/sys/class/graphics/fb0/blank", "4\n");
	trace("display-sleep\n");
}

static int wake_display(void)
{
	if (write_sysfs("/sys/class/graphics/fb0/blank", "4\n") < 0)
		return -1;
	sleep(1);
	if (write_sysfs("/sys/class/graphics/fb0/blank", "0\n") < 0)
		return -1;
	if (write_sysfs(
		"/sys/devices/14800000.dsim/backlight/panel/brightness",
		"255\n") < 0)
		return -1;

	display_awake = 1;
	redraw();
	if (refresh() < 0)
		return -1;
	open_touchscreen();
	trace("display-wake\n");
	return 0;
}

static void power_event(const struct arm_input_event *event)
{
	if (event->type != EV_KEY || event->code != KEY_POWER ||
	    event->value != 1)
		return;
	if (display_awake)
		sleep_display();
	else
		wake_display();
}

static void draw_centered_text(int y, const char *text, int scale)
{
	int width = strlen(text) * 8 * scale;
	text_at((WIDTH - width) / 2, y, text, scale);
}

static void draw_boot_menu(int seconds)
{
	char countdown[32];
	rectangle(0, 0, WIDTH, HEIGHT, 4, 8, 14);
	draw_centered_text(120, "ZIRARCH BOOT", 3);
	rectangle(80, 300, WIDTH - 160, 300, 18, 54, 74);
	draw_centered_text(350, "VOL DOWN: TTY", 3);
	draw_centered_text(430, "Buffyboard + console", 2);
	rectangle(80, 700, WIDTH - 160, 300, 28, 78, 48);
	draw_centered_text(750, "VOL UP: GUI", 3);
	draw_centered_text(830, "FieldM + on-screen keyboard", 2);
	snprintf(countdown, sizeof(countdown), "TTY starts in %d", seconds);
	draw_centered_text(1110, countdown, 2);
	refresh();
}

static int boot_menu(void)
{
	struct arm_input_event event;
	struct timeval now, deadline, last_draw;
	int elapsed, selection = 0, touch_down = 0, touch_y = 0;

	gettimeofday(&now, NULL);
	deadline = now;
	deadline.tv_sec += 5;
	last_draw = now;
	last_draw.tv_sec -= 1;
	draw_boot_menu(5);

	for (;;) {
		struct pollfd inputs[2];
		int ready, milliseconds, value;

		gettimeofday(&now, NULL);
		elapsed = deadline.tv_sec - now.tv_sec;
		if (elapsed <= 0) {
			return selection;
		}
		if (now.tv_sec != last_draw.tv_sec) {
			draw_boot_menu(elapsed);
			last_draw = now;
		}
		milliseconds = (deadline.tv_sec - now.tv_sec) * 1000;
		milliseconds += (deadline.tv_usec - now.tv_usec) / 1000;
		if (milliseconds < 0)
			milliseconds = 0;
		if (milliseconds > 200)
			milliseconds = 200;
		inputs[0].fd = powerfd;
		inputs[0].events = powerfd >= 0 ? POLLIN : 0;
		inputs[1].fd = touchfd;
		inputs[1].events = touchfd >= 0 ? POLLIN : 0;
		ready = poll(inputs, 2, milliseconds);
		if (ready <= 0)
			continue;

		if (powerfd >= 0 && inputs[0].revents & POLLIN &&
		    read(powerfd, &event, sizeof(event)) == sizeof(event) &&
		    event.type == EV_KEY && event.value == 1) {
			if (event.code == KEY_VOLUMEUP)
				return 2;
			if (event.code == KEY_VOLUMEDOWN)
				return 0;
		}

		if (touchfd >= 0 && inputs[1].revents & POLLIN &&
		    read(touchfd, &event, sizeof(event)) == sizeof(event)) {
			if (event.type == EV_ABS && event.code == ABS_MT_POSITION_Y)
				touch_y = event.value;
			else if (event.type == EV_ABS &&
			    event.code == ABS_MT_TRACKING_ID) {
				value = event.value;
				if (value >= 0)
					touch_down = 1;
				else if (touch_down)
					return touch_y > 640 ? 2 : 0;
			}
		}
	}
}

static int openpty_unlocked(int *pty_master, int *pty_slave,
	const struct winsize *size)
{
	char slave_path[64];
	char master_path[64];
	char error[64];
	int lock = 0, number, i;
	*pty_master = open("/dev/ptmx", O_RDWR | O_NOCTTY);
	if (*pty_master < 0) {
		snprintf(error, sizeof(error), "ptmx-open:%d\n", errno);
		trace(error);
		return -1;
	}
	if (ioctl(*pty_master, TIOCSPTLCK, &lock) < 0 ||
	    ioctl(*pty_master, TIOCGPTN, &number) < 0) {
		snprintf(error, sizeof(error), "ptmx-ioctl:%d\n", errno);
		trace(error);
		close(*pty_master);
		return -1;
	}
	snprintf(slave_path, sizeof(slave_path), "/dev/pts/%d", number);
	*pty_slave = open(slave_path, O_RDWR | O_NOCTTY);
	if (*pty_slave < 0 && errno == ENOENT) {
		mknod(slave_path, S_IFCHR | 0600, (136 << 8) | number);
		*pty_slave = open(slave_path, O_RDWR | O_NOCTTY);
	}
	if (*pty_slave < 0) {
		snprintf(error, sizeof(error), "pty-slave:%d:%s\n", errno,
			slave_path);
		trace(error);
		close(*pty_master);
		unlink(slave_path);
	} else {
		ioctl(*pty_slave, TIOCSWINSZ, size);
		return 0;
	}

	for (i = 0; i < 16; i++) {
		snprintf(master_path, sizeof(master_path), "/dev/pty%d", i);
		snprintf(slave_path, sizeof(slave_path), "/dev/ttyp%d", i);
		if (access(master_path, F_OK) < 0)
			mknod(master_path, S_IFCHR | 0600, (2 << 8) | i);
		if (access(slave_path, F_OK) < 0)
			mknod(slave_path, S_IFCHR | 0600, (3 << 8) | i);
		*pty_master = open(master_path, O_RDWR | O_NOCTTY);
		if (*pty_master < 0)
			continue;
		*pty_slave = open(slave_path, O_RDWR | O_NOCTTY);
		if (*pty_slave >= 0) {
			ioctl(*pty_slave, TIOCSWINSZ, size);
			snprintf(error, sizeof(error), "legacy-pty:%d\n", i);
			trace(error);
			return 0;
		}
		close(*pty_master);
	}
	trace("legacy-pty-failed\n");
	return -1;
}

static int spawn_login(void)
{
	int slave;
	struct winsize size = {
		.ws_row = ROWS,
		.ws_col = COLS,
	};
	char columns[16], lines[16];
	pid_t pid;

	if (openpty_unlocked(&master, &slave, &size) < 0)
		return -1;
	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		setsid();
		ioctl(slave, TIOCSCTTY, 0);
		dup2(slave, STDIN_FILENO);
		dup2(slave, STDOUT_FILENO);
		dup2(slave, STDERR_FILENO);
		close(master);
		close(slave);
		setenv("TERM", "linux", 1);
		snprintf(columns, sizeof(columns), "%d", COLS);
		snprintf(lines, sizeof(lines), "%d", ROWS);
		setenv("COLUMNS", columns, 1);
		setenv("LINES", lines, 1);
		setenv("HOME", "/root", 1);
		setenv("USER", "root", 1);
		setenv("LOGNAME", "root", 1);
		chdir("/root");
		execl("/bin/sh", "sh", "-l", (char *)NULL);
		_exit(127);
	}
	close(slave);
	return 0;
}

static void enable_serial(int fd)
{
	struct termios tty;

	if (tcgetattr(fd, &tty) == 0) {
		cfmakeraw(&tty);
		tty.c_cflag |= CLOCAL | CREAD;
		cfsetispeed(&tty, B115200);
		cfsetospeed(&tty, B115200);
		tcflush(fd, TCIFLUSH);
		tcsetattr(fd, TCSANOW, &tty);
	}
}

int main(int argc, char **argv)
{
	unsigned char buffer[4096];
	struct pollfd pollfds[4];
	int serial, status;
	unsigned char fixed_screeninfo[80];

	trace("main\n");
	fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd < 0)
		return 1;
	trace("fb-open\n");
	if (ioctl(fbfd, FBIOGET_VSCREENINFO, var_screeninfo) < 0 ||
	    ioctl(fbfd, FBIOGET_FSCREENINFO, fixed_screeninfo) < 0)
		return 4;
	memcpy(&stride, fixed_screeninfo + 44, sizeof(uint32_t));
	if (!stride || stride < WIDTH * BPP)
		return 5;
	framebuffer = mmap(NULL, stride * HEIGHT, PROT_READ | PROT_WRITE,
		MAP_SHARED, fbfd, 0);
	if (framebuffer == MAP_FAILED)
		return 2;
	trace("mapped\n");
	if (argc > 1 && strcmp(argv[1], "--boot-menu") == 0) {
		open_touchscreen();
		powerfd = open_power_button();
		return boot_menu();
	}
	clear_screen();
	refresh();
	trace("display-open\n");
	draw_boot_splash();
	sleep(1);
	clear_screen();
	refresh();
	trace("console-ready\n");
	if (argc > 1 && strcmp(argv[1], "--smoke") == 0) {
		const char *message = "ZIRARCH FRAMEBUFFER CONSOLE OK";
		while (*message)
			put_character(*message++);
		return refresh() < 0 ? 6 : 0;
	}

	serial = open("/dev/ttyGS0", O_RDWR | O_NOCTTY);
	trace("serial-open\n");
	if (serial >= 0)
		enable_serial(serial);
	if (spawn_login() < 0) {
		trace("pty-open-failed\n");
		return 3;
	}
	trace("login-started\n");
	open_touchscreen();
	trace("touch-open\n");
	powerfd = open_power_button();
	trace(powerfd >= 0 ? "power-open\n" : "power-open-failed\n");

	pollfds[0].fd = master;
	pollfds[0].events = POLLIN;
	pollfds[1].fd = serial;
	pollfds[1].events = serial >= 0 ? POLLIN : 0;
	pollfds[2].fd = touchfd;
	pollfds[2].events = touchfd >= 0 ? POLLIN : 0;
	pollfds[3].fd = powerfd;
	pollfds[3].events = powerfd >= 0 ? POLLIN : 0;
	for (;;) {
		ssize_t count;
		pollfds[2].fd = touchfd;
		pollfds[2].events = touchfd >= 0 ? POLLIN : 0;
		if (poll(pollfds, 4, 1000) <= 0) {
			if (waitpid(-1, &status, WNOHANG) > 0)
				spawn_login();
			continue;
		}
		if (pollfds[0].revents & POLLIN) {
			count = read(master, buffer, sizeof(buffer));
			if (count > 0) {
				render(buffer, count);
				refresh();
				if (serial >= 0)
					write(serial, buffer, count);
			}
		}
		if (serial >= 0 && pollfds[1].revents & POLLIN) {
			count = read(serial, buffer, sizeof(buffer));
			if (count > 0)
				write(master, buffer, count);
		}
		if (touchfd >= 0 && pollfds[2].revents & POLLIN) {
			int offset = 0;
			count = read(touchfd, buffer, sizeof(buffer));
			while (count - offset >=
				(ssize_t)sizeof(struct arm_input_event)) {
				touch_event((struct arm_input_event *)(buffer + offset));
				offset += sizeof(struct arm_input_event);
			}
		}
		if (powerfd >= 0 && pollfds[3].revents & POLLIN) {
			int offset = 0;
			count = read(powerfd, buffer, sizeof(buffer));
			while (count - offset >=
				(ssize_t)sizeof(struct arm_input_event)) {
				power_event((struct arm_input_event *)(buffer + offset));
				offset += sizeof(struct arm_input_event);
			}
		}
	}
}
