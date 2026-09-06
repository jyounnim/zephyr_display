/*
 * Lab 06: ST7789V3 1.69" 240x280 color TFT, raw SPI, no Zephyr
 * Display/CFB subsystem.
 *
 * Board: Synaptics SR110 (sr100_rdk/sr100/m55)
 * Bus:   SPI0 (only SPI *master* on this SoC - spi1 is slave-only),
 *        native hardware CS (spi_mstr_cs pinctrl, no cs-gpios) - the
 *        same CS mechanism confirmed working on real hardware by
 *        Lab 05 (SSD1306 SPI, using Zephyr's own in-tree display
 *        driver). See the devicetree overlay for the UART console
 *        conflict this creates (SPI0 shares pads with UART0/UART1's
 *        default pins on this board) and how it's worked around.
 *
 * This lab talks to the ST7789V3 controller directly with raw SPI
 * writes (command/data selected via the DC pin), the same style used
 * throughout this series for SPI displays, rather than going through
 * Zephyr's built-in "sitronix,st7789v" display driver.
 *
 * SR110 PORTING HISTORY: an earlier version of this file chased a
 * blank-screen symptom through several dead ends - software
 * (GPIO-based) CS, manually holding CS low across an entire RAMWR
 * burst, forcing spi_transceive_dt() (TX+RX) instead of spi_write_dt()
 * (TX-only) - none of which were the actual problem. Lab 05 succeeding
 * with plain native CS and ordinary per-call SPI writes (via Zephyr's
 * own SSD1306 driver) on the exact same SPI0 bus showed that none of
 * that machinery was necessary. This version drops all of it and
 * reuses Lab 05's proven-working RST/DC pins (GPIO17/18) instead of
 * this lab's earlier, still-unverified GPIO27/28/29/4 choices.
 *
 * The one genuinely hardware-confirmed fix that remains: SR110's SPI0
 * hardware FIFO is only 8 bytes deep (`fifo-depth = <8>;` in
 * sr100_m55.dtsi), and a single spi_write_dt() call needing a
 * mid-transfer TX-FIFO-refill interrupt to complete (i.e. any single
 * call over 8 bytes) reproducibly times out (-ETIMEDOUT / errno 116)
 * instead of that refill interrupt ever firing. Every send below is
 * chunked to <= 8 bytes to avoid ever needing that refill.
 *
 * PANEL RAM OFFSET: the ST7789 controller's native GRAM is 240x320.
 * This particular module's visible glass is only 240x280, centered
 * in a sub-window of that GRAM - so every addressing window sent to
 * the controller needs a fixed offset added (X_OFFSET/Y_OFFSET below,
 * also mirrored in the devicetree overlay). 20 is the commonly
 * documented Y offset for 1.69" 240x280 ST7789V3 modules; if the
 * image on your specific module is shifted or clipped, check your
 * module's own datasheet/example code for its offset and update the
 * overlay's x-offset/y-offset properties.
 *
 * COLOR ORDER / ORIENTATION: MADCTL below is set to a common default.
 * If colors come out with red/blue swapped, or the image is mirrored/
 * rotated relative to how your module is mounted, see the
 * troubleshooting doc for the MADCTL bits to flip.
 */

/*
 * SR110 DEBUG STEP (2026-09-03): comprehensive init sequence (matching
 * Lab 08's approach) still didn't produce a visible image - screen
 * stays black, log completes with no error, same as every previous
 * attempt. Since SPI0/CS/pins/chunking are all independently proven
 * working (Lab 05, Lab 08), the next untested hypothesis is DC
 * polarity: if this specific module's DC line is wired/expects the
 * opposite sense from the conventional "low=command, high=data" (some
 * clone boards do), every command byte would land in the panel's data
 * path and vice versa - the panel would never see a valid SWRESET/
 * DISPON, explaining a permanently-black screen with no bus-level
 * error. The overlay's dc-gpios flag has been flipped to
 * GPIO_ACTIVE_LOW to test this without any code or wiring change -
 * gpio_pin_set_dt() below still calls with the same 0=command/1=data
 * values, but the physical HIGH/LOW meaning is now inverted.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <stdbool.h>

#define DISP_NODE DT_NODELABEL(st7789v_disp)

#define PANEL_WIDTH   DT_PROP(DISP_NODE, width)
#define PANEL_HEIGHT  DT_PROP(DISP_NODE, height)
#define X_OFFSET      DT_PROP(DISP_NODE, x_offset)
#define Y_OFFSET      DT_PROP(DISP_NODE, y_offset)

/* ST7789 command set (only what this lab needs). */
#define ST7789_SWRESET  0x01
#define ST7789_SLPOUT   0x11
#define ST7789_COLMOD   0x3A
#define ST7789_MADCTL   0x36
#define ST7789_INVON    0x21
#define ST7789_NORON    0x13
#define ST7789_DISPON   0x29
#define ST7789_CASET    0x2A
#define ST7789_RASET    0x2B
#define ST7789_RAMWR    0x2C
#define ST7789_PORCTRL  0xB2
#define ST7789_GCTRL    0xB7
#define ST7789_VCOMS    0xBB
#define ST7789_LCMCTRL  0xC0
#define ST7789_VDVVRHEN 0xC2
#define ST7789_VRHS     0xC3
#define ST7789_VDVS     0xC4
#define ST7789_FRCTRL2  0xC6
#define ST7789_PWCTRL1  0xD0
#define ST7789_PVGAMCTRL 0xE0
#define ST7789_NVGAMCTRL 0xE1

