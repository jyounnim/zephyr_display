/*
 * Lab 03: SSD1306 0.96" OLED, I2C mode, with runtime address
 * auto-detection - raw I2C, no Zephyr Display/CFB subsystem.
 *
 * Board: Synaptics SR110 (sr100_rdk/sr100/m55)
 * Bus:   I2C0 (pin group i2c0_ms_scl/i2c0_ms_sda) - same bus as Labs
 *        01/02, independent of SPI0/UART0/UART1's shared pin group.
 *
 * WHY RAW I2C INSTEAD OF ZEPHYR'S "solomon,ssd1306" DRIVER:
 * SSD1306 modules ship at one of two 7-bit addresses depending on how
 * the module's SA0 pin is strapped - 0x3C or 0x3D. A devicetree node's
 * `reg` property is fixed at build time, so the standard driver can
 * only target one address per build. This lab's whole point is to
 * *scan* for whichever address is actually present at boot (same
 * pattern as Lab 01's bus scanner) and use that - so it talks to the
 * controller directly with i2c_write(), the same approach Lab 02 used
 * for the raw-I2C PCF8574 LCD backpack.
 *
 * I2C PROTOCOL: every SSD1306 I2C transaction starts with a control
 * byte after the device address - 0x00 selects a command stream, 0x40
 * selects a GDDRAM (pixel data) stream. This lab sends one command per
 * transaction (control byte + 1 command byte), and the whole 1024-byte
 * framebuffer as a single transaction too (control byte + 1024 data
 * bytes, copied into one contiguous static buffer and sent with one
 * i2c_write() call - see ssd1306_data()'s comment for why a two-message
 * i2c_transfer() was tried first and abandoned after it produced a
 * screen full of noise on real hardware).
 *
 * ADDRESS PROBING: uses a 1-byte i2c_read() per candidate address, the
 * same probe style established in Lab 01 - on this platform's I2C
 * driver, a write-based probe is confirmed to miss real devices, while
 * i2c_read() reliably reports ACK/NACK.
 *
 * INIT SEQUENCE: a standard 128x64 SSD1306 init sequence (internal
 * charge pump enabled, since almost all breakout modules have no
 * external Vcc supply for the panel), horizontal addressing mode,
 * segment remap + COM scan direction remapped to match how the glass
 * is typically bonded onto these breakout boards right-side-up.
 *
 * HARDWARE RESET (RES) PIN: some SSD1306 breakout modules expose a
 * RES/RST pin that needs a low-then-high pulse before I2C commands are
 * sent, or the panel shows scattered dot noise indefinitely even
 * though the software init sequence completes without error (see
 * ssd1306_init()'s comment). Most low-cost 4-pin (VCC/GND/SDA/SCL)
 * modules - the common case, e.g. typical AliExpress modules - have no
 * such pin at all and don't need any of this (leave OLED_USE_HW_RESET
 * at 0 if that's your module).
 *
 * PIN SAFETY WARNING: do not assume a pin is free just because a
 * schematic net name looks unrelated to this lab. An earlier version
 * of this file drove SoC GPIO26 (labeled SD0_CLK, and this project
 * doesn't use the SD card interface) as the reset line, and on real
 * SR110 hardware this produced a full lockup - no serial output at
 * all, not even the boot banner - not just "RST doesn't work." Exactly
 * why is unconfirmed, but checking sr100_pinctrl.dtsi afterwards
 * showed GPIO26's alternate-function group also included a Debug
 * Module signal (dm0_clk_a) alongside sd0_clk - possibly relevant,
 * possibly not. This file now uses GPIO4 instead, chosen specifically
 * because its alternate-function group (gpio_4 / ciu_vsync_a /
 * uart0_cts) has no JTAG/Debug-Module/boot-strap-sounding neighbor and
 * isn't referenced anywhere in the base board dts by default - but
 * this is still an unverified pin choice, not a proven-safe one.
 * Confirm the board still boots and prints its serial banner with this
 * pin wired and toggled before trusting it for real use. If it also
 * causes problems, set OLED_USE_HW_RESET back to 0 and treat any
 * further candidate pin the same way: check its full alternate-
 * function group in sr100_pinctrl.dtsi, prefer pins not shared with
 * JTAG/debug/SD/clock functions, and verify boot survives before
 * wiring anything to it.
 */

#define OLED_USE_HW_RESET 1 /* GPIO4 - see the pin choice rationale below.
			      * Still unverified on real hardware; if the
			      * board fails to boot/print its banner with
			      * this pin wired, set this back to 0 and try
			      * a different pin. */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <stdbool.h>
#include <string.h>

#define I2C0_NODE DT_NODELABEL(i2c0)

