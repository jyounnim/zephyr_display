# Lab 06: TFT ST7789V3 (1.69" 240x280, raw SPI)

## 1. Overview

Board: **Synaptics SR110** (`sr100_rdk/sr100/m55`), framework: **Zephyr RTOS**.

This lab drives a 1.69" 240x280 color TFT panel based on the Sitronix **ST7789V3** controller over SPI. As with the other SPI display labs in this series, it uses **raw SPI** to talk to the controller directly rather than Zephyr's Display/CFB subsystem or the in-tree `sitronix,st7789v` driver.

This lab has been **fully verified on real hardware**.

> ⚠️ **This lab requires a level shifter.** Read section 4 first - the wiring differs from this series' other SPI labs.

## 2. Requirements

- An ST7789V3 1.69" 240x280 color TFT module (3.3V IOVCC)
- A Synaptics SR110 board
- A **bidirectional logic level shifter** (1.8V ↔ 3.3V, 5+ channels - e.g. TXS0108E). See section 4.
- An external USB-TTL adapter to observe console output

## 3. Wiring

| Signal | Role | SR110 side (1.8V) | Level shifter | Display side (3.3V) |
|---|---|---|---|---|
| VCC | Power | - | - | 3.3V |
| GND | Ground | GND | GND (common) | GND |
| SCL/SCLK | SPI clock | SPI0 CLK (SoC GPIO22, **J25 pin 11**) | CH_A ↔ CH_B | SCL |
| SDA/MOSI | SPI data | SPI0 MOSI (SoC GPIO23, **J25 pin 14**) | CH_A ↔ CH_B | SDA |
| CS | Chip select | SPI0 CS, native hardware CS (SoC GPIO21, **J25 pin 12**) | CH_A ↔ CH_B | CS |
| RES/RST | Reset | SoC GPIO17, **J24 pin 3** | CH_A ↔ CH_B | RES |
| DC | Data/Command select | SoC GPIO18, **J24 pin 4** | CH_A ↔ CH_B | DC |
| BLK | Backlight | - | - | 3.3V (direct, bypasses the shifter) |

Connect the shifter's low-voltage side (VCCA) to **1.8V** and the high-voltage side (VCCB) to **3.3V**. If using a TXS0108E, tie **OE to 1.8V (VCCA)**. MISO isn't wired - this panel is write-only.

## 4. Why a Level Shifter Is Needed

SR110's SPI0/GPIO pins are documented on the schematic (SC950-C01116-01 RevE) as a **1.8V I/O domain** (`SR110_VDDIO1P8`) - separate from the 3.3V power pin available on the board headers, the actual signal logic level is 1.8V.

This ST7789V3 module, however, is rated for **3.3V IOVCC**. Applying the typical ST7789-family input-high threshold (VIH = 0.7 x IOVCC):

```
VIH_min = 0.7 x 3.3V = 2.31V
```

SR110's 1.8V output falls short of this threshold. In that state, SPI transfers still report success with no error (SR110 only knows it drove the signal, not whether the receiver actually read it as a logic 1), but the panel never reliably receives its commands, leaving the screen permanently black.

Fixed by running **all five signals - SCLK, MOSI, CS, RST, DC** - through a level shifter from 1.8V to 3.3V.

> **Note**: this series' Lab 05 (SSD1306) and Lab 08 (ST7735) both work fine at 1.8V with no level shifter. This isn't a contradiction - real silicon input thresholds often have margin beyond the datasheet spec, so some parts happen to work below their rated threshold. **When driving a 3.3V-rated SPI display from a 1.8V MCU, default to using a level shifter, and verify on real hardware whether you can skip it - don't assume.**
>
> **If using a TXS0108E**: it's an auto-direction-sensing shifter, not really designed for a continuously-toggling unidirectional signal like SPI SCLK - it can become unreliable above roughly 1-2MHz depending on wiring/parasitic capacitance. This lab is verified at 4MHz, but if you see the same symptom (no error, blank screen) with your own wiring, try lowering the clock further or switching to a dedicated unidirectional buffer (74LVC245, 74AHCT125, etc.) - all five signals here are SR110-to-display only, so a unidirectional buffer is actually the more correct choice.

## 5. Console Note

Turning on SR110's only SPI master (SPI0) disables the board's default console (UART1, GPIO23/24, J25 pins 13/14). This lab's overlay moves the console to UART0's alternate pins (GPIO44/45, **J24 pins 13/14**) - connect an external USB-TTL adapter there.

## 6. Panel RAM Offset

The ST7789 controller's native GRAM is 240x320. This particular module's visible glass is only 240x280, centered in a sub-window of that GRAM - so every addressing window sent to the controller needs a fixed offset added.

