// Bleed Kernel frontend for ptetris
// draws into a backbuffer that gets flipped to /dev/fb0, input is raw key events off stdin

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>

#include <syscalls.h>
#include <keyboard.h>
#include <display.h>
#include <console.h>
#include <hpet.h>

#define FIELD_WIDTH     10
#define FIELD_HEIGHT    20
#define MAX_BLOCK_SIZE  40

#define COL_BLACK       0x000000
#define COL_WHITE       0xffffff
#define COL_GRAY        0x808080

uint32_t pt_colors[] = {
	0x000000,
	0x00ffff,
	0xff00ff,
	0xff8000,
	0xffff00,
	0x00ff00,
	0xff0000,
	0x0000ff,
};

#define PT_CUSTOM_DRAW_BLOCK
void pt_draw_block(int bx, int by, int col);

#include "ptetris.h"

enum {
	STATE_PLAYING,
	STATE_PAUSED,
	STATE_GAME_OVER,
};

static uint32_t *fb;
static struct fb_info fb_info;
static uint32_t fb_pitch_pixels;
static int fb_fd = -1;
static int hpet_fd = -2;

// top left of the playfield on screen
static int field_x;
static int field_y;
static int text_scale = 1;

static int state = STATE_PLAYING;
static int score;
static int level = 1;
static uint64_t last_step_ms;
static volatile int quit;

static uint32_t old_kbd_flags;
static uint32_t old_tty_flags;
static int have_old_kbd_flags;
static int have_old_tty_flags;

// font8x8_basic
// https://github.com/dhepper/font8x8/blob/master/font8x8_basic.h
static const uint8_t font_digits[10][8] = {
	{0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},
	{0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},
	{0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},
	{0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},
	{0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},
	{0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},
	{0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},
	{0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},
	{0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},
	{0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},
};

