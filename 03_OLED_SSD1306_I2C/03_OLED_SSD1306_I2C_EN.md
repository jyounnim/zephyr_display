# Lab 03: OLED SSD1306 (I2C mode, address auto-detect)

## 1. Overview

Board: **Synaptics SR110** (`sr100_rdk/sr100/m55`), framework: **Zephyr RTOS**.

This lab drives a 0.96" SSD1306 128x64 monochrome OLED in **I2C mode**. SSD1306 I2C modules ship strapped to one of two 7-bit addresses (**0x3C or 0x3D**) depending on the module's SA0 pin, so this lab scans for whichever one is actually present at boot (the same pattern as Lab 01's I2C bus scanner) and uses that.

This lab doesn't use Zephyr's Display/CFB subsystem or the in-tree `solomon,ssd1306` driver - it talks to the SSD1306 with **raw I2C**, the same approach Lab 02 used for the PCF8574 LCD backpack (see section 2 for why).

This lab has been **fully verified on real hardware**.

> **Display spec summary**
> | Item | Detail |
> |---|---|
> | Controller | Solomon Systech **SSD1306** |
> | Resolution | 128 x 64 pixels, 1-bit (monochrome) |
> | Typical size | 0.96" |
> | Interface | I2C (this lab) or SPI (Lab 05) - same chip supports both |
> | I2C address | `0x3C` or `0x3D` (set by the SA0 strap) |
> | Power | Most modules generate the OLED drive voltage internally via a charge pump (no external boost needed) |

## 2. Why Raw I2C Instead of Zephyr's Standard Driver

Zephyr's `solomon,ssd1306` driver bakes the I2C address into the devicetree node at build time (`reg = <0x3C>;`). Since this lab's whole point is to scan for whichever address is actually connected at boot, a static devicetree binding doesn't fit here - so no SSD1306 node is declared in the overlay at all, and `main.c` issues I2C commands/data directly.

## 3. SSD1306 I2C Protocol Summary

Every I2C transaction with an SSD1306 starts with a **control byte**:

| Control byte | Meaning |
|---|---|
| `0x00` | The following byte(s) are a **command** |
| `0x40` | The following byte(s) are **pixel data** (GDDRAM) |

Commands go out one at a time (control byte + 1 command byte). The whole 1024-byte framebuffer goes out as a single **contiguous 1025-byte buffer through one `i2c_write()`** call (control byte prepended). Splitting the control byte and framebuffer into separate messages via `i2c_transfer()` produced a screen full of noise on this platform's I2C driver (an unwanted STOP apparently gets inserted between messages) - so a single contiguous buffer is used instead.

## 4. Wiring

| Signal | Role | SR110 connection |
|---|---|---|
| VCC | Power | 3.3V (see section 5) |
| GND | Ground | GND |
| SDA | Data | I2C0 SDA (pin group `i2c0_ms_sda`) |
| SCL | Clock | I2C0 SCL (pin group `i2c0_ms_scl`) |
| RST (only on modules with this pin) | Hardware reset | gpioa 4 (SoC GPIO4, **J25 pin 5**) |

Most low-cost SSD1306 modules (common on AliExpress etc.) only expose 4 pins (VCC/GND/SDA/SCL) with no RST pin at all - if that's your module, ignore the RST row above. This reuses the same I2C0 bus as Labs 01/02.

> ✅ **Some modules need the RST pin**: the software init sequence (section 8) alone doesn't always fully reset the SSD1306 controller's internal state - some modules show scattered dot noise right after boot that never clears without a low-then-high pulse on RST. `main.c`'s `oled_hw_reset()` handles this.
>
> **Why GPIO4**: on SR110, reusing a GPIO just because it "looks unused" on the schematic can be risky (some pins physically share a pad with JTAG or debug-module functions). GPIO4 was chosen after checking `sr100_pinctrl.dtsi` directly and confirming it shares its pad only with an unused camera VSYNC signal and an unused UART flow-control signal - no JTAG/debug-module neighbors - and it's confirmed working on real hardware.
>
> If your module has no RST pin (the common 4-pin case), set `OLED_USE_HW_RESET` to `0` in `main.c`.

## 5. Power Note (see Lab 01's Doc)

> ✅ **Confirmed on real hardware**: powering the module from the board header's 3.3V rail can cause I2C writes to fail outright - the address scan may succeed while later init commands fail. Switching to an external 3.3V supply fixes this. When attaching external I2C/SPI devices anywhere in this series, **default to external power from the start**. See Lab 01's "Power Supply Notes" section for the full background.

## 6. Devicetree Overlay

```dts
&i2c0_ms_scl {
    bias-pull-up;
};

&i2c0_ms_sda {
    bias-pull-up;
};

&i2c0 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_STANDARD>;
    pinctrl-0 = <&i2c0_ms_scl &i2c0_ms_sda>;
    pinctrl-names = "default";
};
```

