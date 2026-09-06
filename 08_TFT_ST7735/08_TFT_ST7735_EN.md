# Lab 08: ST7735 128x160 Color TFT — raw SPI

## 1. Overview

Board: **Synaptics SR110** (`sr100_rdk/sr100/m55`), framework: **Zephyr RTOS**.

This is the first **color** display in this series - a **128x160 color TFT based on the ST7735 controller** (commonly sold as an "1.8 inch SPI TFT" module), driven over raw SPI. As with the other raw-SPI labs, this doesn't use Zephyr's Display/CFB subsystem - the application code drives SPI/GPIO directly.

This lab has been **fully verified on real hardware**.

## 2. Requirements

- An ST7735 128x160 color TFT module ("green tab" variant - see section 3 below)
- A Synaptics SR110 board
- An external USB-TTL adapter to observe console output

Like the Nokia 5110 (Lab 07), this module is a **write-only display with no MISO line**, so the overlay only needs MOSI/SCLK/CS.

## 3. Wiring

| Signal | Role | SR110 connection |
|---|---|---|
| VCC | Power | 3.3V (see note below) |
| GND | Ground | GND |
| SCL (SCLK) | Clock | SPI0 CLK (SoC GPIO22, **J25 pin 11**) |
| SDA (MOSI) | Data in | SPI0 MOSI (SoC GPIO23, **J25 pin 14**) |
| CS | Chip select | SPI0 CS, native hardware CS (SoC GPIO21, **J25 pin 12**) |
| RST | Reset (active low) | SoC GPIO17, **J24 pin 3** |
| DC (some modules label it A0/RS) | Data/Command select | SoC GPIO18, **J24 pin 4** |
| LED (BLK) | Backlight | 3.3V (tied high/always-on) |

> **Console note**: turning on SR110's only SPI master (SPI0) disables the board's default console (UART1, GPIO23/24, J25 pins 13/14). This lab's overlay moves the console to UART0's alternate pins (GPIO44/45, **J24 pins 13/14**) - connect an external USB-TTL adapter there.
>
> **Power note**: this TFT typically draws the most backlight current of this series' displays. If wiring this alongside other displays, prefer an external 3.3V supply where practical.

The ST7735 IC itself is mostly 2.8-3.3V logic, so it's a reasonably good voltage match for SR110. That said, some breakout boards have an onboard regulator and accept 5V VIN while still outputting 3.3V logic - check your specific module's spec.

## 4. "Tab Color" — Why It Matters

ST7735 128x160 modules ship with a colored tab sticker ("red"/"green"/"black", etc.) behind the actual glass - a convention marking small offset differences between panel manufacturing batches. Even with the identical ST7735 controller, tab color affects:

- The starting coordinate offset added to CASET/RASET
- The default MADCTL (rotation/color-order) value

This lab is written for the **most common "green tab"** variant (column +2, row +1 offset). If the image is cropped by a few pixels on the top/left, or shows black margin on the opposite side, this offset is almost certainly the mismatch - adjust `ST7735_XSTART`/`ST7735_YSTART` in `main.c` (red-tab modules typically use 0 for both).

## 5. Init Sequence

Cross-checked byte-for-byte against the Adafruit_ST7735 library's "Rcmd1 + Rcmd3" table (ST7735R, green tab) - this exact combination is effectively an industry-standard sequence.

| Command | Args | Meaning |
|---|---|---|
| SWRESET (`0x01`) | - | Software reset, wait 150ms |
| SLPOUT (`0x11`) | - | Sleep out, wait 500ms |
| FRMCTR1-3 (`0xB1-0xB3`) | 3/3/6 bytes | Frame rate control |
| INVCTR (`0xB4`) | 1 byte | Display inversion control |
| PWCTR1-5 (`0xC0-0xC4`) | - | Power control (voltage regulator setup) |
| VMCTR1 (`0xC5`) | 1 byte | VCOM voltage control |
| INVOFF (`0x20`) | - | Display inversion off |
| MADCTL (`0x36`) | `0xC8` | Memory access control - rotation/BGR order (green-tab default) |
| COLMOD (`0x3A`) | `0x05` | Pixel format = 16-bit (RGB565) |
| GMCTRP1/GMCTRN1 (`0xE0`/`0xE1`) | 16 bytes each | Gamma correction tables (positive/negative) |
| NORON (`0x13`) | - | Normal display mode |
| DISPON (`0x29`) | - | Display on |