/* RGB565 colors used by the demo pattern. */
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F

static const struct spi_dt_spec spi_spec =
	SPI_DT_SPEC_GET(DISP_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);
static const struct gpio_dt_spec reset_spec = GPIO_DT_SPEC_GET(DISP_NODE, reset_gpios);
static const struct gpio_dt_spec dc_spec = GPIO_DT_SPEC_GET(DISP_NODE, dc_gpios);

/* Minimal 5x7 font - only the glyphs this lab prints ("Hello World!").
 * Same column-major, bottom-to-top bit convention used by the other
 * raw-framebuffer labs in this series, so the table is copied
 * verbatim for consistency.
 */
struct glyph {
	char ch;
	uint8_t cols[5];
};

static const struct glyph font5x7[] = {
	{' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
	{'!', {0x00, 0x00, 0x5F, 0x00, 0x00}},
	{'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
	{'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
	{'d', {0x38, 0x44, 0x44, 0x48, 0x7F}},
	{'e', {0x38, 0x54, 0x54, 0x54, 0x18}},
	{'l', {0x00, 0x41, 0x7F, 0x40, 0x00}},
	{'o', {0x38, 0x44, 0x44, 0x44, 0x38}},
	{'r', {0x7C, 0x08, 0x04, 0x04, 0x08}},
	{'x', {0x22, 0x14, 0x08, 0x14, 0x22}},
};

static const uint8_t *glyph_lookup(char c)
{
	static const uint8_t blank[5] = {0x00, 0x00, 0x00, 0x00, 0x00};

	for (size_t i = 0; i < ARRAY_SIZE(font5x7); i++) {
		if (font5x7[i].ch == c) {
			return font5x7[i].cols;
		}
	}
	return blank;
}

/* SR110: SPI0's hardware FIFO is only 8 bytes deep - see the file
 * header comment. Every send chunks to this size, letting native CS
 * (spi_write_dt() asserts/releases it automatically per call, exactly
 * like Lab 05's SSD1306 writes) toggle normally between chunks -
 * confirmed fine on this hardware, unlike the software-CS-continuity
 * detour an earlier version of this file took.
 */
#define ST7789_CHUNK_BYTES 8

static int st7789_send(int dc_value, const uint8_t *data, size_t len)
{
	int ret;

	gpio_pin_set_dt(&dc_spec, dc_value);

	while (len) {
		size_t chunk = MIN(len, ST7789_CHUNK_BYTES);
		struct spi_buf buf = { .buf = (void *)data, .len = chunk };
		struct spi_buf_set set = { .buffers = &buf, .count = 1 };

		ret = spi_write_dt(&spi_spec, &set);
		if (ret) {
			printk("  spi_write_dt(dc=%d, len=%zu) failed, ret=%d\n",
			       dc_value, chunk, ret);
			return ret;
		}
		data += chunk;
		len -= chunk;
	}

	return 0;
}

static int st7789_write_cmd(uint8_t cmd)
{
	return st7789_send(0, &cmd, 1);
}

static int st7789_write_data(const uint8_t *data, size_t len)
{
	return st7789_send(1, data, len);
}

static int st7789_reset(void)
{
	int ret = gpio_pin_configure_dt(&reset_spec, GPIO_OUTPUT_INACTIVE);

	if (ret) {
		return ret;
	}
	k_sleep(K_MSEC(10));
	gpio_pin_set_dt(&reset_spec, 1); /* assert reset */
	k_sleep(K_MSEC(10));
	gpio_pin_set_dt(&reset_spec, 0); /* release reset */
	k_sleep(K_MSEC(150));            /* datasheet: allow up to ~120ms to recover */

	return 0;
}

/* SR110 DEBUG STEP (2026-09-03): Lab 08 (ST7735), using a full
 * power/frame-rate/gamma init sequence, works correctly on this exact
 * SPI0/CS/pin/chunking setup. This lab's original init sequence was
 * the bare MIPI-DCS minimum (SWRESET/SLPOUT/COLMOD/MADCTL/INVON/NORON/
 * DISPON only, no porch/gate/VCOM/power/gamma setup at all) and never
 * produced a visible image despite completing without any SPI error -
 * consistent with a panel that's technically responding to commands
 * but never gets its analog/timing configured well enough to actually
 * drive the glass. Replaced with a standard, widely-used ST7789V init
 * table (cross-checked against ESPHome's st7789v component, Bodmer's
 * TFT_eSPI library, and Adafruit's Raspberry Pi fbtft driver, which
 * all agree on this sequence/these values) in the same
 * (command, args, delay) table style as Lab 08, for easy comparison.
 */
struct st7789_init_cmd {
	uint8_t cmd;
	uint8_t num_args;
	uint8_t args[16];
	uint16_t delay_ms;
};

static const struct st7789_init_cmd init_seq[] = {
	{ ST7789_SWRESET,   0, {0}, 150 },
	{ ST7789_SLPOUT,    0, {0}, 255 },
	{ ST7789_COLMOD,    1, {0x55}, 10 },
	{ ST7789_PORCTRL,   5, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 0 },
	{ ST7789_GCTRL,     1, {0x35}, 0 },
	{ ST7789_VCOMS,     1, {0x28}, 0 },
	{ ST7789_LCMCTRL,   1, {0x0C}, 0 },
	{ ST7789_VDVVRHEN,  2, {0x01, 0xFF}, 0 },
	{ ST7789_VRHS,      1, {0x10}, 0 },
	{ ST7789_VDVS,      1, {0x20}, 0 },
	{ ST7789_FRCTRL2,   1, {0x0F}, 0 },
	{ ST7789_PWCTRL1,   2, {0xA4, 0xA1}, 0 },
	{ ST7789_MADCTL,    1, {0x00}, 0 },
	{ ST7789_INVON,     0, {0}, 10 },
	{ ST7789_PVGAMCTRL, 14, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44,
				 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17}, 0 },
	{ ST7789_NVGAMCTRL, 14, {0xD0, 0x00, 0x02, 0x07, 0x05, 0x25, 0x2D, 0x44,
				 0x45, 0x10, 0x0E, 0x12, 0x13, 0x17}, 0 },
	{ ST7789_NORON,     0, {0}, 10 },
	{ ST7789_DISPON,    0, {0}, 100 },
};

static int st7789_init(void)
{
	int ret;

	for (size_t i = 0; i < ARRAY_SIZE(init_seq); i++) {
		ret = st7789_write_cmd(init_seq[i].cmd);
		if (ret) return ret;

		if (init_seq[i].num_args) {
			ret = st7789_write_data(init_seq[i].args, init_seq[i].num_args);
			if (ret) return ret;
		}
		if (init_seq[i].delay_ms) {
			k_sleep(K_MSEC(init_seq[i].delay_ms));
		}
	}

	return 0;
}

static int st7789_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	uint16_t xs = x0 + X_OFFSET, xe = x1 + X_OFFSET;
	uint16_t ys = y0 + Y_OFFSET, ye = y1 + Y_OFFSET;
	uint8_t caset[4] = { xs >> 8, xs & 0xFF, xe >> 8, xe & 0xFF };
	uint8_t raset[4] = { ys >> 8, ys & 0xFF, ye >> 8, ye & 0xFF };
	int ret;

	ret = st7789_write_cmd(ST7789_CASET);
	if (ret) return ret;
	ret = st7789_write_data(caset, sizeof(caset));
	if (ret) return ret;

	ret = st7789_write_cmd(ST7789_RASET);
	if (ret) return ret;
	ret = st7789_write_data(raset, sizeof(raset));
	if (ret) return ret;

	return st7789_write_cmd(ST7789_RAMWR);
}

/* Fills a rectangle with a solid RGB565 color, one row at a time from
 * a small stack buffer (no full-screen framebuffer is kept in RAM -
 * consistent with this series' raw-SPI, direct-write approach).
 */
static int st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
	uint8_t row[PANEL_WIDTH * 2];
	int ret;

	ret = st7789_set_addr_window(x, y, x + w - 1, y + h - 1);
	if (ret) return ret;

	for (uint16_t i = 0; i < w; i++) {
		row[i * 2] = color >> 8;
		row[i * 2 + 1] = color & 0xFF;
	}

	for (uint16_t line = 0; line < h; line++) {
		ret = st7789_write_data(row, (size_t)w * 2);
		if (ret) return ret;
	}

	return 0;
}