static const uint8_t font_letters[26][8] = {
	{0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},
	{0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},
	{0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},
	{0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},
	{0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},
	{0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},
	{0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},
	{0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},
	{0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
	{0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},
	{0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},
	{0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},
	{0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},
	{0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},
	{0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},
	{0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},
	{0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},
	{0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},
	{0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},
	{0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
	{0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},
	{0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
	{0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
	{0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},
	{0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},
	{0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},
};

static uint64_t now_fs(void) {
	uint64_t now = 0;

	if (hpet_fd == -2)
		hpet_fd = _open("/dev/hpet", O_RDONLY);
	if (hpet_fd < 0)
		return 0;

	if (_ioctl(hpet_fd, HPET_IOCTL_GET_FEMTOSECONDS, &now) == 0)
		return now;
	if (_read(hpet_fd, &now, sizeof(now)) == (long)sizeof(now))
		return now;
	return 0;
}

static uint64_t now_ms(void) {
	static uint64_t fallback_ms = 0;
	uint64_t now = now_fs();

	// no hpet, count loop iterations so the game still moves
	if (!now)
		return fallback_ms++;
	return now / femtosecondsPerMillisecond;
}

static void fill_rect(int x, int y, int w, int h, uint32_t col) {
	int x1 = x + w;
	int y1 = y + h;

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x1 > (int)fb_info.width) x1 = (int)fb_info.width;
	if (y1 > (int)fb_info.height) y1 = (int)fb_info.height;

	for (int py = y; py < y1; py++) {
		uint32_t *row = &fb[(uint32_t)py * fb_pitch_pixels];
		for (int px = x; px < x1; px++)
			row[px] = col;
	}
}

void pt_draw_block(int bx, int by, int col) {
	int size = pt.block_size;
	int x = field_x + bx * size;
	int y = field_y + by * size;

	// 1px gap so neighbouring blocks stay distinct
	fill_rect(x, y, size, size, COL_BLACK);
	if (col != 0)
		fill_rect(x + 1, y + 1, size - 2, size - 2, pt_colors[col]);
}

static const uint8_t *glyph_for_char(char ch) {
	if (ch >= '0' && ch <= '9') return font_digits[ch - '0'];
	if (ch >= 'a' && ch <= 'z') return font_letters[ch - 'a'];
	if (ch >= 'A' && ch <= 'Z') return font_letters[ch - 'A'];
	return NULL;
}

static void draw_text(int x, int y, const char *text, uint32_t col, int scale) {
	for (; *text; text++, x += 8 * scale) {
		const uint8_t *glyph = glyph_for_char(*text);
		if (!glyph) continue;

		for (int i = 0; i < 8; i++) {
			for (int j = 0; j < 8; j++) {
				if (glyph[i] & (1 << j))
					fill_rect(x + j * scale, y + i * scale, scale, scale, col);
			}
		}
	}
}

static int text_width(const char *text, int scale) {
	return (int)strlen(text) * 8 * scale;
}

static void draw_stat(int x, int *y, const char *label, int value) {
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "%d", value);

	draw_text(x, *y, label, COL_GRAY, text_scale);
	*y += 10 * text_scale;
	draw_text(x, *y, buffer, COL_GRAY, text_scale);
	*y += 14 * text_scale;
}

static void draw_banner(const char *title, const char *hint) {
	int field_w = pt.width * pt.block_size;
	int field_h = pt.height * pt.block_size;
	int box_h = 36 * text_scale;
	int box_y = field_y + (field_h - box_h) / 2;

	// the hint is the widest line, drop to the small font if it would overflow the field
	int scale = text_scale;
	if (text_width(hint, scale) > field_w)
		scale = 1;

	fill_rect(field_x, box_y, field_w, box_h, COL_WHITE);
	fill_rect(field_x, box_y + 1, field_w, box_h - 2, COL_BLACK);

	draw_text(field_x + (field_w - text_width(title, scale)) / 2,
		box_y + 8 * text_scale, title, COL_WHITE, scale);
	draw_text(field_x + (field_w - text_width(hint, scale)) / 2,
		box_y + 20 * text_scale, hint, COL_WHITE, scale);
}

static void draw(void) {
	int field_w = pt.width * pt.block_size;
	int field_h = pt.height * pt.block_size;

	fill_rect(0, 0, (int)fb_info.width, (int)fb_info.height, COL_BLACK);
	fill_rect(field_x - 2, field_y - 2, field_w + 4, field_h + 4, COL_WHITE);

	pt_render();

	int x = field_x + field_w + pt.block_size;
	int y = field_y;

	draw_text(x, y, "TETRIS", COL_WHITE, text_scale);
	y += 20 * text_scale;

	draw_stat(x, &y, "SCORE", score);
	draw_stat(x, &y, "LINES", pt.score);
	draw_stat(x, &y, "LEVEL", level);

	static const char *help[] = {
		"ARROWS  MOVE",
		"UP      ROTATE",
		"DOWN    SOFT DROP",
		"SPACE   HARD DROP",
		"P       PAUSE",
		"R       RESTART",
		"Q       QUIT",
	};

	y += 8 * text_scale;
	for (size_t i = 0; i < sizeof(help) / sizeof(help[0]); i++) {
		draw_text(x, y, help[i], COL_WHITE, text_scale);
		y += 12 * text_scale;
	}

	if (state == STATE_PAUSED)
		draw_banner("PAUSED", "P TO RESUME");
	else if (state == STATE_GAME_OVER)
		draw_banner("GAME OVER", "R RETRY  Q QUIT");

	_ioctl(fb_fd, FB_IOC_FLIP, fb);
}

static uint64_t step_interval_ms(void) {
	int interval = 550 - (level - 1) * 45;
	if (interval < 70) interval = 70;
	return (uint64_t)interval;
}

static void new_game(void) {
	memset(&pt_main_field, 0, sizeof(pt_main_field));
	pt.score = 0;
	score = 0;
	level = 1;

	pt_blocks.curr = PT_RAND(BLOCK_LEN);
	pt_blocks.x = 4;
	pt_blocks.y = 0;

	state = STATE_PLAYING;
	last_step_ms = now_ms();
}

// one gravity tick, also where pieces lock and lines get scored
static void step(void) {
	static const int line_points[] = {0, 100, 300, 500, 800};
	int lines_before = pt.score;

	if (pt_step() != PT_NORMAL) {
		state = STATE_GAME_OVER;
		return;
	}

	int cleared = pt.score - lines_before;
	if (cleared > 4) cleared = 4;
	score += line_points[cleared] * level;
	level = 1 + pt.score / 10;
	last_step_ms = now_ms();
}

// returns 1 when the screen needs a redraw
static int handle_key(const keyboard_event_t *ev) {
	if (ev->action == KEY_RELEASE)
		return 0;

	// mess with random number
	pt_rand_x += ev->keycode + (uint32_t)now_ms();

	char c = tty_key_to_ascii(ev);
	if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));

	if (c == 'q' || c == 27) {
		quit = 1;
		return 0;
	}

	if (c == 'r') {
		new_game();
		return 1;
	}

	if (state == STATE_GAME_OVER)
		return 0;

	if (c == 'p') {
		state = (state == STATE_PAUSED) ? STATE_PLAYING : STATE_PAUSED;
		last_step_ms = now_ms();
		return 1;
	}

	if (state == STATE_PAUSED)
		return 0;

	// keycodes are scancodes here, the arrows have no ascii
	switch (ev->keycode) {
	case ArrowLeft:
		pt_handle_input(PT_LEFT);
		return 1;
	case ArrowRight:
		pt_handle_input(PT_RIGHT);
		return 1;
	case ArrowUp:
		pt_handle_input(PT_ROT);
		return 1;
	case ArrowDown:
		score += 1;
		step();
		return 1;
	}

	if (c == ' ') {
		int start_y = pt_blocks.y;
		pt_handle_input(PT_DOWN);
		score += (pt_blocks.y - start_y) * 2;
		step();
		return 1;
	}

	return 0;
}

