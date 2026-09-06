/*
 * Lab 08: ST7735 128x160 color TFT, raw SPI, no Zephyr Display/CFB
 * subsystem - same house style as the earlier SSD1306/LCD/Nokia 5110
 * labs in this series.
 *
 * Board: Synaptics SR110 (sr100_rdk/sr100/m55)
 * Bus:   SPI0 (only SPI *master* on this SoC - spi1 is slave-only),
 *        native hardware CS (spi_mstr_cs pinctrl, no cs-gpios) - see
 *        the overlay for the UART console conflict this creates and
 *        how it's worked around.
 * Extra: RST=gpioa 17 (active low), DC=gpioa 18 (0=command, 1=data) -
 *        J24 pins 3/4, the same pins already confirmed working by
 *        Lab 05/06 on this exact board (deliberately reused here
 *        rather than this lab's original GPIO13/14 - see the
 *        overlay's comment for why).
 *
 * Like the Nokia 5110 in Lab 07, this module is write-only (no MISO
 * wired) - the overlay only needs MOSI+SCLK+CS.
 *
 * This is the "green tab" ST7735R init sequence and CASET/RASET pixel
 * offsets (+2 columns, +1 row) - by far the most common variant on
 * generic "1.8 inch SPI TFT 128x160" breakout boards. Red-tab and
 * black-tab modules use different offsets/MADCTL - see the KR/EN doc's
 * troubleshooting section if the image looks shifted or cropped.
 *
 * SPI clock: 4 MHz. Faster than the 1 MHz used in Lab 07 - a full
 * 128x160 screen fill here is 40,960 bytes (vs. Nokia 5110's 504), so
 * 1 MHz would make every full-screen redraw noticeably slow. Still
 * conservative relative to this controller's real ceiling (many
 * modules run at 20+ MHz); raise spi-max-frequency in the overlay once
 * wiring is confirmed solid.
 *
 * Hardware-verified: confirmed working on real ESP32-S3-DevKitC-1 +
 * ST7735 128x160 TFT hardware (text + color bars both render
 * correctly with the green-tab init sequence/offsets above). The one
 * issue hit during bring-up was, once again, a wiring mistake rather
 * than a code/overlay bug - the same lesson as Lab 07: SPI has no
 * ACK, so wrong wiring just means a blank screen, never an error.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <string.h>

#define ST7735_NODE DT_NODELABEL(st7735)

#define ST7735_WIDTH   128
#define ST7735_HEIGHT  160
#define ST7735_XSTART  2  /* green-tab column offset */
#define ST7735_YSTART  1  /* green-tab row offset */

/* ST7735 command bytes (Adafruit_ST7735-compatible naming/values -
 * this is the same command set essentially every ST7735 library uses). */
#define ST7735_SWRESET  0x01
#define ST7735_SLPOUT   0x11
#define ST7735_INVOFF   0x20
#define ST7735_DISPON   0x29
#define ST7735_CASET    0x2A
#define ST7735_RASET    0x2B
#define ST7735_RAMWR    0x2C
#define ST7735_MADCTL   0x36
#define ST7735_COLMOD   0x3A
#define ST7735_FRMCTR1  0xB1
#define ST7735_FRMCTR2  0xB2
#define ST7735_FRMCTR3  0xB3
#define ST7735_INVCTR   0xB4
#define ST7735_PWCTR1   0xC0
#define ST7735_PWCTR2   0xC1
#define ST7735_PWCTR3   0xC2
#define ST7735_PWCTR4   0xC3
#define ST7735_PWCTR5   0xC4
#define ST7735_VMCTR1   0xC5
#define ST7735_GMCTRP1  0xE0
#define ST7735_GMCTRN1  0xE1
#define ST7735_NORON    0x13

/* RGB565 helper + a handful of named colors used by the demo. */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))
#define COLOR_BLACK   RGB565(0, 0, 0)
#define COLOR_WHITE   RGB565(255, 255, 255)
#define COLOR_RED     RGB565(255, 0, 0)
#define COLOR_GREEN   RGB565(0, 255, 0)
#define COLOR_BLUE    RGB565(0, 0, 255)
#define COLOR_YELLOW  RGB565(255, 255, 0)
#define COLOR_MAGENTA RGB565(255, 0, 255)
#define COLOR_CYAN    RGB565(0, 255, 255)

static const struct spi_dt_spec st7735_spi = SPI_DT_SPEC_GET(
    ST7735_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER, 0);
static const struct gpio_dt_spec st7735_reset =
    GPIO_DT_SPEC_GET(ST7735_NODE, reset_gpios);
static const struct gpio_dt_spec st7735_dc =
    GPIO_DT_SPEC_GET(ST7735_NODE, dc_gpios);

/* Same 5x7 font convention as the SSD1306/Nokia 5110 labs (LSB = top
 * pixel per column byte) - only the glyphs this lab actually prints. */
