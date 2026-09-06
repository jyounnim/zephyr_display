/*
 * Lab 02: I2C character LCD (HD44780 16x2 through a PCF8574/PCF8574A
 * I2C backpack), "LiquidCrystal-I2C" style driver, raw I2C, no Zephyr
 * Display/CFB subsystem.
 *
 * Board:   Synaptics SR110 (sr100_rdk/sr100/m55)
 *          (ported from ESP32-S3-DevKitC-1; see 02_I2C_LCD_LAB_KR.md /
 *          _EN.md "SR110 포팅" section for what changed)
 * Bus:     I2C0, pin group i2c0_ms_scl / i2c0_ms_sda (fixed by the SoC,
 *          not software-configurable like ESP32-S3's GPIO matrix)
 * Backpack address: 0x27 (PCF8574, most common "LCM1602 IIC" boards)
 *                    0x3F (PCF8574A - this is what this lab defaults to,
 *                          since that's the module this lab was built
 *                          and tested against)
 *
 * This is a fresh, independent lab (not a continuation of the earlier
 * SSD1306/SH1106 OLED lab, which is parked pending different hardware).
 * It reuses only two things learned there:
 *   1. The board's I2C0 pin facts (I2C0 / GPIO1 / GPIO2).
 *   2. The bus-scan-before-talking approach (see main()), which is handy
 *      here too since these backpacks ship at either 0x27 or 0x3F
 *      depending on whether the PCF8574 or PCF8574A variant is populated.
 *
 * --- PCF8574 bit map (standard "LiquidCrystal_I2C" wiring) ---
 *   P0 -> LCD RS   (0 = command, 1 = data)
 *   P1 -> LCD RW   (tied to write-only here; always driven 0)
 *   P2 -> LCD EN   (rising/falling edge latches the nibble on P4-P7)
 *   P3 -> Backlight transistor gate (1 = backlight on)
 *   P4-P7 -> LCD D4-D7 (4-bit data bus, upper nibble first)
 *
 * This bit map is the de-facto standard across essentially every
 * "LCM1602 IIC" / "LiquidCrystal_I2C" backpack board and Arduino
 * library clone. If a given module wires it differently, cursor
 * position and character glyphs will be wrong even though the I2C
 * ACK/scan succeeds - see the KR doc's troubleshooting table.
 *
 * --- Power / signal level caution ---
 * Many of these backpacks (and the HD44780 panel behind them) are
 * meant to run from 5V: the PCF8574's own pull-up resistors on
 * SDA/SCL then idle at 5V, which is out of spec for the ESP32-S3's
 * 3.3V-only GPIOs. See the KR doc for the safe wiring options
 * (running the module at 3.3V, or a bidirectional logic-level
 * shifter) before connecting SDA/SCL directly.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define I2C0_NODE DT_NODELABEL(i2c0)

/* The two addresses these backpacks commonly ship at. */
#define LCD_ADDR_PCF8574   0x27
#define LCD_ADDR_PCF8574A  0x3F

/* PCF8574 output bit map, see file header comment. */
#define LCD_RS BIT(0)
#define LCD_RW BIT(1)
#define LCD_EN BIT(2)
#define LCD_BL BIT(3)

#define LCD_COLS 16
#define LCD_ROWS 2

/* HD44780 commands used below. */
#define HD44780_CLEAR_DISPLAY   0x01
#define HD44780_ENTRY_MODE_SET  0x06 /* increment cursor, no display shift */
#define HD44780_DISPLAY_CONTROL 0x0C /* display on, cursor off, blink off */
#define HD44780_FUNCTION_SET    0x28 /* 4-bit bus, 2 lines, 5x8 font */
#define HD44780_SET_DDRAM_ADDR  0x80

static uint8_t lcd_addr = LCD_ADDR_PCF8574A; /* overwritten after the scan */
static uint8_t lcd_backlight = LCD_BL;       /* keep backlight on throughout */

/* Row start addresses for a standard 16x2 (and 20x2) HD44780 DDRAM map. */
static const uint8_t row_offsets[LCD_ROWS] = { 0x00, 0x40 };

static int pcf8574_write(const struct device *i2c, uint8_t data)
{
	return i2c_write(i2c, &data, 1, lcd_addr);
}

/* Latch whatever is currently on D4-D7/RS/RW/BL with an EN pulse.
 * HD44780 timing needs EN high >= 450 ns and a settle time afterwards;
 * k_busy_wait() is used for both since these are sub-microsecond/
 * low-microsecond delays that k_sleep() can't hit reliably.
 */
static int lcd_pulse_enable(const struct device *i2c, uint8_t data)
{
	int ret;

	ret = pcf8574_write(i2c, data | LCD_EN);
	if (ret) {
		return ret;
	}
	k_busy_wait(1);

	ret = pcf8574_write(i2c, data & ~LCD_EN);
	if (ret) {
		return ret;
	}
	k_busy_wait(50);

	return 0;
}

/* Send one 4-bit nibble (already in the upper 4 bits of `nibble`). */
static int lcd_write4(const struct device *i2c, uint8_t nibble, bool rs)
{
	uint8_t data = (nibble & 0xF0) | lcd_backlight;

	if (rs) {
		data |= LCD_RS;
	}

	int ret = pcf8574_write(i2c, data);
	if (ret) {
		return ret;
	}
	return lcd_pulse_enable(i2c, data);
}

