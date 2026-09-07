# Lab 01: I2C Bus Scanner — Zephyr (Synaptics SR110, sr100_rdk/sr100/m55)

> **SR110 porting note (2026-09-01)**: originally written for the ESP32-S3-DevKitC-1. The I2C concept explanations below still apply regardless of platform, but **the wiring pins, the devicetree overlay, and the probing method (read vs. write) have all changed for SR110** - the probing method in particular is the exact opposite of the ESP32-S3 version, so please re-read that section.

An example that scans I2C0 once from a dedicated thread at boot, finds
every device that answers with an ACK, and prints the result as an
`i2cdetect`-style grid. Use it to check which address a new sensor or
display module shows up at once it's wired to the board. It's reused
throughout this lab series - starting with Lab 2 (I2C LCD) and continuing
later in the OLED/Nokia 5110/color TFT labs - as a quick wiring check.

## File Layout

```
Zephyr_display/
└── 01_I2C_bus_scanner/
    ├── lab/
    │   ├── src/
    │   │   └── main.c              # scanner logic (comments in English)
    │   ├── boards/
    │   │   └── sr100_rdk_sr100_m55.overlay   # overlay enabling I2C0
    │   ├── CMakeLists.txt
    │   ├── prj.conf
    │   └── sample.yaml
    └── 01_I2C_bus_scanner_EN.md     # this document
```

## I2C Bus Concepts

| Concept | Description |
|---|---|
| Shared bus | Multiple devices hang in parallel off the same two lines, SDA/SCL |
| Address | Each device has a 7-bit address (in the 0x08-0x77 range) - this is what makes scanning possible |
| ACK/NACK | When the master sends an address, only the device using that address answers with an ACK; if nobody's there, it's NACKed |
| Scanning principle | There's no standard "who's at this address?" command, but sweeping through **a zero-length transaction that only sends the address and nothing else**, and checking for an ACK, reveals whether any device is present at that address |

## Wiring

I2C's two signal lines are named **SDA** (Serial **DA**ta) and **SCL** (Serial **CL**ock).

> ⚠️ **SCK/SCLK are SPI terms.** I2C's clock line is always called **SCL** - easy to confuse since some module silkscreens print "SCK" or "CLK" instead, but on an I2C module (usually 4 pins: VCC/GND/SDA/SCL), that pin is SCL.

