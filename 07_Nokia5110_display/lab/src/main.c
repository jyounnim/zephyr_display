/*
 * Lab 07: Nokia 5110 (PCD8544) monochrome LCD, raw SPI, no Zephyr
 * Display/CFB subsystem - same house style as the earlier SSD1306/LCD
 * labs in this series.
 *
 * Board: Synaptics SR110 (sr100_rdk/sr100/m55)
 * Bus:   SPI0 (only SPI *master* on this SoC - spi1 is slave-only),
 *        native hardware CS (spi_mstr_cs pinctrl, no cs-gpios) - see
 *        the overlay for the UART console conflict this creates and
 *        how it's worked around.
 * Extra: RST=gpioa 19 (active low), DC=gpioa 20 (0=command, 1=data) -
 *        J24 pins 5/6, confirmed against the SR110 RDK schematic.
 *
 * The Nokia 5110 module has no MISO line at all (it's a write-only
 * display), so unlike Lab 03's loopback test, this overlay only wires
 * up MOSI + SCLK + CS - there's nothing to read back.
 *
 * SPI clock is deliberately conservative here (1 MHz, well under the
 * PCD8544's ~4 MHz datasheet ceiling) for the same reason noted in the
 * earlier SSD1306 lab: breadboard jumper wiring has enough parasitic
 * capacitance that a slower clock buys real reliability margin. Once
 * the wiring is confirmed solid, spi-max-frequency in the overlay can
 * be raised.
 *
 * Hardware-verified: confirmed working on real ESP32-S3-DevKitC-1 +
 * Nokia 5110 hardware with the default Vop (0xB0) - no contrast
 * tuning needed. The one real issue hit during bring-up was a stale
 * wiring assumption (RST/DC/CE/DIN/CLK had been moved to different
 * GPIOs from an earlier, unrelated test and not re-checked against
 * this lab's wiring table) - a reminder that with SPI, a write "ret
 * == 0" only means the bytes went out on the wire, never that the
 * chip on the other end actually received them (see Lab 03).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <string.h>

#define PCD8544_NODE DT_NODELABEL(pcd8544)

#define LCD_WIDTH   84
#define LCD_HEIGHT  48
#define LCD_ROWS    (LCD_HEIGHT / 8) /* 6 pages of 8 vertical pixels each */

/* PCD8544 commands (basic instruction set unless noted). */
#define PCD8544_FUNCTION_SET       0x20
#define PCD8544_FUNC_EXTENDED      0x01
#define PCD8544_DISPLAY_NORMAL     0x0C
#define PCD8544_SET_X_ADDR         0x80
#define PCD8544_SET_Y_ADDR         0x40
/* Extended-instruction-set-only commands: */
#define PCD8544_SET_TEMP_COEFF0    0x04
#define PCD8544_SET_BIAS_MODE1_48  0x14

/* Set-Vop (contrast) command byte - already includes the 0x80 opcode
 * bit, so this is sent to pcd8544_cmd() as-is, not further OR'd.
 *
 * This is the single most module-dependent value in the whole init
 * sequence - PCD8544 breakout boards vary quite a bit here. 0xB0
 * matches several widely-used reference implementations; if the
 * screen comes up blank, all-dark, or a low-contrast checkerboard,
 * adjust this first (there's no physical contrast trimmer on these
 * modules, unlike the HD44780 LCD in Lab 02 - it's purely a software
 * value). Valid range is 0x80-0xFF.
 */
#define PCD8544_SET_VOP_DEFAULT 0xB0

static const struct spi_dt_spec pcd8544_spi = SPI_DT_SPEC_GET(
    PCD8544_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER, 0);
static const struct gpio_dt_spec pcd8544_reset =
    GPIO_DT_SPEC_GET(PCD8544_NODE, reset_gpios);
static const struct gpio_dt_spec pcd8544_dc =
    GPIO_DT_SPEC_GET(PCD8544_NODE, dc_gpios);

static uint8_t framebuffer[LCD_WIDTH * LCD_ROWS];

/* Minimal 5x7 font - only the glyphs this lab actually prints.
 * Columns are stored LSB-first bottom-to-top per the PCD8544's
 * vertical-byte-per-column page format; a blank 6th column is the
 * inter-character gap. */
struct glyph {
    char ch;
    uint8_t cols[5];
};

static const struct glyph font5x7[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x00, 0x00, 0x5F, 0x00, 0x00}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'5', {0x72, 0x49, 0x49, 0x49, 0x46}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'a', {0x20, 0x54, 0x54, 0x54, 0x78}},
    {'d', {0x38, 0x44, 0x44, 0x48, 0x7F}},
    {'e', {0x38, 0x54, 0x54, 0x54, 0x18}},
    {'i', {0x00, 0x44, 0x7D, 0x40, 0x00}},
    {'k', {0x7F, 0x10, 0x28, 0x44, 0x00}},
    {'l', {0x00, 0x41, 0x7F, 0x40, 0x00}},
    {'o', {0x38, 0x44, 0x44, 0x44, 0x38}},
    {'r', {0x7C, 0x08, 0x04, 0x04, 0x08}},
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