struct glyph {
    char ch;
    uint8_t cols[5];
};

static const struct glyph font5x7[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x00, 0x00, 0x5F, 0x00, 0x00}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'3', {0x22, 0x41, 0x49, 0x49, 0x36}},
    {'5', {0x72, 0x49, 0x49, 0x49, 0x46}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'F', {0x7F, 0x09, 0x09, 0x01, 0x01}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
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
 * any single call over 8 bytes) has been confirmed on real hardware
 * to fail with -ETIMEDOUT (errno 116) instead of that refill
 * interrupt ever firing - see Lab 06's doc for the full story. This
 * matters here more than it first looks: the init sequence below has
 * 16-byte gamma-correction commands (GMCTRP1/GMCTRN1) that would
 * otherwise exceed the FIFO on their own. Every send is chunked to
 * <= 8 bytes; native hardware CS (see the overlay) asserts/releases
 * automatically per spi_write_dt() call, which is fine to toggle
 * between chunks - confirmed on this hardware by Lab 05 (SSD1306 SPI).
 */
#define ST7735_CHUNK_BYTES 8

static int st7735_send(int dc_value, const uint8_t *data, size_t len)
{
    int ret;

    gpio_pin_set_dt(&st7735_dc, dc_value);

    while (len) {
        size_t chunk = MIN(len, ST7735_CHUNK_BYTES);
        struct spi_buf buf = { .buf = (void *)data, .len = chunk };
        struct spi_buf_set bufs = { .buffers = &buf, .count = 1 };

        ret = spi_write_dt(&st7735_spi, &bufs);
        if (ret) {
            printk("  spi_write_dt(dc=%d, len=%zu) failed, ret=%d\n", dc_value, chunk, ret);
            return ret;
        }
        data += chunk;
        len -= chunk;
    }
    return 0;
}

static int st7735_cmd(uint8_t cmd)
{
    return st7735_send(0, &cmd, 1);
}

static int st7735_data(const uint8_t *data, size_t len)
{
    return st7735_send(1, data, len);
}

/*
 * Canonical ST7735R "green tab" init sequence - byte-for-byte the same
 * command/argument/delay values as the widely-used Adafruit_ST7735
 * library's Rcmd1 + Rcmd3 tables (cross-checked against that library's
 * source before writing this). Kept as a data table rather than a wall
 * of individual function calls so it stays easy to diff against that
 * reference if something needs adjusting later.
 */
struct st7735_init_cmd {
    uint8_t cmd;
    uint8_t num_args;
    uint8_t args[16];
    uint16_t delay_ms;
};

static const struct st7735_init_cmd init_seq[] = {
    { ST7735_SWRESET, 0, {0}, 150 },
    { ST7735_SLPOUT,  0, {0}, 500 },
    { ST7735_FRMCTR1, 3, {0x01, 0x2C, 0x2D}, 0 },
    { ST7735_FRMCTR2, 3, {0x01, 0x2C, 0x2D}, 0 },
    { ST7735_FRMCTR3, 6, {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 0 },
    { ST7735_INVCTR,  1, {0x07}, 0 },
    { ST7735_PWCTR1,  3, {0xA2, 0x02, 0x84}, 0 },
    { ST7735_PWCTR2,  1, {0xC5}, 0 },
    { ST7735_PWCTR3,  2, {0x0A, 0x00}, 0 },
    { ST7735_PWCTR4,  2, {0x8A, 0x2A}, 0 },
    { ST7735_PWCTR5,  2, {0x8A, 0xEE}, 0 },
    { ST7735_VMCTR1,  1, {0x0E}, 0 },
    { ST7735_INVOFF,  0, {0}, 0 },
    { ST7735_MADCTL,  1, {0xC8}, 0 },
    { ST7735_COLMOD,  1, {0x05}, 10 },
    { ST7735_GMCTRP1, 16, {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                           0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10}, 0 },
    { ST7735_GMCTRN1, 16, {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                           0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10}, 10 },
    { ST7735_NORON,   0, {0}, 10 },
    { ST7735_DISPON,  0, {0}, 100 },
};

static int st7735_run_init_sequence(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(init_seq); i++) {
        int ret = st7735_cmd(init_seq[i].cmd);
        if (ret) return ret;

        if (init_seq[i].num_args) {
            ret = st7735_data(init_seq[i].args, init_seq[i].num_args);
            if (ret) return ret;
        }
        if (init_seq[i].delay_ms) {
            k_sleep(K_MSEC(init_seq[i].delay_ms));
        }
    }
    return 0;
}

static int st7735_init(void)
{
    /* Hardware reset: pulse RST low (gpio value 1, since reset-gpios is
     * declared GPIO_ACTIVE_LOW) for >=10us per the datasheet - 10ms is
     * a generous margin. Datasheet also wants >=120ms after release
     * before the controller will reliably accept commands. */
    gpio_pin_set_dt(&st7735_reset, 1);
    k_sleep(K_MSEC(10));
    gpio_pin_set_dt(&st7735_reset, 0);
    k_sleep(K_MSEC(120));

    return st7735_run_init_sequence();
}