/* Send a full byte as two nibbles, high nibble first. */
static int lcd_write_byte(const struct device *i2c, uint8_t byte, bool rs)
{
	int ret = lcd_write4(i2c, byte & 0xF0, rs);
	if (ret) {
		return ret;
	}
	return lcd_write4(i2c, (byte << 4) & 0xF0, rs);
}

static int lcd_command(const struct device *i2c, uint8_t cmd)
{
	return lcd_write_byte(i2c, cmd, false);
}

static int lcd_data(const struct device *i2c, uint8_t ch)
{
	return lcd_write_byte(i2c, ch, true);
}

static int lcd_clear(const struct device *i2c)
{
	int ret = lcd_command(i2c, HD44780_CLEAR_DISPLAY);
	/* Clear/home are slow HD44780 commands (~1.5-2 ms); no busy-flag
	 * read here (RW is tied to write-only), so just wait it out. */
	k_sleep(K_MSEC(2));
	return ret;
}

static int lcd_set_cursor(const struct device *i2c, uint8_t row, uint8_t col)
{
	if (row >= LCD_ROWS) {
		row = LCD_ROWS - 1;
	}
	if (col >= LCD_COLS) {
		col = LCD_COLS - 1;
	}
	return lcd_command(i2c, HD44780_SET_DDRAM_ADDR | (row_offsets[row] + col));
}

static int lcd_print(const struct device *i2c, const char *str)
{
	int ret;

	while (*str) {
		ret = lcd_data(i2c, (uint8_t)*str++);
		if (ret) {
			return ret;
		}
	}
	return 0;
}

/*
 * Standard HD44780 "4-bit mode by instruction" init sequence
 * (Hitachi HD44780U datasheet, Figure 24). This talks to the
 * controller in 8-bit-nibble bursts before it has committed to 4-bit
 * mode, so the first three writes go out as raw nibbles via
 * lcd_write4(), not lcd_write_byte().
 */
static int lcd_init(const struct device *i2c)
{
	int ret;

	/* Wait for HD44780 internal reset to finish after power-on
	 * (datasheet requires >40 ms once Vcc >= 2.7V; be generous). */
	k_sleep(K_MSEC(50));

	ret = lcd_write4(i2c, 0x30, false);
	if (ret) return ret;
	k_sleep(K_MSEC(5));

	ret = lcd_write4(i2c, 0x30, false);
	if (ret) return ret;
	k_busy_wait(150);

	ret = lcd_write4(i2c, 0x30, false);
	if (ret) return ret;
	k_busy_wait(150);

	/* Switch to 4-bit interface. */
	ret = lcd_write4(i2c, 0x20, false);
	if (ret) return ret;
	k_busy_wait(150);

	/* From here on the controller is in 4-bit mode: every command
	 * is a normal two-nibble write via lcd_command(). */
	ret = lcd_command(i2c, HD44780_FUNCTION_SET);
	if (ret) return ret;

	ret = lcd_command(i2c, 0x08); /* display off while configuring */
	if (ret) return ret;

	ret = lcd_clear(i2c);
	if (ret) return ret;

	ret = lcd_command(i2c, HD44780_ENTRY_MODE_SET);
	if (ret) return ret;

	ret = lcd_command(i2c, HD44780_DISPLAY_CONTROL);
	if (ret) return ret;

	return 0;
}

int main(void)
{
	const struct device *i2c = DEVICE_DT_GET(I2C0_NODE);
	bool found_3f = false, found_27 = false;

	printk("I2C LCD (PCF8574 + HD44780) lab starting\n");

	if (!device_is_ready(i2c)) {
		printk("I2C0 device not ready\n");
		return 0;
	}

	/* Scan first: these backpacks ship at either 0x27 (PCF8574) or
	 * 0x3F (PCF8574A) depending on which expander chip is populated.
	 *
	 * Probe with a 1-byte i2c_read(), not i2c_write(): on SR110's I2C
	 * driver a write-based probe (zero-length or 1-byte dummy) is
	 * confirmed to miss real devices on the bus, while i2c_read()
	 * correctly reports NACKs (see Lab 01's scanner for the full
	 * rationale). Reading is also side-effect-free here, whereas a
	 * dummy write would briefly toggle the PCF8574's output pins.
	 */
	{
		uint8_t dummy;

		printk("Scanning I2C0 bus (0x08-0x77)...\n");
		for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
			if (i2c_read(i2c, &dummy, 1, addr) == 0) {
				printk("  found device at 0x%02x\n", addr);
				if (addr == LCD_ADDR_PCF8574A) {
					found_3f = true;
				} else if (addr == LCD_ADDR_PCF8574) {
					found_27 = true;
				}
			}
		}
	}

	if (found_3f) {
		lcd_addr = LCD_ADDR_PCF8574A;
	} else if (found_27) {
		lcd_addr = LCD_ADDR_PCF8574;
	} else {
		printk("Neither 0x3F nor 0x27 found on the bus; "
		       "defaulting to 0x%02x anyway (check wiring/power)\n",
		       lcd_addr);
	}
	printk("Using LCD backpack address 0x%02x\n", lcd_addr);

	if (lcd_init(i2c)) {
		printk("LCD init failed (I2C write error) - check wiring/address\n");
		return 0;
	}

	lcd_set_cursor(i2c, 0, 0);
	lcd_print(i2c, "Hello World!");
	lcd_set_cursor(i2c, 1, 0);
	lcd_print(i2c, "SR110 Zephyr");

	printk("LCD initialized and \"Hello World!\" / \"SR110 Zephyr\" written\n");

	while (1) {
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