| Signal | Role | SR110 connection (per this lab's overlay) |
|---|---|---|
| VCC | Power | 3.3V |
| GND | Ground | GND |
| **SDA** | Data | I2C0 SDA (pin group `i2c0_ms_sda`) |
| **SCL** | Clock (SPI's SCK equivalent) | I2C0 SCL (pin group `i2c0_ms_scl`) |

ESP32-S3's GPIO matrix let you freely assign any GPIO number via a macro like `pinmux = <I2C0_SDA_GPIO8>`. SR110 has no such macro - the SoC package fixes each pin's function, and I2C0's pin group is already named `i2c0_ms_scl` / `i2c0_ms_sda` (no suffix - the `_b` suffix is reserved for I2C1's alternate pin group). Check the Synaptics Platform Guide / board silkscreen for which physical header pin these correspond to.

```dts
&i2c0_ms_scl {
    bias-pull-up;
};
&i2c0_ms_sda {
    bias-pull-up;
};
```

## Power Supply Notes (applies to this whole lab series)

> ✅ **Confirmed on real hardware (2026-09-02)**: in this lab (01, I2C0 bus scanner), **powering an external I2C device from the board header's 3.3V rail caused the scanner to miss some devices.** Switching to an external power supply fixed it. So the schematic-based concern below isn't just theoretical - it's a **real, reproduced problem**. When attaching external I2C/SPI devices anywhere in this series, **try an external power supply first** rather than the board rail.
>
> **Further confirmed (Lab 03, 2026-09-02)**: the same root cause can also show up not as "not found in the scan" but as **an outright I2C write error once the scan has already succeeded and init commands start going out.** So a successful scan does not rule out a power problem - if writes start failing with no obvious code/wiring cause, suspect power before anything else.
>
> **Confirmed against the schematic** (SC950-C01116-01 RevE, sheet 2 "POWER TREE"): the SR110 RDK's 20-pin GPIO headers (J24/J25) only expose **1.8V and 3.3V rails - there is no 5V pin at all.** Each of these rails comes from a single `NCP167`-family LDO (rated up to 700mA), and that same LDO is **shared with onboard loads** - PSRAM, the GPIO expander, the camera (CSI), an M.2 WiFi/BT module, the IMU/ALS sensor, DMICs, and more. So "700mA rated" does not mean "700mA available for your external device" - as confirmed above, the actual current/voltage stability available at the header pins is more limited than it looks on paper.
>
> Given that:
> - **Any external module that needs 5V (e.g. Lab 02's LCM1602) has no board pin to draw it from - always use a separate external 5V supply** (bench supply, USB power bank + cable, etc.).
> - **Even 3.3V modules have been confirmed to drop out intermittently, or not be detected at all, when powered from the board rail.** If a scan is flaky, try switching to external power before suspecting wiring or addresses.
> - This can get worse if you wire up several of this series' displays (Labs 05/07/08) at once from the board's own rails, or if the board has an M.2 WiFi/BT module or camera populated.
> - Whenever you use an external supply, **always tie its GND to the board's GND** - without a shared reference, signal levels themselves become unreliable.

## How It Works

1. A dedicated thread (`scan_tid`) defined with `K_THREAD_DEFINE` starts automatically at boot and runs the I2C0 scan (`main()` does nothing and returns immediately)
2. For each address (0x08-0x77), attempts **a 1-byte read** and treats a successful ACK as evidence the device is present
3. After the scan finishes, prints how many devices were found and the address map
4. Scans once and the thread exits (no repeat)

## Probing Method — On SR110, a 1-Byte Read, Not a Write (the Opposite of ESP32-S3)

The original ESP32-S3 version probed with **a zero-length write**, matching Zephyr's own official sample (`samples/drivers/i2c/i2c_scanner`), specifically to avoid **Zephyr GitHub issue #45008** ("esp32: i2c_read() error was returned successfully at the bus nack") - ESP32-family I2C drivers can't fully be trusted to detect a NACK correctly in `i2c_read()`.

**On SR110 (the `snps,designware-i2c` driver), the situation is confirmed to be the exact opposite on real hardware**: neither a zero-length write nor a 1-byte dummy write reliably finds devices that are actually present, while **a 1-byte `i2c_read()` correctly detects the ACK/NACK**. This port therefore flips the probing method entirely.

```c
static bool i2c_probe_addr(const struct device *bus, uint8_t addr)
{
    uint8_t dummy;
    int ret = i2c_read(bus, &dummy, 1, addr);
    return (ret == 0);
}
```

> ⚠️ If you ever port this scanner to yet another SoC, don't assume either direction transfers over - re-verify on real hardware. This lab is itself a live example of "the same problem, opposite fix, depending on the platform."

## Devicetree — Enabling I2C0

SR110's I2C0 ships with `status = "disabled"` and **no default pinctrl at all**, so it must be turned on and wired in the overlay (unlike ESP32-S3, there's no "redeclare the board's default node under the same name to override it" trick needed here - SR110 simply has no default to override).

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

I2C0's pin group (`i2c0_ms_scl`/`i2c0_ms_sda`) sits on its own LPS-domain mux slot, entirely separate from the pin group shared by SPI0/UART0/UART1 - so unlike Labs 04/05/07/08, there's no console conflict here.

## Build

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\01_I2C_bus_scanner\lab\
```

## Flash & Console

```bash
python srsdk_tools/openocd_flash.py --openocd <path to openocd> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

This lab only touches I2C0 and never reassigns any UART pins, so the console can stay on the board's default (UART1, GPIO23=TX/GPIO24=RX via the J25 header, external USB-TTL adapter, **230400bps 8N1**).

### Expected Output

```
=== I2C Bus Scanner (SR110) ===

Scanning I2C0...
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- 3c -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
70: -- -- -- -- -- -- -- --
Scan complete on I2C0: 1 device(s) found
```

(The example above is with a single SSD1306 OLED wired at 0x3C - Lab 2 is where you'll actually get into this state.)

## Things to Notice

- This scanner is a **diagnostic tool meant to be reused across the whole `Zephyr_display` project** - whenever a later lab hits a "device not responding" issue, make it a habit to run this scanner first and confirm the address actually shows up.
- Needing opposite probing methods on ESP32-S3 (zero-length write) versus SR110 (1-byte read) is a good example of "**the same problem can have opposite answers depending on the platform/driver**" - if you port this to yet another SoC, don't reuse this choice blindly; re-verify on real hardware.
- An unexpected address in the scan result is itself useful information - it could mean a wiring mistake (a different device got connected), or a module using an address different from what you expected (some modules change address via a pin jumper).

## Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| **Some devices drop out intermittently or aren't detected at all (confirmed on real hardware)** | Check whether the external device is powered from the board header's 3.3V rail - **switch to an external power supply and retry**. See "Power Supply Notes" above; this is more likely than a wiring or code issue |
| `[I2C0] device not ready` | The overlay isn't actually being applied - check that the overlay filename (`sr100_rdk_sr100_m55.overlay`) matches the west board target |
| No address shows up at all | Check SDA/SCL wiring and pull-ups, confirm `pinctrl-0` actually resolved to `i2c0_ms_scl`/`i2c0_ms_sda` via `west build -t devicetree` |
| Specific addresses consistently missed, especially ones that used to work with a write probe | Check whether the probing method has reverted to write - SR110 requires a **1-byte read** probe (see the probing section above) |
| Build error (can't find `i2c0_ms_scl`/`i2c0_ms_sda`) | `sr100_pinctrl.dtsi` isn't included - confirm the board.dts loaded correctly and the west target is `sr100_rdk/sr100/m55` |

## Next

Lab 2 (`02_I2C_LCD_LAB`) puts a real I2C LCD on top of the wiring this scanner confirmed.