/* SR110: SPI0's hardware FIFO is only 8 bytes deep (confirmed via
 * sr100_m55.dtsi's `fifo-depth = <8>;`). A single spi_write_dt() call
 * needing a mid-transfer TX-FIFO-refill interrupt to complete (i.e.
 * any single call over 8 bytes) fails with -ETIMEDOUT (errno 116) -
 * this bit both Lab 06 and Lab 08 before they added chunking, and
 * this lab's pcd8544_update() sends the *entire* 504-byte framebuffer
 * in one call, so it needs the same fix. Native hardware CS (see the
 * overlay) asserts/releases automatically per spi_write_dt() call,
 * which is fine to toggle between chunks - confirmed on this hardware
 * by Lab 05 and Lab 08.
 */
#define PCD8544_CHUNK_BYTES 8

static int pcd8544_send(int dc_value, const uint8_t *data, size_t len)
{
    int ret;

    gpio_pin_set_dt(&pcd8544_dc, dc_value);

    while (len) {
        size_t chunk = MIN(len, PCD8544_CHUNK_BYTES);
        struct spi_buf buf = { .buf = (void *)data, .len = chunk };
        struct spi_buf_set bufs = { .buffers = &buf, .count = 1 };

        ret = spi_write_dt(&pcd8544_spi, &bufs);
        if (ret) {
            printk("  spi_write_dt(dc=%d, len=%zu) failed, ret=%d\n", dc_value, chunk, ret);
            return ret;
        }
        data += chunk;
        len -= chunk;
    }
    return 0;
}

static int pcd8544_cmd(uint8_t cmd)
{
    return pcd8544_send(0, &cmd, 1);
}

static int pcd8544_data(const uint8_t *data, size_t len)
{
    return pcd8544_send(1, data, len);
}

static int pcd8544_init(void)
{
    int ret;

    /* Hardware reset: pulse RST low (gpio value 1, since reset-gpios
     * is declared GPIO_ACTIVE_LOW in the overlay) for >100ns per the
     * datasheet - a millisecond is a generous, easy margin. */
    gpio_pin_set_dt(&pcd8544_reset, 1);
    k_sleep(K_MSEC(1));
    gpio_pin_set_dt(&pcd8544_reset, 0);
    k_sleep(K_MSEC(1));

    /* Extended instruction set: contrast (Vop), temp coefficient, bias. */
    ret = pcd8544_cmd(PCD8544_FUNCTION_SET | PCD8544_FUNC_EXTENDED);
    if (ret) return ret;
    ret = pcd8544_cmd(PCD8544_SET_VOP_DEFAULT);
    if (ret) return ret;
    ret = pcd8544_cmd(PCD8544_SET_TEMP_COEFF0);
    if (ret) return ret;
    ret = pcd8544_cmd(PCD8544_SET_BIAS_MODE1_48);
    if (ret) return ret;

    /* Back to basic instruction set, normal (non-inverted) display. */
    ret = pcd8544_cmd(PCD8544_FUNCTION_SET);
    if (ret) return ret;
    ret = pcd8544_cmd(PCD8544_DISPLAY_NORMAL);
    if (ret) return ret;

    return 0;
}

static void fb_draw_char(int col, int page, char c)
{
    if (page < 0 || page >= LCD_ROWS) {
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

static int pcd8544_update(void)
{
    int ret = pcd8544_cmd(PCD8544_SET_X_ADDR | 0);
    if (ret) return ret;
    ret = pcd8544_cmd(PCD8544_SET_Y_ADDR | 0);
    if (ret) return ret;

    /* PCD8544's addressing auto-increments X then wraps to the next Y,
     * so the whole 84x6-byte framebuffer can go out as one transfer. */
    return pcd8544_data(framebuffer, sizeof(framebuffer));
}

int main(void)
{
    printk("Nokia 5110 (PCD8544) lab starting\n");

    if (!spi_is_ready_dt(&pcd8544_spi)) {
        printk("SPI device not ready - check devicetree status/overlay\n");
        return 0;
    }
    if (!gpio_is_ready_dt(&pcd8544_reset) || !gpio_is_ready_dt(&pcd8544_dc)) {
        printk("RST/DC GPIO not ready - check devicetree overlay\n");
        return 0;
    }

    gpio_pin_configure_dt(&pcd8544_reset, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&pcd8544_dc, GPIO_OUTPUT_INACTIVE);

    if (pcd8544_init()) {
        printk("PCD8544 init failed (SPI write error) - check wiring\n");
        return 0;
    }

    memset(framebuffer, 0, sizeof(framebuffer));
    fb_draw_string(0, 0, "Hello World!");
    fb_draw_string(0, 1, "Nokia 5110");

    if (pcd8544_update()) {
        printk("PCD8544 framebuffer update failed (SPI write error)\n");
        return 0;
    }

    printk("Nokia 5110 initialized and \"Hello World!\" / \"Nokia 5110\" written\n");

    while (1) {
        k_sleep(K_SECONDS(5));
    }

    return 0;
}