RST is hardware-reset (low pulse 10ms, then wait 120ms) before this sequence runs.

> **SR110 note**: this lab worked correctly on real hardware right away, thanks to this detailed init sequence covering power control, frame rate, and gamma correction. Elsewhere in this series, a color TFT with only the bare minimum commands (SWRESET/SLPOUT/COLMOD/MADCTL/INVON/NORON/DISPON) completed all SPI writes without error yet showed nothing on screen - so when porting a new color TFT, start with a full init sequence like this one rather than a minimal one.

## 6. Pixel Format and Addressing

- `COLMOD=0x05` → **RGB565** (16 bits/pixel: 5 bits R, 6 bits G, 5 bits B), high byte first
- `CASET`/`RASET` set a (x0,y0)-(x1,y1) rectangle, then `RAMWR` streams data that fills it in row-major order
- This lab doesn't keep a full framebuffer in RAM - Nokia 5110 (Lab 07) is only 84x6=504 bytes, small enough for a static array, but this screen is 128x160x2=40,960 bytes, too large to justify a static buffer, so each draw call (one glyph, one color bar) sets its own address window and streams data on the spot

## 7. Code Structure

- `st7735_send()` / `st7735_cmd()` / `st7735_data()`: set the DC pin, then send via `spi_write_dt()` - chunked to <= 8 bytes because **SR110's SPI0 hardware FIFO is only 8 bytes deep** (a single transfer over 8 bytes fails with `-116`/`-ETIMEDOUT`). The init sequence's 16-byte gamma commands (`GMCTRP1`/`GMCTRN1`) would fail without this chunking.
- `init_seq[]` + `st7735_run_init_sequence()`: the init sequence as a (command, args, delay) table, kept easy to diff against the reference library
- `st7735_set_addr_window()`: sets a rectangle via CASET/RASET/RAMWR
- `st7735_fill_rect()`: fills a rectangle with a solid color (sent in 64-pixel chunks, further split into 8-byte pieces internally)
- `st7735_draw_char()`/`st7735_draw_string()`: draws the 5x7 font pixel-by-pixel, sent as 6x8 blocks
- `main()`: fills the screen black → draws "Hello World!" (white) / "ST7735 TFT" (cyan) → draws **R/G/B/Y/M/C/W color bars**

## 8. Why Color Bars — A Built-In Diagnostic Tool

**The most common bugs on color displays (swapped R/G/B channel order, pixel byte-order mistakes) are completely invisible on a monochrome display.** So instead of stopping at text, this lab always draws R/G/B/Y/M/C/W color bars alongside it - if the "R" bar comes out blue, MADCTL's BGR/RGB bit is flipped; if every color looks subtly off, suspect the pixel data is being sent in the wrong byte order.

## 9. Devicetree

```dts
&spi0 {
    #address-cells = <1>;
    #size-cells = <0>;
    status = "okay";
    pinctrl-0 = <&spi_mstr_mosi &spi_mstr_miso &spi_mstr_clk &spi_mstr_cs>;
    pinctrl-names = "default";

    st7735: st7735@0 {
        compatible = "zds,st7735";
        reg = <0>;
        spi-max-frequency = <4000000>;
        reset-gpios = <&gpioa 17 GPIO_ACTIVE_LOW>;
        dc-gpios = <&gpioa 18 GPIO_ACTIVE_HIGH>;
    };
};

&ns16550_uart1 {
    status = "disabled";
};

&ns16550_uart0 {
    pinctrl-0 = <&uart0_tx_c &uart0_rx_c>;
    pinctrl-names = "default";
    current-speed = <230400>;
    dlf = <2>;
    status = "okay";
};

/ {
    chosen {
        zephyr,console = &ns16550_uart0;
        zephyr,shell-uart = &ns16550_uart0;
    };
};
```

`reset-gpios`/`dc-gpios` match Zephyr's own `mipi-dbi-spi` binding naming, and the GPIO controller is `&gpioa` (a dedicated, unshared 32-pin controller). CS uses native hardware CS (`spi_mstr_cs`) as-is - no separate `cs-gpios` needed, the controller manages CS internally via the `reg` value. `spi_mstr_miso` isn't used by this module but is included to keep pinctrl consistent with this series' other SPI labs.