This lab uses X offset 0, Y offset 20 by default (a commonly documented value for 1.69" 240x280 ST7789V3 modules). The offsets live in the overlay's `x-offset`/`y-offset` properties - adjust them if the image on your module looks shifted or clipped.

## 7. Devicetree

```dts
&spi0 {
    #address-cells = <1>;
    #size-cells = <0>;
    status = "okay";
    pinctrl-0 = <&spi_mstr_mosi &spi_mstr_miso &spi_mstr_clk &spi_mstr_cs>;
    pinctrl-names = "default";

    st7789v_disp: st7789v@0 {
        compatible = "zds,st7789v";
        reg = <0>;
        spi-max-frequency = <4000000>;
        reset-gpios = <&gpioa 17 GPIO_ACTIVE_LOW>;
        dc-gpios = <&gpioa 18 GPIO_ACTIVE_HIGH>;
        width = <240>;
        height = <280>;
        x-offset = <0>;
        y-offset = <20>;
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

`main.c` pulls the SPI spec and RST/DC GPIO specs straight from this node via `SPI_DT_SPEC_GET()` / `GPIO_DT_SPEC_GET()`. CS uses native hardware CS via `spi_mstr_cs` pinctrl alone - no separate `cs-gpios` needed.

> ✅ **SPI0's 8-byte FIFO limit**: the SoC devicetree (`sr100_m55.dtsi`) specifies SPI0/SPI1's hardware FIFO depth as `fifo-depth = <8>;` - 8 bytes. A single `spi_write_dt()` call needing a mid-transfer FIFO-refill interrupt to complete (i.e. anything over 8 bytes) fails with `-116` (`-ETIMEDOUT`), because that refill interrupt doesn't fire reliably on this platform. Every SPI transfer in `main.c` is chunked to 8 bytes to avoid this - a hardware constraint that applies to any lab using this SoC's SPI0 with raw SPI, regardless of CS mechanism, worth remembering for future ports.

## 8. Init Sequence

Uses a full init sequence covering porch control, gate control, VCOM, power control, and gamma correction - cross-checked against ESPHome's `st7789v` component, Bodmer's TFT_eSPI library, and Adafruit's Raspberry Pi fbtft driver, which all agree on this sequence/these values.

| Command | Args | Meaning |
|---|---|---|
| SWRESET (`0x01`) | - | Software reset, wait 150ms |
| SLPOUT (`0x11`) | - | Sleep out, wait 255ms |
| COLMOD (`0x3A`) | `0x55` | Pixel format = 16-bit (RGB565) |
| PORCTRL (`0xB2`) | 5 bytes | Porch setting |
| GCTRL (`0xB7`) | 1 byte | Gate control |
| VCOMS (`0xBB`) | 1 byte | VCOM voltage |
| LCMCTRL (`0xC0`) | 1 byte | LCM control |
| VDVVRHEN (`0xC2`) | 2 bytes | VDV/VRH enable |
| VRHS (`0xC3`) | 1 byte | VRH setting |
| VDVS (`0xC4`) | 1 byte | VDV setting |
| FRCTRL2 (`0xC6`) | 1 byte | Frame rate control |
| PWCTRL1 (`0xD0`) | 2 bytes | Power control |
| MADCTL (`0x36`) | `0x00` | Rotation/RGB order |
| INVON (`0x21`) | - | Display inversion on |
| PVGAMCTRL/NVGAMCTRL (`0xE0`/`0xE1`) | 14 bytes each | Gamma correction tables |
| NORON (`0x13`) | - | Normal display mode |
| DISPON (`0x29`) | - | Display on |

If the image comes out flipped or rotated (some modules have the glass bonded in a different orientation), adjust the `MADCTL`/`INVON` values - see section 10 for the specific bits.

## 9. Build & Run

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\06_TFT_ST7789V3\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <path to openocd> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

The console has moved to UART0's alternate pins (`uart0_tx_c`/`uart0_rx_c` = GPIO44/45) - connect an external USB-TTL adapter to **J24 pins 13/14** and open it at **230400bps 8N1**.

### Expected Serial Output

```
=== TFT ST7789V3 (SPI, 240x280) ===
ST7789V3 initialized, color bars + "Hello World!" drawn
```

The panel should show `Hello World!` at the top, with red/green/blue/yellow/cyan/magenta/white color bars below it on a black background.

## 10. Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| Screen stays completely black, log completes with no error | **The most common cause when wired directly without a level shifter.** See section 4 - confirm all five signals (SCLK/MOSI/CS/RST/DC) go through a 1.8V-to-3.3V level shifter |
| Same symptom even with a level shifter installed | If using a TXS0108E, the SPI clock may be too fast - try lowering `spi-max-frequency` to 1MHz or below, or switch to a unidirectional buffer (74LVC245, etc.) |
| No console output at all | Confirm the external USB-TTL adapter is on **J24 pins 13/14** - the board's default console (J25 pins 13/14) dies once SPI0 is enabled |
| `spi_write_dt(...) failed, ret=-116` | SPI0's 8-byte FIFO limit - confirm `ST7789_CHUNK_BYTES` is 8 or less (already the case) |
| Display comes on but the image is shifted or clipped | See section 6 - adjust `x-offset`/`y-offset` to match your module's datasheet |
| Colors look inverted (e.g. black background shows as white) | Try changing `main.c`'s `ST7789_INVON` (`0x21`) call to `0x20` (`INVOFF`) |
| Red/blue channels swapped (RGB vs BGR) | Try toggling bit 3 (`0x08`) of `st7789_init()`'s `madctl` value (`0x00`) |
| Image is mirrored/rotated relative to how the module is mounted | Try different `madctl` bit combinations (`0x00`/`0x60`/`0xA0`/`0xC0`, etc.) |
| Screen fills in but text is missing or garbled | Check for characters not in the `font5x7` table, or matching text/background colors |

**Lab 06 fully verified (2026-09-04)** - the level-shifter requirement (1.8V↔3.3V), SPI0 bus (native CS), RST/DC (GPIO17/18), SPI0's 8-byte FIFO chunking, and the expanded init sequence have all been confirmed on real hardware.

## 11. File Layout

```
06_TFT_ST7789V3/
├── 06_TFT_ST7789V3_KR.md
├── 06_TFT_ST7789V3_EN.md
└── lab/
    ├── CMakeLists.txt
    ├── prj.conf
    ├── sample.yaml
    ├── boards/
    │   └── sr100_rdk_sr100_m55.overlay
    ├── dts/
    │   └── bindings/
    │       └── display/
    │           └── zds,st7789v.yaml
    └── src/
        └── main.c
```

## 12. Next

Lab 07 (`07_Nokia5110_display`) continues the series.
