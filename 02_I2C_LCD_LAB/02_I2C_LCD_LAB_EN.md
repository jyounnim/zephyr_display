# Lab 02: I2C LCD (PCF8574 + HD44780, "LiquidCrystal-I2C" style)

## 1. Overview

Board: **Synaptics SR110** (`sr100_rdk/sr100/m55`), framework: **Zephyr RTOS**.

> **SR110 porting note (2026-09-01)**: originally written for the ESP32-S3-DevKitC-1. The PCF8574 bit map (section 3) and HD44780 init sequence (section 6) still apply regardless of platform, but the wiring (section 4), the devicetree overlay, and the boot-time bus-scan probing method have all changed for SR110 - as established in Lab 01 (`01_I2C_bus_scanner`), SR110 needs a **1-byte `i2c_read()`** probe rather than a zero-length/1-byte-dummy `i2c_write()`.

This lab drives a common 16x2 HD44780-compatible character LCD sitting behind a **PCF8574(A) I2C GPIO expander** backpack (the "LCM1602 IIC" module). It reimplements what the Arduino `LiquidCrystal_I2C` library does, but as raw I2C on Zephyr (no Zephyr Display/CFB subsystem involved).

- The PCF8574 takes the 8-bit value it receives over I2C and drives it straight out onto 8 GPIO pins (P0-P7).
- Those 8 pins are wired to the HD44780's RS/RW/EN, a backlight transistor, and the 4-bit data bus (D4-D7).

In other words, this isn't "I2C to the LCD" - it's **"I2C to a GPIO expander, which then bit-bangs the parallel LCD protocol behind it."**

> **Display spec summary**
> | Item | Detail |
> |---|---|
> | LCD controller | Hitachi **HD44780**-compatible (or a clone) |
> | Resolution | 16 x 2 characters (character cells, not arbitrary dot graphics) |
> | I2C expander | **PCF8574** or **PCF8574A** (only the address differs) |
> | Interface | I2C via the backpack - a bare HD44780 alone is 4/8-bit parallel |
> | Common name | "LCM1602 IIC", "1602 I2C LCD" |
> | Supply voltage | Varies by module (3.3V or 5V) - see section 5 |

> ⚠️ **Caution (confirmed on real hardware)**: the common **LCM1602** module needs **5V power to operate correctly**. At 3.3V, the display shows nothing or is extremely faint. See section 5 for wiring/level-shifter guidance.

## 2. Address: 0x27 vs 0x3F

Depending on which expander chip is populated on the backpack, the I2C address differs.

| Populated chip | Default I2C address |
| --- | --- |
| PCF8574 | 0x27 |
| PCF8574A | 0x3F |

The module used for this lab is **0x3F (PCF8574A)**. The firmware scans the I2C bus at boot and uses 0x3F if found, falling back to 0x27 (and defaulting to 0x3F with a warning if neither is found).

## 3. PCF8574 bit map (standard LiquidCrystal_I2C wiring)

This is the de-facto wiring shared by essentially every "LCM1602 IIC" backpack and its Arduino library forks.

| PCF8574 pin | Connected to | Notes |
| --- | --- | --- |
| P0 | LCD RS | 0 = command, 1 = data |
| P1 | LCD RW | Always driven 0 in this lab (write-only, busy flag never read) |
| P2 | LCD EN | Must be pulsed to latch the nibble |
| P3 | Backlight transistor gate | 1 = backlight on |
| P4-P7 | LCD D4-D7 | 4-bit data bus, upper nibble sent first |

If your particular module wires this differently (some low-cost clones are reported to), the symptom shows up as garbled characters or wrong cursor position - not as an I2C communication failure (the ACK/scan can still look fine). See the troubleshooting table in section 8.

## 4. Wiring

| Signal | Synaptics SR110 | Backpack |
| --- | --- | --- |
| VCC | see section 5 | VCC |
| GND | GND | GND |
| SDA | I2C0 SDA (pin group `i2c0_ms_sda`) | SDA |
| SCL | I2C0 SCL (pin group `i2c0_ms_scl`) | SCL |

This reuses the same I2C0 bus as Lab 01 - all external I2C wiring in this series is standardized on I2C0. SR110's I2C0 ships `status = "disabled"` with no default pinctrl, so the overlay just needs to turn it on (no need for ESP32-S3's "redeclare the board default under the same name" trick). See Lab 01's doc for the full background.

## 5. Power / signal-level caution (important)

> ✅ **Confirmed on real hardware (LCM1602)**: the commonly-used **LCM1602 (16x2 HD44780 + PCF8574 backpack)** module needs **VCC supplied at 5V to operate correctly**. At 3.3V, only the backlight lights up with no characters at all, or characters are extremely faint - confirmed on real hardware, not something the contrast trimmer fixes. If you're using an LCM1602, skip step 1 below (trying 3.3V first) and wire it for external 5V (step 2) from the start. A level shifter on SDA/SCL turned out not to be strictly required on real hardware - see below.

This backpack + LCD combination is usually **designed around 5V**:

- The PCF8574's own SDA/SCL pull-up resistors are typically already on the module, tied to VCC. Supplying 5V to VCC means the I2C bus idles high at 5V.
- SR110's GPIOs are also **3.3V-only** and are not rated for 5V input. Connecting a 5V bus directly to SR110 GPIOs risks long-term GPIO damage.
- The LCD's own contrast circuit is also usually tuned around 5V, so running it at 3.3V can leave characters looking very faint even after adjusting the contrast trimmer.