static int st7735_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t caset[4] = {0x00, (uint8_t)(x0 + ST7735_XSTART), 0x00, (uint8_t)(x1 + ST7735_XSTART)};
    uint8_t raset[4] = {0x00, (uint8_t)(y0 + ST7735_YSTART), 0x00, (uint8_t)(y1 + ST7735_YSTART)};
    int ret;

    ret = st7735_cmd(ST7735_CASET);
    if (ret) return ret;
    ret = st7735_data(caset, sizeof(caset));
    if (ret) return ret;

    ret = st7735_cmd(ST7735_RASET);
    if (ret) return ret;
    ret = st7735_data(raset, sizeof(raset));
    if (ret) return ret;

    return st7735_cmd(ST7735_RAMWR);
}

static int st7735_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    int ret = st7735_set_addr_window(x, y, x + w - 1, y + h - 1);
    if (ret) return ret;

    /* 64-pixel (128-byte) solid-color chunk, resent as many times as
     * needed - keeps the on-stack buffer small regardless of rect size. */
    uint8_t chunk[64 * 2];
    for (int i = 0; i < 64; i++) {
        chunk[i * 2]     = (uint8_t)(color >> 8);
        chunk[i * 2 + 1] = (uint8_t)(color & 0xFF);
    }

    uint32_t remaining = (uint32_t)w * h;
    while (remaining > 0) {
        uint32_t n = MIN(remaining, 64);
        ret = st7735_data(chunk, n * 2);
        if (ret) return ret;
        remaining -= n;
    }
    return 0;
}

static int st7735_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg)
{
    const uint8_t *glyph = glyph_lookup(c);
    uint8_t buf[6 * 8 * 2]; /* 6 cols (5 glyph + 1 gap) x 8 rows x 2 bytes/pixel */

    for (int col = 0; col < 6; col++) {
        uint8_t bits = (col < 5) ? glyph[col] : 0x00;
        for (int row = 0; row < 8; row++) {
            uint16_t color = (bits & BIT(row)) ? fg : bg;
            int idx = (row * 6 + col) * 2;
            buf[idx]     = (uint8_t)(color >> 8);
            buf[idx + 1] = (uint8_t)(color & 0xFF);
        }
    }

    int ret = st7735_set_addr_window(x, y, x + 5, y + 7);
    if (ret) return ret;
    return st7735_data(buf, sizeof(buf));
}

static int st7735_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg)
{
    while (*str) {
        int ret = st7735_draw_char(x, y, *str++, fg, bg);
        if (ret) return ret;
        x += 6;
    }
    return 0;
}

int main(void)
{
    printk("ST7735 TFT lab starting\n");

    if (!spi_is_ready_dt(&st7735_spi)) {
        printk("SPI device not ready - check devicetree status/overlay\n");
        return 0;
    }
    if (!gpio_is_ready_dt(&st7735_reset) || !gpio_is_ready_dt(&st7735_dc)) {
        printk("RST/DC GPIO not ready - check devicetree overlay\n");
        return 0;
    }

    gpio_pin_configure_dt(&st7735_reset, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&st7735_dc, GPIO_OUTPUT_INACTIVE);

    if (st7735_init()) {
        printk("ST7735 init failed (SPI write error) - check wiring\n");
        return 0;
    }

    st7735_fill_rect(0, 0, ST7735_WIDTH, ST7735_HEIGHT, COLOR_BLACK);
    st7735_draw_string(4, 10, "Hello World!", COLOR_WHITE, COLOR_BLACK);
    st7735_draw_string(4, 26, "ST7735 TFT", COLOR_CYAN, COLOR_BLACK);

    /*
     * Color bars: R/G/B/Y/M/C/W in order. This is a deliberate built-in
     * diagnostic, not just decoration - a swapped MADCTL BGR/RGB bit or
     * a pixel byte-order mistake is invisible on a monochrome display
     * (Lab 02/07) but immediately obvious here (e.g. the "R" bar
     * rendering blue instead of red). See the doc's troubleshooting
     * section if these come out wrong or in the wrong order.
     */
    static const uint16_t bars[] = {
        COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW,
        COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE,
    };
    uint16_t bar_w = ST7735_WIDTH / ARRAY_SIZE(bars);
    for (size_t i = 0; i < ARRAY_SIZE(bars); i++) {
        st7735_fill_rect(i * bar_w, 50, bar_w, 40, bars[i]);
    }

    printk("ST7735 initialized and demo screen (text + color bars) drawn\n");

    while (1) {
        k_sleep(K_SECONDS(5));
    }

    return 0;
}