static void sigint_handler(int sig) {
	(void)sig;
	quit = 1;
}

static void input_init(void) {
	uint32_t flags;

	// stdin is the keyboard device, shared with the shell so put it back on the way out
	if (_ioctl(0, TTY_IOCTL_GET_FLAGS, &flags) == 0) {
		old_kbd_flags = flags;
		have_old_kbd_flags = 1;
	}
	flags = TTY_NONBLOCK;
	_ioctl(0, TTY_IOCTL_SET_FLAGS, &flags);

	// stop the tty echoing keys over the top of the game
	if (_ioctl(1, TTY_IOCTL_GET_FLAGS, &flags) == 0) {
		old_tty_flags = flags;
		have_old_tty_flags = 1;
		flags &= ~(TTY_ECHO | TTY_CANNONICAL);
		_ioctl(1, TTY_IOCTL_SET_FLAGS, &flags);
	}

	struct sigaction act = {0};
	act.handler = (uintptr_t)sigint_handler;
	act.flags = SA_RESTART;
	if (sigemptyset(&act.mask) == 0)
		sigaction(SIGINT, &act, NULL);
}

static void input_restore(void) {
	if (have_old_kbd_flags)
		_ioctl(0, TTY_IOCTL_SET_FLAGS, &old_kbd_flags);
	if (have_old_tty_flags)
		_ioctl(1, TTY_IOCTL_SET_FLAGS, &old_tty_flags);
}

static int video_init(void) {
	fb_fd = _open("/dev/fb0", O_RDWR);
	if (fb_fd < 0) {
		printf("tetris: cannot open /dev/fb0\n");
		return -1;
	}

	if (_ioctl(fb_fd, FB_IOC_GET_INFO, &fb_info) < 0) {
		printf("tetris: cannot read framebuffer info\n");
		return -1;
	}

	if (fb_info.bpp != 32) {
		printf("tetris: unsupported framebuffer depth (%d bpp)\n", (int)fb_info.bpp);
		return -1;
	}

	fb = (uint32_t *)malloc(fb_info.height * fb_info.pitch);
	if (!fb) {
		printf("tetris: out of memory for the backbuffer\n");
		return -1;
	}
	fb_pitch_pixels = (uint32_t)(fb_info.pitch >> 2);

	pt.width = FIELD_WIDTH;
	pt.height = FIELD_HEIGHT;
	pt.block_size = ((int)fb_info.height - 32) / FIELD_HEIGHT;
	if (pt.block_size > MAX_BLOCK_SIZE) pt.block_size = MAX_BLOCK_SIZE;
	if (pt.block_size < 4) pt.block_size = 4;

	text_scale = (pt.block_size >= 24) ? 2 : 1;
	field_x = ((int)fb_info.width - pt.width * pt.block_size) / 2;
	field_y = ((int)fb_info.height - pt.height * pt.block_size) / 2;
	return 0;
}

int main(void) {
	if (video_init() < 0)
		return 1;

	input_init();

	pt_rand_x += (uint32_t)(now_fs() / femtosecondsPerMicrosecond) + (uint32_t)_getpid();
	new_game();
	draw();

	while (!quit) {
		keyboard_event_t ev;
		int dirty = 0;

		while (_read(0, &ev, sizeof(ev)) == (long)sizeof(ev))
			dirty |= handle_key(&ev);

		if (state == STATE_PLAYING && now_ms() - last_step_ms >= step_interval_ms()) {
			step();
			dirty = 1;
		}

		if (dirty)
			draw();

		_yeild();
	}

	input_restore();

	// hand the screen back to the tty
	printf("\x1b[0m\x1b[2J");
	printf("tetris: score %d, %d lines, level %d\n", score, pt.score, level);
	fflush(stdout);
	return 0;
}
