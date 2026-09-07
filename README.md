# Zephyr Display Labs — Synaptics SR110 (sr100_rdk/sr100/m55)

A lecture/workshop lab series for bringing up I2C/SPI displays one at a time on the Synaptics **SR110** board (`sr100_rdk/sr100/m55`, Zephyr RTOS). Originally written for the ESP32-S3-DevKitC-1, these labs were ported to SR110 with every platform difference (fixed pinmux, I2C driver probe direction, GPIO I/O voltage domain, etc.) verified on real hardware.

Every lab in this repo has been **confirmed working on real hardware**.

## Labs

| # | Name | Bus | Key topic |
|---|---|---|---|
| [01](01_I2C_bus_scanner/) | I2C Bus Scanner | I2C0 | I2C fundamentals, address scanning, the diagnostic tool reused throughout this series |
| [02](02_I2C_LCD_LAB/) | I2C LCD (PCF8574 + HD44780) | I2C0 | Driving a parallel LCD through a GPIO expander, power/signal-level considerations (5V vs 3.3V) |
| [03](03_OLED_SSD1306_I2C/) | OLED SSD1306 (I2C) | I2C0 | Runtime address auto-detection, raw I2C, hardware reset |
| [04](04_SPI_basics/) | SPI Basics (loopback self-test) | SPI0 | The fundamental I2C-vs-SPI difference, the SPI0/console-UART pin conflict and how to work around it |
| [05](05_OLED_SSD1306_SPI/) | OLED SSD1306 (SPI) | SPI0 | Same chip over SPI, Zephyr's driver-model bus abstraction |
| [06](06_TFT_ST7789V3/) | TFT ST7789V3 (color) | SPI0 | Color TFT, a **1.8V↔3.3V level shifter**, SPI FIFO constraints |
| [07](07_Nokia5110_display/) | Nokia 5110 (PCD8544) | SPI0 | A write-only display, software-only contrast (Vop) |
| [08](08_TFT_ST7735/) | TFT ST7735 (color) | SPI0 | Color-bar verification technique, "tab color" (offset) quirks |

Each lab folder contains Korean (`_KR.md`) and English (`_EN.md`) docs, plus a `lab/` directory ready to build with `west build`.

## Suggested Order

01 → 02 → 03 (I2C) → 04 (SPI basics) → 05 → 06 → 07 → 08 (SPI displays). Each doc's closing "Next" section points to the following lab.

## Key SR110 Lessons (Apply Across All Labs)

Real-hardware-confirmed facts about this board worth knowing before bringing up a new device:

- **Power**: the board header's 3.3V/1.8V rails are shared with onboard components, and can cause intermittent misbehavior or write failures when powering external devices. Default to an external power supply where practical (see Lab 01's doc).
- **I2C probe direction**: on SR110 (`snps,designware-i2c`), a **1-byte read probe** reliably detects devices, while a zero-length/dummy write probe misses real devices - the opposite of ESP32-S3 (see Lab 01's doc).
- **SPI0/console UART conflict**: SR110's only SPI master (SPI0)'s MOSI/MISO/CLK/CS pins physically overlap with the board's default console (UART1) and UART0's default pins. Every lab using SPI0 (04-08) moves the console to UART0's alternate pins (J24 pins 13/14) - see Lab 04's doc.
- **SPI0's hardware FIFO is only 8 bytes deep**: a single SPI transfer over 8 bytes needs a FIFO-refill interrupt that doesn't fire reliably on this platform, failing with `-ETIMEDOUT`. When porting a new raw-SPI device, always chunk transfers to 8 bytes (see Lab 06's doc).
- **Check the pinctrl file before reusing any GPIO**: assuming a pin is free just because its schematic net name looks unrelated can be risky - some pins physically share a pad with JTAG or debug-module functions. Check the pin's full alternate-function group in the SDK's `sr100_pinctrl.dtsi` before choosing a GPIO, and always build/flash to confirm normal boot before wiring anything to a newly-chosen pin (see Lab 03's doc).
- **GPIO I/O voltage is 1.8V**: a 3.3V power pin being present on the header doesn't mean the GPIO logic level is 3.3V. SR110's SPI0/GPIO signals are a 1.8V I/O domain - displays rated for 3.3V IOVCC may need a level shifter (see Lab 06's doc).

## Development Environment

- Zephyr RTOS (SDK: `syna_zephyr_sdk-1.0.0`)
- Board target: `sr100_rdk/sr100/m55`
- Build: `west build -p always -b sr100_rdk/sr100/m55 <lab path>/lab/`
- Flash: `srsdk_tools/openocd_flash.py` (see each lab's "Build & Run" section)

## Schematic/Datasheet References

- SR110 RDK schematic: `SC950-C01116-01 RevE`
- Each lab doc cites the specific sheet number for any hardware claim (e.g. "sheet 10, PIN HEADERS")
