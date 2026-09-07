# Lab 07: Nokia 5110 (PCD8544) Monochrome LCD — 84x48, raw SPI

## 1. Overview

Board: **Synaptics SR110** (`sr100_rdk/sr100/m55`), framework: **Zephyr RTOS**.

This lab drives an **84x48 monochrome LCD based on the PCD8544 controller** - the "Nokia 5110" module, famous from old Nokia feature phones - over raw SPI. As with the other labs in this series, it doesn't use Zephyr's Display/CFB subsystem; the application code drives SPI/GPIO directly.

This module has **no MISO line at all** (a write-only display) - so unlike Lab 04 (SPI loopback), this overlay only needs MOSI/SCLK/CS.

This lab has been **fully verified on real hardware**.

> **Display spec summary**
> | Item | Detail |
> |---|---|
> | Controller | Philips (NXP) **PCD8544** |
> | Resolution | 84 x 48 pixels, 1-bit (monochrome) |
> | Origin | Nokia 5110/3310-era feature phone displays (early 2000s) |
> | Interface | SPI only (write-only, no MISO) |
> | Contrast | No physical trimmer - software Vop command only |

## 2. Requirements

- A Nokia 5110 (PCD8544) LCD module (typically 8 pins: RST, CE, DC, DIN, CLK, VCC, LIGHT, GND)
- A Synaptics SR110 board
- An external USB-TTL adapter to observe console output

## 3. Wiring

| Signal | Role | SR110 connection |
|---|---|---|
| VCC | Power | 3.3V |
| GND | Ground | GND |
| RST | Reset (active low) | gpioa 19 (SoC GPIO19, **J24 pin 5**) |
| CE (CS) | Chip select | SPI0 CS (pin group `spi_mstr_cs`, native hardware CS) |
| DC | Data/Command select | gpioa 20 (SoC GPIO20, **J24 pin 6**) |
| DIN (MOSI) | Data in | SPI0 MOSI (pin group `spi_mstr_mosi`) |
| CLK (SCLK) | Clock | SPI0 CLK (pin group `spi_mstr_clk`) |
| LIGHT (BL) | Backlight | 3.3V or switched to GND (optional) |

> ✅ **Why GPIO19/20**: these are the SoC's I2S_DO/I2S_DI pins, unused by this lab (no I2S here), reused as plain GPIO (confirmed against the schematic, SC950-C01116-01 RevE sheet 10 - **J24 pins 5/6**). On this project, assuming a pin is safe to reuse just because it "looks unused" on the schematic has caused real problems elsewhere (some pins physically share a pad with JTAG/debug-module functions) - GPIO19/20 were chosen after confirming via `sr100_pinctrl.dtsi` that they don't overlap with any JTAG/debug-module function. They're different pins from Lab 05 (GPIO17/18, J24 pins 3/4), so both labs can be wired up on the same board without conflict.
>
> **SPI0/console conflict**: turning on SR110's only SPI master (SPI0) disables the board's default console (UART1, GPIO23/24, J25 pins 13/14). This lab's overlay moves the console to UART0's alternate pins (`uart0_tx_c`/`uart0_rx_c` = GPIO44/45, **J24 pins 13/14**), same as Lab 04. Connect an external USB-TTL adapter there.
>
> **Power note**: see Lab 01's "Power Supply Notes" section - the board header's 3.3V rail is shared with onboard components. If wiring this alongside other displays, prefer an external 3.3V supply where practical.

The Nokia 5110/PCD8544 module came out of an actual Nokia phone, so its **native logic level is 2.7-3.3V** - a natural match for SR110's 3.3V-only GPIOs, no level shifter needed. That said, some low-cost breakout boards' onboard regulator/resistor network assumes a 5V input, so check your specific module's silkscreen/listing before feeding VCC 5V. This lab is written **assuming a direct 3.3V connection**.

## 4. PCD8544 Command Set

PCD8544 toggles between a "basic instruction set" and an "extended instruction set" to configure itself.

| Command | Value | Meaning |
|---|---|---|
| Function Set (extended) | `0x21` | Enter extended mode |
| Set Vop (contrast) | `0x80 \| Vop` | Contrast setting - this lab uses the default `0xB0` |
| Temperature Control | `0x04` | Temperature coefficient 0 |
| Bias System | `0x14` | 1:48 bias |
| Function Set (basic) | `0x20` | Return to basic mode |
| Display Control | `0x0C` | Normal (non-inverted) display mode |

**Contrast (Vop) values vary a lot between modules.** The HD44780 LCD (Lab 02) has a physical trimmer you can turn by hand, but PCD8544 has **no trimmer - this Vop command value is the entire software-side contrast control**. If the screen shows nothing (too light) or is entirely black (too dark), adjust `main.c`'s `PCD8544_SET_VOP_DEFAULT` (default `0xB0`) somewhere in the `0x80`-`0xFF` range.

## 5. Addressing and the Framebuffer

- The screen is 84 x 48 pixels = 84 columns x 6 pages (8 pixels per page)
- `0x80|x` sets the X (column) address, `0x40|y` sets the Y (page) address
- After setting the address, streaming data auto-increments X and auto-wraps to the next page at column 84 - so the entire framebuffer (84x6 = 504 bytes) can be written starting at (0,0) in one continuous stream to refresh the whole screen

## 6. Code Structure

- `pcd8544_send()` / `pcd8544_cmd()` / `pcd8544_data()`: set DC to 0 (command) or 1 (data), then send via `spi_write_dt()` - chunked to <= 8 bytes because **SR110's SPI0 hardware FIFO is only 8 bytes deep** (a single transfer over 8 bytes fails with `-116`/`-ETIMEDOUT`). Since this lab sends the entire 504-byte framebuffer in one logical write, it would fail every time without this chunking.
- `pcd8544_init()`: RST pulse → extended commands (Vop/temperature/bias) → back to basic mode → normal display mode
- `framebuffer[84*6]`: holds the whole screen; `fb_draw_char`/`fb_draw_string` fill it in with a 5x7 font, and `pcd8544_update()` sends it all at once (chunked to 8 bytes internally)
- `main()`: confirm SPI/GPIO readiness → initialize → print "Hello World!" / "Nokia 5110" on two lines