/* Draws one character scaled up by `scale`, one source pixel row at a
 * time (again no per-character framebuffer needed).
 */
static int st7789_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
	const uint8_t *glyph = glyph_lookup(c);
	uint8_t row[5 * 8 * 2]; /* generous fixed max: 5 cols * up to 8x scale * 2 bytes */
	int ret;

	ret = st7789_set_addr_window(x, y, x + 5 * scale - 1, y + 7 * scale - 1);
	if (ret) return ret;

	for (uint8_t srow = 0; srow < 7; srow++) {
		for (uint8_t scol = 0; scol < 5; scol++) {
			bool on = (glyph[scol] >> srow) & 0x1;
			uint16_t color = on ? fg : bg;

			for (uint8_t sx = 0; sx < scale; sx++) {
				size_t idx = (scol * scale + sx) * 2;

				row[idx] = color >> 8;
				row[idx + 1] = color & 0xFF;
			}
		}
		for (uint8_t sy = 0; sy < scale; sy++) {
			ret = st7789_write_data(row, (size_t)5 * scale * 2);
			if (ret) return ret;
		}
	}

	return 0;
}

static int st7789_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t scale)
{
	int ret;

	while (*str) {
		ret = st7789_draw_char(x, y, *str++, fg, bg, scale);
		if (ret) return ret;
		x += 6 * scale; /* 5 glyph columns + 1 blank column */
	}
	return 0;
}