/* Hardware reset (RES) pin, only used when OLED_USE_HW_RESET is 1.
 *
 * PIN CHOICE RATIONALE: SoC GPIO4 was chosen after the GPIO26 lockup
 * (see the warning above) by checking sr100_pinctrl.dtsi directly
 * rather than trusting the schematic net name alone. GPIO26's mux
 * group also included sd0_clk *and* dm0_clk_a (a Debug Module
 * function) - GPIO4's mux group only includes ciu_vsync_a (unused
 * camera VSYNC) and uart0_cts (unused UART0 flow control), with no
 * JTAG/Debug-Module/boot-strap-sounding alternate function anywhere
 * in the group. It's also not referenced anywhere in the base board
 * dts by default. None of this *proves* it's safe - there is no
 * devicetree mechanism in this SoC's model to force a bare GPIO pin's
 * mux via an overlay the way peripherals like i2c0/spi0 do with their
 * own pinctrl-0 property, so this still relies on the SoC's reset-
 * default mux state actually being GPIO (alternate-function 0), same
 * as the already-working &gpioa 3 / &gpioa 25 precedents elsewhere in
 * this project. Confirmed against the SR110 RDK schematic
 * (SC950-C01116-01 RevE, sheet 10): broken out on J25 ("Left 20pin
 * CONN") pin 5.
 *
 * Still verify boot survives with this pin wired/toggled before
 * trusting it (see OLED_USE_HW_RESET's comment).
 */
#define OLED_RST_GPIO_NODE DT_NODELABEL(gpioa)
#define OLED_RST_PIN       4

#define LCD_WIDTH  128
#define LCD_HEIGHT 64
#define LCD_PAGES  (LCD_HEIGHT / 8) /* 8 pages of 8 vertical pixels each */

/* The only two addresses an SSD1306 module can be strapped to. */
static const uint8_t oled_addr_candidates[] = { 0x3C, 0x3D };

/* SSD1306 I2C control bytes (sent as the first byte of every transaction). */
#define SSD1306_CTRL_CMD  0x00
#define SSD1306_CTRL_DATA 0x40

static uint8_t framebuffer[LCD_WIDTH * LCD_PAGES];

/* Minimal 5x7 font - only the glyphs this lab actually prints
 * ("Hello World!" and "Addr 0x3C"/"Addr 0x3D"). Same column-major,
 * bottom-to-top bit convention as the Nokia 5110 lab's font table, and
 * the lowercase letters below reuse those exact values for consistency
 * across the series. */
struct glyph {
	char ch;
	uint8_t cols[5];
};