## 7. Devicetree

```dts
&spi0 {
    #address-cells = <1>;
    #size-cells = <0>;
    status = "okay";
    pinctrl-0 = <&spi_mstr_mosi &spi_mstr_miso &spi_mstr_clk &spi_mstr_cs>;
    pinctrl-names = "default";

    pcd8544: pcd8544@0 {
        compatible = "zds,pcd8544";
        reg = <0>;
        spi-max-frequency = <1000000>;
        reset-gpios = <&gpioa 19 GPIO_ACTIVE_LOW>;
        dc-gpios = <&gpioa 20 GPIO_ACTIVE_HIGH>;
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

`reset-gpios`/`dc-gpios` match the names used by Zephyr's own `mipi-dbi-spi` display binding - this lab doesn't use that framework (it's a custom `zds,pcd8544` binding instead), but keeping the naming convention aligned with the official binding avoids confusion if this ever gets ported to a real Zephyr display driver later. `spi_mstr_miso` isn't used by this module but is included to keep pinctrl consistent with Lab 05.

## 8. Custom Devicetree Binding

```yaml
description: |
  PCD8544-based Nokia 5110 monochrome LCD (84x48), driven over raw SPI
  from application code - no Zephyr Display/CFB subsystem involved.

compatible: "zds,pcd8544"

include: spi-device.yaml

properties:
  reset-gpios:
    type: phandle-array
    required: true
    description: >
      Reset pin. Active low - pulse low to reset the PCD8544 controller.

  dc-gpios:
    type: phandle-array
    required: true
    description: >
      Data/Command select pin. Driven low before a command byte is
      written, high before a data byte is written.
```

## 9. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20.0)

list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nokia5110_lab)

target_sources(app PRIVATE src/main.c)
```

Same as Lab 04: since a custom binding is used, `DTS_ROOT` must be extended before `find_package(Zephyr...)`.

## 10. prj.conf

```
CONFIG_SPI=y
CONFIG_GPIO=y
CONFIG_PRINTK=y
```

## 11. File Layout

```
07_Nokia5110_display/
├── 07_Nokia5110_display_KR.md
├── 07_Nokia5110_display_EN.md
└── lab/
    ├── src/
    │   └── main.c
    ├── boards/
    │   └── sr100_rdk_sr100_m55.overlay
    ├── dts/
    │   └── bindings/
    │       └── display/
    │           └── zds,pcd8544.yaml
    ├── CMakeLists.txt
    ├── prj.conf
    └── sample.yaml
```

## 12. Build & Run

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\07_Nokia5110_display\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <path to openocd> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

The console has moved to UART0's alternate pins (`uart0_tx_c`/`uart0_rx_c` = GPIO44/45) - connect an external USB-TTL adapter to **J24 pins 13/14** and open it at **230400bps 8N1**.

### Expected Serial Output

```
Nokia 5110 (PCD8544) lab starting
Nokia 5110 initialized and "Hello World!" / "Nokia 5110" written
```

The display should show `Hello World!` on page 1 (top) and `Nokia 5110` on page 2.

## 13. Things to Notice

- **Vop (contrast) is the setting most likely to need adjusting in this lab** - with no physical trimmer, if the screen looks blank, try a few different `PCD8544_SET_VOP_DEFAULT` values before suspecting the wiring.
- Being a write-only display with no MISO line makes a nice contrast with Lab 04 (SPI loopback) - that lab verified "the bus itself is sound," and this one builds a real device's command protocol on top of that assumption.
- Matching `reset-gpios`/`dc-gpios` naming to the official Zephyr binding means the devicetree can be reused almost as-is if this custom driver is ever swapped for a real Zephyr display driver later.

## 14. Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| `SPI device not ready` / `RST/DC GPIO not ready` | The overlay isn't applied - check that the filename matches the board target |
| Screen is entirely blank/white | Vop too low (not enough contrast) - try raising `PCD8544_SET_VOP_DEFAULT` from `0xB0` (e.g. `0xB8`, `0xC0`) |
| Screen is entirely black / checkerboard pattern | Vop too high (too much contrast) - try lowering it, or check whether the RST sequence completed correctly |
| Serial shows an init failure / SPI write error | Re-check the wiring (especially CE/CS, DIN/MOSI, CLK). If powered from the board rail, switch to external 3.3V and retry |
| `spi_write_dt(...) failed, ret=-116` | SPI0's 8-byte FIFO limit - confirm `PCD8544_CHUNK_BYTES` is 8 or less (already the case) |
| Characters are garbled or in the wrong place | Check the 6-pixel spacing logic in `fb_draw_string`, and that the page argument is within 0-5 |
| No console output at all | Confirm the external USB-TTL adapter is on **J24 pins 13/14** - the board's default console (J25 pins 13/14) dies once SPI0 is enabled |
| `'zds,pcd8544' compatible not found` | Check that `CMakeLists.txt`'s `list(APPEND DTS_ROOT ...)` comes before `find_package(Zephyr...)` |

**Lab 07 fully verified** - the SPI0 bus, RST/DC (GPIO19/20), and SPI0's 8-byte FIFO chunking have all been confirmed on real hardware.

## 15. Next

Lab 08 (`08_TFT_ST7735`) moves to a color TFT - reusing this lab's raw SPI + custom binding pattern, plus a color-specific verification technique (color bars).
