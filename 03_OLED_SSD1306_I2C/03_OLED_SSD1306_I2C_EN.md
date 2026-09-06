# Lab 03: OLED SSD1306 (I2C mode, address auto-detect)

## 1. Overview

Board: **ESP32-S3-DevKitC-1** (`esp32s3_devkitc/esp32s3/procpu`), framework: **Zephyr RTOS**.

This lab drives a 0.96" SSD1306 128x64 monochrome OLED in **I2C mode**. SSD1306 I2C modules ship strapped to one of two 7-bit addresses (**0x3C or 0x3D**) depending on the module's SA0 pin, so this lab scans for whichever one is actually present at boot (the same pattern as Lab 01's I2C bus scanner) and uses that.

This lab doesn't use Zephyr's Display/CFB subsystem or the in-tree `solomon,ssd1306` driver - it talks to the SSD1306 with **raw I2C**, the same approach Lab 02 used for the PCF8574 LCD backpack.

<img width="285" height="306" alt="image" src="https://github.com/user-attachments/assets/35db3950-ee37-47ff-b6fc-c70702fb5a0c" />


> ℹ️ **This is a fresh implementation, separate from the earlier parked SSD1306 I2C lab** recorded in the roadmap's "parked" section. It's based on a reference implementation that reached full, real-hardware verification on a different board (Synaptics SR110), and carries over two concrete bug fixes discovered there (see sections 3 and 8 below). There's no guarantee this automatically resolves the parked lab's hardware mystery (white noise), but it ships with fixes the earlier version didn't have, so it's worth another try.

## 2. Why raw I2C instead of Zephyr's standard driver

Zephyr's `solomon,ssd1306` driver bakes the I2C address into the devicetree node at build time (`reg = <0x3C>;`). Since this lab's whole point is to scan for whichever address is actually connected at boot, a static devicetree binding doesn't fit here - so no SSD1306 node is declared in the overlay at all, and `main.c` issues I2C commands/data directly.

## 3. SSD1306 I2C protocol summary (+ an important note on framebuffer transfers)

Every I2C transaction with an SSD1306 starts with a **control byte**:

| Control byte | Meaning |
|---|---|
| `0x00` | The following byte(s) are a **command** |
| `0x40` | The following byte(s) are **pixel data** (GDDRAM) |

Commands go out one at a time (control byte + 1 command byte = 2 bytes). The whole 1024-byte framebuffer goes out as a single `i2c_write()` transaction of 1025 bytes (control byte prepended).

> ✅ **Real-hardware issue found in the reference implementation (SR110)**: the first version there tried to avoid copying the framebuffer by using `i2c_transfer()` with two messages (control byte, then payload, with `I2C_MSG_STOP` only on the second) chained without a STOP in between. On that board this produced a screen full of noise instead of text, even though the calls returned success - the working theory is that the I2C controller inserted an unwanted STOP/restart between the two messages, so the SSD1306 saw a lone control-byte transaction followed by a second transaction whose first byte (the first framebuffer byte) got misread as a new control byte instead of pixel data, shifting everything after it by one.
>
> This lab was written from the start to send the control byte and framebuffer as **one contiguous buffer through a single `i2c_write()`**. Whether this specific bug reproduces on ESP32-S3's I2C driver hasn't been confirmed either way, but it removes any dependency on multi-message transfer semantics, so it's kept as the safer default regardless.

## 4. Wiring

| Signal | Role | ESP32-S3-DevKitC-1 |
|---|---|---|
| VCC | Power | 3.3V |
| GND | Ground | GND |
| SDA | Data | GPIO8 (I2C0 SDA, series-wide pin) |
| SCL | Clock | GPIO9 (I2C0 SCL, series-wide pin) |
| RST (only on modules that expose this pin) | Hardware reset | **Wire directly to the board's own RST/EN pin (recommended, hardware-confirmed)** - see section 8-1. A GPIO4 software-toggle option also exists in the code as a fallback |

Most low-cost SSD1306 modules (common on AliExpress etc.) only expose 4 pins (VCC/GND/SDA/SCL) with no RST pin at all - if that's your module, ignore the RST row above. This reuses the same I2C0 bus (GPIO8/9) as Lab 01/02.