Identical to Lab 02's overlay - the only difference is that this lab declares no SSD1306 child node (see section 2).

## 7. Address Auto-Detection

```c
static const uint8_t oled_addr_candidates[] = { 0x3C, 0x3D };

static bool ssd1306_find_address(const struct device *bus, uint8_t *addr_out)
{
    for (size_t i = 0; i < ARRAY_SIZE(oled_addr_candidates); i++) {
        uint8_t addr = oled_addr_candidates[i];
        if (i2c_probe_addr(bus, addr)) {   /* 1-byte read probe, same as Lab 01 */
            *addr_out = addr;
            return true;
        }
    }
    return false;
}
```

- Tries 0x3C first, then 0x3D (SSD1306 can only be at one of these two, so there's no need to scan the full 0x08-0x77 range like Lab 01 does).
- Uses the same **1-byte `i2c_read()`** probe established in Lab 01.
- The found address is used for every subsequent command/data write - a runtime variable instead of a fixed devicetree `reg`.

> ✅ **Power-on settling time**: probing before the SSD1306's own power-on reset (POR) finishes can miss a module that's genuinely connected. `main()` waits `k_sleep(K_MSEC(100))` at startup, and retries each candidate address up to 3 times with a short gap between attempts.

## 8. Init Sequence

Standard 128x64 SSD1306 init sequence (internal charge pump enabled, since most breakout modules have no external Vcc supply for the panel):

| Command | Value | Meaning |
|---|---|---|
| `0xAE` | - | Display off |
| `0xD5` | `0x80` | Display clock divide ratio |
| `0xA8` | `0x3F` (63) | Multiplex ratio = height-1 |
| `0xD3` | `0x00` | Display offset |
| `0x40` | - | Start line = 0 |
| `0x8D` | `0x14` | Charge pump enable |
| `0x20` | `0x00` | Memory addressing mode = horizontal |
| `0xA1` | - | Segment remap (column 127 = SEG0) |
| `0xC8` | - | COM output scan direction remapped |
| `0xDA` | `0x12` | COM pins hardware config (for 128x64) |
| `0x81` | `0xCF` | Contrast |
| `0xD9` | `0xF1` | Pre-charge period |
| `0xDB` | `0x40` | VCOMH deselect level |
| `0xA4` | - | Resume to RAM content display |
| `0xA6` | - | Normal (not inverted) display |
| `0xAF` | - | Display on |

If the image comes out flipped (some modules have the glass bonded upside-down), try swapping `0xA1`↔`0xA0` (segment remap) and `0xC8`↔`0xC0` (COM scan direction).

## 9. Build & Run

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\03_OLED_SSD1306_I2C\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <path to openocd> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

This lab only uses I2C0 and never reassigns any UART pins, so the console can stay on the board default (UART1, GPIO23=TX/GPIO24=RX via the J25 header, external USB-TTL adapter, **230400bps 8N1**).

### Expected Serial Output

```
=== OLED SSD1306 (I2C, SR110) ===
Scanning for SSD1306 at 0x3C / 0x3D...
  found device at 0x3D
SSD1306 initialized at 0x3D, "Hello World!" written
```

The OLED should show `Hello World!` on the first line and `Addr 0x3D` on the third line (page 2).

## 10. Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| `No SSD1306 found at 0x3C or 0x3D` | Check SDA/SCL wiring and pull-ups. **If powered from the board rail, switch to external 3.3V and retry** (section 5) |
| `I2C0 device not ready` | The overlay isn't actually being applied - check that the overlay filename matches the west board target |
| Display turns on but shows nothing | Confirm `ssd1306_update()` returns without error; confirm the column/page address range (`0x21`/`0x22`) matches the panel's actual resolution (128x64) |
| Init completes with no error but the screen stays in dot-noise | If your module has an RST pin, check the wiring (section 4). Defaults are `OLED_USE_HW_RESET=1`, `OLED_RST_PIN=4` |
| Screen is flipped top/bottom or left/right | See the last paragraph of section 8 - try swapping the `0xA1`/`0xC8` polarity |
| Text is garbled or misplaced | Check `fb_draw_char`'s page/column math, or characters not present in the font table |

**Lab 03 fully verified** - address auto-detection, power, framebuffer transfer method, and RST pin handling have all been confirmed on real hardware.

## 11. File Layout

```
03_OLED_SSD1306_I2C/
├── 03_OLED_SSD1306_I2C_KR.md
├── 03_OLED_SSD1306_I2C_EN.md
└── lab/
    ├── CMakeLists.txt
    ├── prj.conf
    ├── sample.yaml
    ├── boards/
    │   └── sr100_rdk_sr100_m55.overlay
    └── src/
        └── main.c
```

## 12. Next

Lab 04 (`04_SPI_basics`) moves to the SPI0 bus - SPI0 shares pins with the board's default console UART, so the approach changes quite a bit. See that lab's doc for details.