int main(void)
{
	printk("\n=== TFT ST7789V3 (SPI, 240x%d) ===\n", PANEL_HEIGHT);

	if (!spi_is_ready_dt(&spi_spec)) {
		printk("SPI0 device not ready - check devicetree status/overlay\n");
		return 0;
	}
	if (!gpio_is_ready_dt(&reset_spec) || !gpio_is_ready_dt(&dc_spec)) {
		printk("RST/DC GPIO controller not ready\n");
		return 0;
	}

	if (gpio_pin_configure_dt(&dc_spec, GPIO_OUTPUT_INACTIVE)) {
		printk("DC GPIO configure failed\n");
		return 0;
	}

	if (st7789_reset()) {
		printk("Panel reset failed - check RST wiring\n");
		return 0;
	}

	if (st7789_init()) {
		printk("ST7789 init failed (SPI write error) - check wiring\n");
		return 0;
	}

	/* Black background, then a stack of color bars, then a text
	 * banner drawn on top - same "text + color bars" demo pattern
	 * used by this series' other raw-SPI color TFT lab. */
	if (st7789_fill_rect(0, 0, PANEL_WIDTH, PANEL_HEIGHT, COLOR_BLACK)) {
		printk("Fill (background) failed\n");
		return 0;
	}

	static const uint16_t bar_colors[] = {
		COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW, COLOR_CYAN, COLOR_MAGENTA, COLOR_WHITE,
	};
	uint16_t bar_height = PANEL_HEIGHT / (ARRAY_SIZE(bar_colors) + 2);
	uint16_t bars_top = PANEL_HEIGHT - (bar_height * ARRAY_SIZE(bar_colors));

	for (size_t i = 0; i < ARRAY_SIZE(bar_colors); i++) {
		if (st7789_fill_rect(0, bars_top + i * bar_height, PANEL_WIDTH, bar_height,
				      bar_colors[i])) {
			printk("Fill (color bar %zu) failed\n", i);
			return 0;
		}
	}

	if (st7789_draw_string(10, 10, "Hello World!", COLOR_WHITE, COLOR_BLACK, 2)) {
		printk("Text draw failed\n");
		return 0;
	}

	printk("ST7789V3 initialized, color bars + \"Hello World!\" drawn\n");

	while (1) {
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