## 5. Power/pull-up caution (a lesson from the parked lab)

The earlier parked SSD1306 I2C lab's hardware debugging left **weak/missing I2C pull-ups** as the leading suspect for its white-noise symptom. Since the SPI version of the same controller (in a separate workspace) was confirmed working, the controller/init logic itself is probably not at fault if something goes wrong here - the I2C wiring is more likely. If scanning fails or the screen shows noise, try these before touching the code:

1. Add **external 4.7k-ohm pull-ups** on SDA/SCL to 3.3V (don't rely on the ESP32-S3's weak internal ones alone)
2. Power the module from an **external 3.3V supply** instead of the board header
3. Keep breadboard wiring as short as possible and re-check for loose jumper contacts

## 6. Devicetree overlay

```dts
&pinctrl {
    i2c0_default: i2c0_default {
        group1 {
            pinmux = <I2C0_SDA_GPIO8>, <I2C0_SCL_GPIO9>;
            bias-pull-up;
            drive-open-drain;
            output-high;
        };
    };
};

&i2c0 {
    status = "okay";
    clock-frequency = <100000>;
    pinctrl-0 = <&i2c0_default>;
    pinctrl-names = "default";
};
```

Same GPIO8/9 override pattern as Lab 01/02 - the only difference is there's no child node for the OLED (see section 2).

## 7. How address auto-detection works

```c
static const uint8_t oled_addr_candidates[] = { 0x3C, 0x3D };

static bool i2c_probe_addr(const struct device *bus, uint8_t addr)
{
    uint8_t dummy = 0;
    int ret = i2c_write(bus, &dummy, 1, addr);   /* same write-probe as Lab 01/02 */
    return (ret == 0);
}
```

- Tries 0x3C first, then 0x3D on failure (only two possible addresses, so unlike Lab 01 there's no need to sweep 0x08-0x77).
- Uses the same **1-byte write probe** already confirmed working on ESP32-S3's I2C driver in Lab 01/02. (The SR110 reference used a read-based probe instead, but that was specific to a quirk in that board's I2C driver, so this lab keeps this series' existing write-probe convention.)
- Retries each address up to 3 times with a 20ms gap, and waits 100ms at the start of `main()` - probing before the module's power-on reset has settled can cause a spurious scan failure.
- Whichever address responds is used for every subsequent command/data write.

## 8. Initialization sequence

The standard 128x64 SSD1306 init sequence (using the internal charge pump, since most breakout modules have no external Vcc supply for the panel - `0x8D, 0x14` turns that on):

| Command | Value | Meaning |
|---|---|---|
| `0xAE` | - | Display off |
| `0xD5` | `0x80` | Set display clock divide ratio/osc freq |
| `0xA8` | `0x3F` (63) | Set multiplex ratio = height - 1 |
| `0xD3` | `0x00` | Set display offset |
| `0x40` | - | Set display start line = 0 |
| `0x8D` | `0x14` | Enable charge pump |
| `0x20` | `0x00` | Memory addressing mode = horizontal |
| `0xA1` | - | Segment remap (column 127 = SEG0) |
| `0xC8` | - | COM output scan direction, remapped |
| `0xDA` | `0x12` | COM pins hardware configuration (for 128x64) |
| `0x81` | `0xCF` | Contrast control |
| `0xD9` | `0xF1` | Pre-charge period |
| `0xDB` | `0x40` | VCOMH deselect level |
| `0xA4` | - | Resume to RAM content display |
| `0xA6` | - | Normal (not inverted) display |
| `0xAF` | - | Display on |

If the image looks flipped (some modules mount the glass panel upside-down), try swapping `0xA1`<->`0xA0` (segment remap) and `0xC8`<->`0xC0` (COM scan direction).

### 8-1. Hardware reset (RST) - two options, board RST/EN tie confirmed on real hardware

Some modules need their RST pin pulsed low-then-high right after boot, or the panel stays in a dot-noise state despite a clean software init. There are two ways to handle this.

**Option A - wire the module's RST directly to the board's own RST/EN pin (recommended, hardware-confirmed 2026-09-02)**

Instead of a GPIO, connect the SSD1306 module's RST pin straight to the dev board's own reset pin (usually silkscreened `RST` or `EN` - most ESP32 DevKitC-style boards expose one). Wired this way, the OLED physically resets together with the board itself (on power-up, a reset-button press, or a reflash), so:

- No software toggling is needed at all (leave `OLED_USE_HW_RESET` at 0 and use the code as-is)
- No extra GPIO is consumed
- The MCU and the OLED are always reset in sync - the whole boot sequence starts from a clean reset on both

This was confirmed working on real hardware. When using an SSD1306 (or similar controller) module with an RST pin in this series, **this is the recommended first choice**.

**Option B - GPIO software toggle (fallback, not yet validated on ESP32-S3)**

For cases where option A isn't wireable (e.g. the board's RST/EN pin is already committed to something else), `main.c` also keeps a GPIO-toggle path. This was hardware-validated on the SR110 reference implementation, but **not yet on ESP32-S3**:

```c
#define OLED_USE_HW_RESET 0  /* only set to 1 if your module has an RST pin and option A isn't possible */
...
#define OLED_RST_GPIO_NODE DT_NODELABEL(gpio0)
#define OLED_RST_PIN       4
```

> 🛑 **Pin choice caution**: don't pick a reset GPIO by schematic net name alone - avoid the ESP32-S3's strapping pins (GPIO0/3/45/46) and the USB-JTAG pins (GPIO19/20). GPIO4 was chosen here to match the RST pin already used by Lab 07 in this series.

## 9. Build & Run

```bash
west build -p always -b esp32s3_devkitc/esp32s3/procpu 03_OLED_SSD1306_I2C/lab
west flash
west espressif monitor
```

### Expected serial output

```
=== OLED SSD1306 (I2C, ESP32-S3) ===
Scanning for SSD1306 at 0x3C / 0x3D...
  found device at 0x3c
SSD1306 initialized at 0x3C, "Hello World!" written
```

The OLED itself should show `Hello World!` on the first line and `Addr 0x3C` (or `0x3D`) on the third line (page 2).

## 10. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `No SSD1306 found at 0x3C or 0x3D` | Check SDA/SCL wiring. **Try the pull-up/power guide in section 5 first** - this is the same failure mode that was the leading suspect in the earlier parked lab's white-noise issue |
| `I2C0 device not ready` | The overlay isn't actually being applied - check that the overlay filename matches the west board target |
| Address found but `SSD1306 init failed` during init | The console logs exactly which command failed as `ssd1306 cmd 0xXX failed, ret=N` - check the `ret` value (a negative errno) and the failing command byte. Suspect the power issue in section 5 first |
| Display turns on but shows nothing | Check whether `ssd1306_update()` returned success; verify the column/page address range (`0x21`/`0x22`) matches the panel's actual resolution (128x64) |
| Address scan and init both succeed but the screen shows only noise | See section 3 - this is already implemented with a single-buffer `i2c_write()`, so the specific bug found in the reference implementation is already addressed. If it still reproduces, check the pull-ups/wiring in section 5 |
| Display is flipped vertically or horizontally | See the last paragraph of section 8 - try swapping the `0xA1`/`0xC8` polarity |
| Garbled characters or wrong position | Check `fb_draw_char`'s page/column math, or whether you're printing a character missing from the font table (section 7) |
| Init completes with no error but the screen stays in dot-noise | Could be an RST-pin module - see section 8-1. **Try wiring the module's RST straight to the board's RST/EN pin first** (option A, hardware-confirmed); if that's not possible, try option B (`OLED_USE_HW_RESET` with GPIO4 wired up) |

## 11. File Layout

```
03_OLED_SSD1306_I2C/
├── 03_OLED_SSD1306_I2C_KR.md
├── 03_OLED_SSD1306_I2C_EN.md
└── lab/
    ├── CMakeLists.txt
    ├── README.rst
    ├── prj.conf
    ├── sample.yaml
    ├── boards/
    │   └── esp32s3_devkitc_esp32s3_procpu.overlay
    └── src/
        └── main.c
```