## 10. Custom Devicetree Binding

```yaml
description: |
  ST7735-based 128x160 color TFT ("green tab" variant), driven over raw
  SPI from application code - no Zephyr Display/CFB subsystem involved.

compatible: "zds,st7735"

include: spi-device.yaml

properties:
  reset-gpios:
    type: phandle-array
    required: true
    description: >
      Reset pin. Active low - pulse low to reset the ST7735 controller.

  dc-gpios:
    type: phandle-array
    required: true
    description: >
      Data/Command select pin. Driven low before a command byte is
      written, high before a data byte is written.
```

## 11. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20.0)

list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(st7735_tft_lab)

target_sources(app PRIVATE src/main.c)
```

Since a custom binding is used, `DTS_ROOT` must be extended before `find_package(Zephyr...)`.

## 12. prj.conf

```
CONFIG_SPI=y
CONFIG_GPIO=y
CONFIG_PRINTK=y
```

## 13. File Layout

```
08_TFT_ST7735/
├── 08_TFT_ST7735_KR.md
├── 08_TFT_ST7735_EN.md
└── lab/
    ├── src/
    │   └── main.c
    ├── boards/
    │   └── sr100_rdk_sr100_m55.overlay
    ├── dts/
    │   └── bindings/
    │       └── display/
    │           └── zds,st7735.yaml
    ├── CMakeLists.txt
    ├── prj.conf
    └── sample.yaml
```

## 14. Build & Run

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\08_TFT_ST7735\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <path to openocd> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

The console has moved to UART0's alternate pins (GPIO44/45) - connect an external USB-TTL adapter to **J24 pins 13/14** and open it at **230400bps 8N1**.

### Expected Serial Output

```
ST7735 TFT lab starting
ST7735 initialized and demo screen (text + color bars) drawn
```

The display should show white `Hello World!` and cyan `ST7735 TFT` on a black background, with seven color bars (red/green/blue/yellow/magenta/cyan/white) below.

## 15. Things to Notice

- **The color bars are this lab's real first troubleshooting tool** - before worrying about whether the text looks right, confirm the bar colors and order match R/G/B/Y/M/C/W exactly
- If the screen edges look cropped or shifted, that's very likely a tab-color (offset) issue (see section 4) - unlike a wiring problem, this has no physical fix, only a software constant to adjust
- Drawing only the needed region on the fly (no framebuffer), rather than the Nokia 5110's "send the whole framebuffer every time" approach, illustrates the point where keeping a full framebuffer in RAM becomes impractical as screen size grows

## 16. Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| `SPI device not ready` / `RST/DC GPIO not ready` | The overlay isn't applied - check that the overlay filename (`sr100_rdk_sr100_m55.overlay`) matches the west board target |
| Screen stays completely white/black/unresponsive | Re-check RST/DC wiring - unlike I2C, SPI has no ACK, so wrong wiring shows up exactly like this, silently, with no error |
| `spi_write_dt(...) failed, ret=-116` | SPI0's 8-byte FIFO limit - confirm `ST7735_CHUNK_BYTES` is 8 or less (already the case) |
| No console output at all | Confirm the external USB-TTL adapter is on **J24 pins 13/14** - the board's default console (J25 pins 13/14) dies once SPI0 is enabled |
| Color bars come out with swapped colors (e.g. "R" looks blue) | MADCTL's BGR/RGB bit doesn't match this module - try toggling the BGR bit in `init_seq[]`'s MADCTL (`0x36`) argument (`0xC8`) |
| Image is cropped on top/left, or has margin on the opposite side | Tab-color offset mismatch - try setting `ST7735_XSTART`/`ST7735_YSTART` to 0 (red/black tab variants) |
| Image is mirrored/flipped | MADCTL's MX/MY bits - adjust the top two bits of `0xC8` |
| Text is garbled or misplaced | Check `st7735_draw_char`'s 6x8 pixel layout, or the font table |
| `'zds,st7735' compatible not found` | Check that `CMakeLists.txt`'s `list(APPEND DTS_ROOT ...)` comes before `find_package(Zephyr...)` |

## 17. Next

Across this series' raw-SPI + custom-binding labs, the pattern has scaled from a monochrome character LCD (Lab 02, I2C) to a monochrome graphic LCD (Lab 07, SPI) to this color TFT (Lab 08, SPI).