static const struct glyph font5x7[] = {
	{' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
	{'!', {0x00, 0x00, 0x5F, 0x00, 0x00}},
	{'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
	{'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
	{'A', {0x7C, 0x12, 0x11, 0x12, 0x7C}},
	{'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
	{'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
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

#if OLED_USE_HW_RESET
/* Pulses the RES pin low then high (see the file header comment for
 * why this is required on some modules, and the warning about picking
 * a pin carefully). Uses gpio_pin_set_raw() rather than the
 * ACTIVE_LOW-aware gpio_pin_set(), since this pin isn't described via
 * a devicetree gpio-spec here - "raw" just means the values below are
 * the literal physical level.
 */
static int oled_hw_reset(void)
{
	const struct device *gpioa = DEVICE_DT_GET(OLED_RST_GPIO_NODE);
	int ret;

	if (!device_is_ready(gpioa)) {
		printk("RST GPIO controller (gpioa) not ready\n");
		return -ENODEV;
	}

	ret = gpio_pin_configure(gpioa, OLED_RST_PIN, GPIO_OUTPUT_HIGH);
	if (ret) {
		return ret;
	}

	k_sleep(K_MSEC(10));                       /* settle, not in reset */
	gpio_pin_set_raw(gpioa, OLED_RST_PIN, 0);  /* assert reset (low) */
	k_sleep(K_MSEC(10));
	gpio_pin_set_raw(gpioa, OLED_RST_PIN, 1);  /* release reset (high) */
	k_sleep(K_MSEC(10));                       /* let the panel come back up */

	return 0;
}
#endif /* OLED_USE_HW_RESET */

/* Probe a single 7-bit address with a 1-byte read (confirmed reliable
 * on this platform's I2C driver - see Lab 01's scanner).
 */
static bool i2c_probe_addr(const struct device *bus, uint8_t addr)
{
	uint8_t dummy;
	int ret = i2c_read(bus, &dummy, 1, addr);

	return (ret == 0);
}

/* Scan the two possible SSD1306 addresses and report which one (if
 * any) responds. Returns true and fills *addr_out on success.
 *
 * Each candidate gets a few retries with a short gap in between,
 * rather than a single probe - a single miss right after power-on
 * doesn't necessarily mean "not present", and this costs at most a
 * few tens of milliseconds in the failure case.
 */
#define SSD1306_PROBE_RETRIES     3
#define SSD1306_PROBE_RETRY_DELAY K_MSEC(20)

static bool ssd1306_find_address(const struct device *bus, uint8_t *addr_out)
{
	printk("Scanning for SSD1306 at 0x3C / 0x3D...\n");
	for (size_t i = 0; i < ARRAY_SIZE(oled_addr_candidates); i++) {
		uint8_t addr = oled_addr_candidates[i];

		for (int attempt = 0; attempt < SSD1306_PROBE_RETRIES; attempt++) {
			if (i2c_probe_addr(bus, addr)) {
				printk("  found device at 0x%02X\n", addr);
				*addr_out = addr;
				return true;
			}
			k_sleep(SSD1306_PROBE_RETRY_DELAY);
		}
	}
	return false;
}

static int ssd1306_cmd(const struct device *bus, uint8_t addr, uint8_t cmd)
{
	uint8_t buf[2] = { SSD1306_CTRL_CMD, cmd };

	return i2c_write(bus, buf, sizeof(buf), addr);
}

/* Sends the control byte and the payload as one I2C transaction, using
 * the same plain i2c_write() pattern already confirmed working for
 * ssd1306_cmd() above.
 *
 * An earlier version of this function used i2c_transfer() with two
 * separate messages (control byte, then payload, with I2C_MSG_STOP
 * only on the second) to avoid copying the 1024-byte framebuffer into
 * a new buffer. On real SR110 hardware this produced a screen full of
 * noise instead of the expected text - the likely explanation is that
 * this platform's designware I2C driver inserts a STOP/restart between
 * the two messages rather than continuing the same transaction, which
 * would make the SSD1306 see a lone control byte, then a *second*
 * transaction whose first byte (the first framebuffer byte) gets
 * misread as a fresh control byte instead of pixel data - shifting
 * everything after it by one byte and effectively randomizing which
 * bytes are treated as commands vs. data. That matches "noise" far
 * better than a clean off-by-one visual shift would. Copying into one
 * contiguous buffer and sending a single i2c_write() sidesteps the
 * question of exactly how this driver handles multi-message transfers
 * entirely, at the cost of one extra static 1025-byte buffer.
 */
static uint8_t oled_data_tx_buf[1 + LCD_WIDTH * LCD_PAGES];

static int ssd1306_data(const struct device *bus, uint8_t addr,
			 const uint8_t *data, size_t len)
{
	oled_data_tx_buf[0] = SSD1306_CTRL_DATA;
	memcpy(&oled_data_tx_buf[1], data, len);

	return i2c_write(bus, oled_data_tx_buf, len + 1, addr);
}

/* Small helper so the (fairly long) init sequence below reads as a
 * flat list of commands instead of 16 repeated "if (ret) return ret;"
 * blocks. `bus`, `addr`, and `ret` are expected to be in scope.
 *
 * On failure this prints exactly which command byte failed and with
 * what error code - needed to tell apart "the whole bus is unhappy"
 * from "this one specific command NACKs" while debugging on real
 * hardware, since a bare "init failed" doesn't distinguish those.
 *
 * The 1ms gap after every command is deliberately generous. It is not
 * required by the SSD1306 datasheet (which only specifies a bus-free
 * time in the microsecond range), but costs nothing during init and
 * rules out "commands sent faster than the controller can latch them"
 * as a variable while debugging a real I2C write failure.
 */
#define SSD1306_CMD(c) do { \
		ret = ssd1306_cmd(bus, addr, (c)); \
		if (ret) { \
			printk("  ssd1306 cmd 0x%02X failed, ret=%d\n", (c), ret); \
			return ret; \
		} \
		k_sleep(K_MSEC(1)); \
	} while (0)

static int ssd1306_init(const struct device *bus, uint8_t addr)
{
	int ret;

	SSD1306_CMD(0xAE);       /* Display off */
	SSD1306_CMD(0xD5);       /* Set display clock divide ratio/osc freq */
	SSD1306_CMD(0x80);
	SSD1306_CMD(0xA8);       /* Set multiplex ratio */
	SSD1306_CMD(LCD_HEIGHT - 1);
	SSD1306_CMD(0xD3);       /* Set display offset */
	SSD1306_CMD(0x00);
	SSD1306_CMD(0x40);       /* Set display start line = 0 */
	SSD1306_CMD(0x8D);       /* Charge pump: enable (no external Vcc) */
	SSD1306_CMD(0x14);
	SSD1306_CMD(0x20);       /* Memory addressing mode: horizontal */
	SSD1306_CMD(0x00);
	SSD1306_CMD(0xA1);       /* Segment remap (column 127 = SEG0) */
	SSD1306_CMD(0xC8);       /* COM output scan direction: remapped */
	SSD1306_CMD(0xDA);       /* COM pins hardware configuration */
	SSD1306_CMD(0x12);
	SSD1306_CMD(0x81);       /* Contrast control */
	SSD1306_CMD(0xCF);
	SSD1306_CMD(0xD9);       /* Pre-charge period */
	SSD1306_CMD(0xF1);
	SSD1306_CMD(0xDB);       /* VCOMH deselect level */
	SSD1306_CMD(0x40);
	SSD1306_CMD(0xA4);       /* Resume to RAM content display */
	SSD1306_CMD(0xA6);       /* Normal (not inverted) display */
	SSD1306_CMD(0xAF);       /* Display on */

	return 0;
}

static void fb_draw_char(int col, int page, char c)
{
	if (page < 0 || page >= LCD_PAGES) {
		return;
	}
	const uint8_t *glyph = glyph_lookup(c);

	for (int i = 0; i < 5; i++) {
		int x = col + i;

		if (x >= 0 && x < LCD_WIDTH) {
			framebuffer[page * LCD_WIDTH + x] = glyph[i];
		}
	}
	/* 6th column is left as-is (0-initialized) for the letter-spacing gap. */
}

static void fb_draw_string(int col, int page, const char *str)
{
	while (*str) {
		fb_draw_char(col, page, *str++);
		col += 6; /* 5 glyph columns + 1 blank column */
	}
}

/* Pushes the whole framebuffer out in one shot. Re-sending the
 * column/page address window every time (rather than relying on the
 * controller's auto-increment/wrap alone) makes this self-contained -
 * safe to call repeatedly without tracking cursor state elsewhere.
 */
static int ssd1306_update(const struct device *bus, uint8_t addr)
{
	int ret;

	SSD1306_CMD(0x21);            /* Set column address range */
	SSD1306_CMD(0);
	SSD1306_CMD(LCD_WIDTH - 1);
	SSD1306_CMD(0x22);            /* Set page address range */
	SSD1306_CMD(0);
	SSD1306_CMD(LCD_PAGES - 1);

	return ssd1306_data(bus, addr, framebuffer, sizeof(framebuffer));
}

int main(void)
{
	const struct device *i2c0 = DEVICE_DT_GET(I2C0_NODE);
	uint8_t oled_addr;

	printk("\n=== OLED SSD1306 (I2C, SR110) ===\n");

	if (!device_is_ready(i2c0)) {
		printk("I2C0 device not ready - check devicetree status/overlay\n");
		return 0;
	}

	/* Give the OLED's own power supply/POR circuit time to settle
	 * before probing it. Lab 01's full 0x08-0x77 scan reaches 0x3C/
	 * 0x3D fairly late (after already probing ~50 other addresses),
	 * so it incidentally gets this delay for free. This lab probes
	 * only two addresses right at boot, so without an explicit delay
	 * it can reach the module before it's ready to ACK on the bus -
	 * confirmed on real hardware to cause "not found" on a module
	 * that Lab 01 finds just fine on the exact same wiring/power.
	 */
	k_sleep(K_MSEC(100));

	if (!ssd1306_find_address(i2c0, &oled_addr)) {
		printk("No SSD1306 found at 0x3C or 0x3D - check wiring/power\n");
		printk("(see this lab's doc: board-rail power has been observed\n");
		printk(" to make I2C devices drop out on this platform - try an\n");
		printk(" external 3.3V supply for the module before anything else)\n");
		return 0;
	}

	/* Brief settle gap between the read-based probe above and the
	 * write-based init sequence below - cheap insurance in case the
	 * bus/controller needs a moment between a read transaction and
	 * the first write transaction that follows it.
	 */
	k_sleep(K_MSEC(10));

#if OLED_USE_HW_RESET
	if (oled_hw_reset()) {
		printk("SSD1306 hardware reset failed - check RST wiring (gpioa %d)\n",
		       OLED_RST_PIN);
		return 0;
	}
#endif

	if (ssd1306_init(i2c0, oled_addr)) {
		printk("SSD1306 init failed (I2C write error) - check wiring\n");
		return 0;
	}

	memset(framebuffer, 0, sizeof(framebuffer));
	fb_draw_string(0, 0, "Hello World!");
	fb_draw_string(0, 2, oled_addr == 0x3C ? "Addr 0x3C" : "Addr 0x3D");

	if (ssd1306_update(i2c0, oled_addr)) {
		printk("SSD1306 framebuffer update failed (I2C write error)\n");
		return 0;
	}

	printk("SSD1306 initialized at 0x%02X, \"Hello World!\" written\n", oled_addr);

	while (1) {
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