**Recommended order**:

1. **If your module isn't an LCM1602**, try running it at 3.3V first (VCC -> the board's 3V3 pin) and adjust the contrast trimmer to see if characters become visible. Many HD44780 panels do work at 3.3V.
2. If nothing shows up at 3.3V, or only the backlight lights up with no characters (**this is the confirmed case for LCM1602**), the panel needs 5V. In that case:
   - **Note**: the SR110 RDK's GPIO headers have no 5V pin at all (see the "Power Supply Notes" section in Lab 01's doc - the headers only expose 1.8V/3.3V). Supply VCC from a **separate external 5V source** (bench supply, USB power bank, etc.), not the board.
   - Tie the external supply's GND to the board's GND.

> ✅ **Confirmed on real hardware (works without a level shifter)**: supplying VCC from an external 5V source while wiring **SDA/SCL directly to SR110's GPIOs with no level shifter** has been confirmed to work correctly. That said, this remains **out-of-spec use of SR110's GPIOs** (rated 3.3V-only, not guaranteed for 5V input) - the PCF8574 backpack's pull-up resistors sit on VCC (5V), so the bus idles high near 5V in theory, and the GPIO tolerating that is a margin in the silicon, not a guaranteed spec. **It works right now, but this is a trade-off that leaves some long-term GPIO wear risk on the table.** Routing SDA/SCL through a bidirectional logic-level shifter remains the by-the-book, fully-rated approach if you want to eliminate that risk entirely.

The lab's code/overlay behave identically regardless of which power option is chosen (power wiring isn't something software can see).

## 6. HD44780 initialization sequence

Since RW is tied low and the busy flag is never read, initialization and each subsequent command simply wait out the datasheet's required delay instead (Hitachi HD44780U datasheet, Figure 24, "4-Bit Interface" procedure):

1. Wait >= 40 ms after power-up (the code uses a safe 50 ms)
2. Send nibble `0x3`, wait 5 ms
3. Send nibble `0x3`, wait 150 us
4. Send nibble `0x3`, wait 150 us
5. Send nibble `0x2` (switch to 4-bit mode), wait 150 us
6. From here on, normal 2-nibble (high, then low) byte commands:
   - Function Set `0x28` (4-bit bus, 2 lines, 5x8 font)
   - Display Off `0x08` (display off while configuring)
   - Clear Display `0x01` (wait 2 ms - a slow HD44780 command)
   - Entry Mode Set `0x06` (increment cursor, no display shift)
   - Display Control `0x0C` (display on, cursor/blink off)

## 7. Build & Run

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\02_I2C_LCD_LAB\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <path to openocd> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

This lab only uses I2C0, so the console can stay on the board default (UART1, GPIO23=TX/GPIO24=RX via the J25 header, external USB-TTL adapter, **230400bps 8N1**).

### Expected serial output

```
I2C LCD (PCF8574 + HD44780) lab starting
Scanning I2C0 bus (0x08-0x77)...
  found device at 0x3f
Using LCD backpack address 0x3f
LCD initialized and "Hello World!" / "SR110 Zephyr" written
```

The LCD itself should show `Hello World!` on the first line and `SR110 Zephyr` on the second.

## 8. Troubleshooting

| Symptom | Likely cause | Check / fix |
| --- | --- | --- |
| Neither 0x27 nor 0x3F shows up in the scan | Wiring (including SDA/SCL swapped), or no power | Re-check wiring; review the full scan output in the console log (external USB-TTL, 230400bps 8N1) |
| Scan succeeds but nothing shows on screen | If the backlight is on but no characters appear, this is usually a contrast issue | Adjust the backpack's trimmer potentiometer (not a driver bug) |
| Backlight doesn't light up either | Power (VCC/GND) issue, or a 5V-only module being run at 3.3V | See the power guide in section 5 |
| Characters stay faint / only readable from an angle even at max trimmer setting | Running at VDD=3.3V - HD44780 panels typically need V0 about 4-5V below VDD for good contrast, which 3.3V can't provide regardless of trimmer setting | Move to 5V VDD with a level shifter for SDA/SCL, per section 5 |
| Characters are garbled or show up in the wrong position | The bit map in section 3 doesn't match this particular module, or EN pulse timing is off | Re-check the bit map if using a different manufacturer's module |
| Only the first character is garbled, rest are fine | Initialization timing too tight | Increase the delays in `lcd_init()` and retry |
| The I2C write itself fails (ret != 0) | Wiring/power, or two I2C slaves sharing the same address | Check the `i2c_write` return value and the scan log |
| Nothing shows up in the scan even though a device is really wired | The probe reverted to `i2c_write` | SR110 needs a **1-byte `i2c_read()`** probe to work correctly (see Lab 01, section 5) |

## 9. File Layout

```
02_I2C_LCD_LAB/
├── 02_I2C_LCD_LAB_EN.md
├── 02_I2C_LCD_LAB_KR.md
└── lab/
    ├── CMakeLists.txt
    ├── sample.yaml
    ├── prj.conf
    ├── boards/
    │   └── sr100_rdk_sr100_m55.overlay
    └── src/
        └── main.c
```

## 10. Next

Lab 03 (`03_OLED_SSD1306_I2C`) connects an OLED to the same I2C0 bus - this time a pixel-addressable graphic display instead of a character LCD.
