# 3. SPI Basics — Why There's No "SPI Scanner"

> **Review note**: the original file was uploaded as `04_SPI_basics_KR.md`, but the body of the document (the "3." in the title, the `03_SPI_basics/` folder tree, section 8's "I2C scanner (Lab 1) vs. this loopback test (Lab 3)", and the closing section's "Lab 4") all consistently point to this being **Lab 3**. Renamed to `03_SPI_basics_*.md` to match, which also lines up with the Lab 01 (I2C scanner) → Lab 02 (I2C LCD) → this lab (Lab 03) sequence.
>
> Also filled in `CMakeLists.txt` and `sample.yaml`, which were listed in the folder tree but had no content shown in the original document. The code, overlay, and custom binding were cross-checked against official Zephyr sources and left unchanged. See "Review summary" at the bottom for details.

> **SR110 porting note (2026-09-01)**: originally written for the ESP32-S3-DevKitC-1. The I2C-vs-SPI comparison below still applies regardless of platform, but **SR110 has no ESP32-S3-style GPIO matrix, so the "map the same pin to both MOSI and MISO" software trick is impossible here** - a real jumper wire is required instead. SR110's only SPI master (SPI0) also shares its pins with the board's default console UART, which requires moving the console to a different UART pin group - see the "SPI0/console conflict" section below.

## What this lab covers

The original plan included an "SPI scanner," but on review, **SPI is structurally incapable of I2C-style scanning**. This lab explains why, and builds a **loopback self-test** instead - one that verifies the SPI bus itself is working correctly.

## I2C vs. SPI: a fundamental difference

| | I2C | SPI |
|---|---|---|
| Signal lines | 2 shared lines (SDA, SCL) | At least 3-4 (SCK, MOSI, MISO, CS) |
| How devices are told apart | **Address** (7-bit) - can be swept in software | **CS (Chip Select) pin** - determined by which pin is asserted, only known from the schematic |
| "Who's out there?" | Possible (ACK/NACK on an address) | **Not possible** - the protocol itself has no concept of addressing |
| Connecting multiple devices | Just wire them to the same two lines | SCK/MOSI/MISO can be shared, but each device needs **its own CS line** |

**The key point**: I2C scanning works because the protocol has a built-in mechanism - "ask at an address, and only the device using that address answers." SPI has no such mechanism to begin with: the instant CS is asserted, whatever's wired to that line simply knows "I've been selected" - there's no step in the protocol where a device first announces who it is. So there is no general-purpose way to ask, in code, "what's connected to this bus?"

## So how do you verify SPI wiring is correct?

Split it into two separate questions.

1. **Is the bus itself (SCK/MOSI/MISO wiring, SPI peripheral configuration) working?** This can be verified generically. The **loopback test** covered in this lab answers this question.
2. **Is there actual communication with a specific chip?** This can only be verified by knowing that chip's own commands (e.g. reading a SPI flash chip's JEDEC ID, or a specific display's init sequence). Labs 5-7, where we talk to a real display, are this step.

## What a loopback test is

If you physically jumper MOSI (what the master sends) to MISO (what the master receives), or - even simpler - share the same GPIO pin between them at the pinmux level, whatever you send comes right back. If the data matches exactly, that confirms "at minimum, the SPI peripheral, clock, and pin routing are all working."

The original ESP32-S3 version used the **no-jumper-wire** trick: the overlay maps MISO and MOSI onto the same GPIO (GPIO8). The ESP32-S3's GPIO matrix lets input routing and output routing be configured independently per pin, so the same physical pad can act as "SPI2 output (MOSI)" and "SPI2 input (MISO)" at the same time.

**SR110 can't do this.** The SoC package fixes each pad's alternate function, so MOSI and MISO are distinct, fixed pins from the start. The SR110 version therefore requires a **physical jumper wire directly connecting the MOSI and MISO pads**.

## SPI0/console conflict — SR110's most important constraint

SR110 has exactly **one SPI *master* controller: SPI0** (SPI1 also exists but is slave-only, so it can't be used for this test). The base devicetree (`sr100_rdk_m55.dts`) already calls out a conflict:

> "the default pinmux configuration for this board uses GP23/GP24 for UART1_TX/UART1_RX respectively, which conflicts with the spi_mstr_mosi/spi_mstr_miso muxes."

In other words, SPI0's MOSI/MISO/CLK/CS signals each share their exact physical pad with UART1_TX, UART1_RX, UART0_RX, and UART0_TX respectively. Turning on SPI0 makes the board's default console (UART1, GPIO23/24, connected via an external USB-TTL adapter on the J25 header) unusable.

**Fix**: Synaptics' own official SDK has already hit this exact conflict - `samples/dma/sr100_rdk_m55.overlay` works around it by moving the console to UART0's *alternate* pin group (`uart0_tx_c`/`uart0_rx_c`, on the GLOBAL pin domain - UART0's *default* group, `uart0_tx_b`/`uart0_rx_b`, collides with spi_mstr_clk/cs instead, so that's not usable either). This lab's overlay follows the same validated pattern.

> ✅ **Confirmed against the schematic** (SC950-C01116-01 RevE, sheet 10 "PIN HEADERS, JTAG, DMIC"): `uart0_tx_c` is SoC pin **GPIO44** and `uart0_rx_c` is **GPIO45**, both broken out on **J24 ("Right 20pin CONN") pins 13/14** (TX=pin 13, RX=pin 14). Wire an external USB-TTL adapter's RX to J24 pin 13, TX to J24 pin 14, and GND to board GND to see console output. This is a *different* header from the board's default console path (UART1 on J25 pins 13/14 = GPIO23/24) - don't mix them up, since SPI0 takes over GPIO23/24 in this lab.
>
> **The MOSI-MISO jumper location is also confirmed**: `spi_mstr_mosi`=GPIO23 and `spi_mstr_miso`=GPIO24, both broken out on **J25 ("Left 20pin CONN") pins 13/14** (MISO=pin 13, MOSI=pin 14). **Jumper J25 pin 13 directly to J25 pin 14.**

## Requirements

- 1 MOSI-MISO jumper wire, connecting J25 pin 13 to J25 pin 14 (required on SR110, unlike ESP32-S3's software loopback trick)
- An external USB-TTL adapter wired to J24 pins 13/14 to observe console output (see "SPI0/console conflict" above)

## File Layout

```
Zephyr_display/
└── 04_SPI_basics/
    ├── lab/
    │   ├── src/
    │   │   └── main.c
    │   ├── boards/
    │   │   └── sr100_rdk_sr100_m55.overlay
    │   ├── dts/
    │   │   └── bindings/
    │   │       └── spi/
    │   │           └── zds,spi-loopback.yaml
    │   ├── CMakeLists.txt
    │   ├── prj.conf
    │   └── sample.yaml
    ├── 04_SPI_basics_KR.md
    └── 04_SPI_basics_EN.md
```

## Devicetree Overlay

```dts
&spi0 {
    #address-cells = <1>;
    #size-cells = <0>;
    status = "okay";
    pinctrl-0 = <&spi_mstr_mosi &spi_mstr_miso &spi_mstr_clk &spi_mstr_cs>;
    pinctrl-names = "default";

    loopback_dev: loopback@0 {
        compatible = "zds,spi-loopback";
        reg = <0>;
        spi-max-frequency = <1000000>;
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

`spi_mstr_mosi`/`spi_mstr_miso` are distinct fixed pads on SR110, so per the "Requirements" section above, **connect them with a real jumper wire** for the loopback to close. The rest of the overlay (disabling `uart1`, reconfiguring `uart0` onto `uart0_tx_c`/`uart0_rx_c`) is the console workaround explained in "SPI0/console conflict" above.

## Custom Devicetree Binding

Since we need a placeholder "bus test slot" rather than a real chip, a minimal binding is enough.

```yaml
description: Generic placeholder SPI device node, used for a bus-level loopback self-test

compatible: "zds,spi-loopback"

include: spi-device.yaml
```

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20.0)

# The custom "zds,spi-loopback" binding lives under this app's own
# dts/bindings/, so DTS_ROOT must be extended BEFORE find_package(Zephyr...)
# runs. Get the order wrong and the devicetree compiler can't find the
# binding, failing the build with "'zds,spi-loopback' compatible not found".
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spi_basics_lab)

target_sources(app PRIVATE src/main.c)
```

## prj.conf

```
CONFIG_SPI=y
CONFIG_GPIO=y
```

Same as the ESP32-S3 version: no need to turn on the SPI driver separately. SR110's `snps,designware-spi` driver also auto-enables whenever the devicetree node is `status = "okay"`.

## sample.yaml

```yaml
sample:
  name: SPI basics - bus loopback self-test
  description: >
    Verify the SPI0 peripheral, clock, and pin routing on the Synaptics
    SR110 (sr100_rdk/sr100/m55) using a physical MOSI-MISO jumper wire
    loopback (SR110 has no ESP32-style software GPIO-matrix trick, so a
    real jumper wire is required here).
common:
  tags:
    - spi
  platform_allow:
    - sr100_rdk/sr100/m55
  harness: console
  harness_config:
    type: one_line
    regex:
      - "PASS: received bytes match sent bytes.*"
tests:
  sample.spi.sr110_loopback:
    build_only: true
```

## Code

```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <string.h>

#define LOOPBACK_NODE DT_NODELABEL(loopback_dev)

static const struct spi_dt_spec loopback_spi = SPI_DT_SPEC_GET(
    LOOPBACK_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER, 0);

static bool spi_loopback_test(void) {
    uint8_t tx_data[8] = {0x01, 0x02, 0x03, 0x04, 0xAA, 0x55, 0xFF, 0x00};
    uint8_t rx_data[8] = {0};

    struct spi_buf tx_buf = { .buf = tx_data, .len = sizeof(tx_data) };
    struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };

    struct spi_buf rx_buf = { .buf = rx_data, .len = sizeof(rx_data) };
    struct spi_buf_set rx_bufs = { .buffers = &rx_buf, .count = 1 };

    int ret = spi_transceive_dt(&loopback_spi, &tx_bufs, &rx_bufs);
    if (ret != 0) {
        printk("spi_transceive_dt failed: %d\n", ret);
        return false;
    }

    printk("Sent:     ");
    for (int i = 0; i < (int)sizeof(tx_data); i++) printk("%02X ", tx_data[i]);
    printk("\n");

    printk("Received: ");
    for (int i = 0; i < (int)sizeof(rx_data); i++) printk("%02X ", rx_data[i]);
    printk("\n");

    return memcmp(tx_data, rx_data, sizeof(tx_data)) == 0;
}

#define TEST_STACK_SIZE 2048
#define TEST_PRIORITY   5

static void test_thread_entry(void *p1, void *p2, void *p3) {
    printk("\n=== SPI Basics: bus loopback self-test ===\n");

    if (!spi_is_ready_dt(&loopback_spi)) {
        printk("SPI device not ready - check devicetree status/overlay\n");
        return;
    }

    bool ok = spi_loopback_test();

    if (ok) {
        printk("PASS: received bytes match sent bytes - SPI peripheral, "
               "clock, and pin routing are all working.\n");
    } else {
        printk("FAIL: received bytes do NOT match sent bytes - check "
               "the MISO/MOSI pinmux, or SCLK wiring.\n");
    }
}

K_THREAD_DEFINE(test_tid, TEST_STACK_SIZE, test_thread_entry,
                NULL, NULL, NULL, TEST_PRIORITY, 0, 0);

int main(void) {
    return 0;
}
```

## Build & Run

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\04_SPI_basics\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <path to openocd> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

The console has moved to UART0's alternate pins (`uart0_tx_c`/`uart0_rx_c` = GPIO44/45) per "SPI0/console conflict" above - connect an external USB-TTL adapter to J24 pins 13/14 and open it at **230400bps 8N1**.

### Expected output

```
=== SPI Basics: bus loopback self-test ===
Sent:     01 02 03 04 AA 55 FF 00
Received: 01 02 03 04 AA 55 FF 00
PASS: received bytes match sent bytes - SPI peripheral, clock, and pin routing are all working.
```

Confirm that `Sent` and `Received` match exactly.

## Things to notice

- Passing this test does not mean "any SPI device will now just work" - it only confirms **the bus itself (electrical signals, peripheral configuration) is sound**. Talking to a real device still requires implementing that device's own protocol correctly (which is exactly what labs 5-7 do).
- Placing the I2C scanner (Lab 1) side by side with this loopback test (Lab 3) makes it concrete that **"these two protocols look superficially similar (clock + data), but their design philosophies are completely different"** - I2C is optimized for "discovering multiple devices on a bus," while SPI is optimized for "talking fast to a device you already know is there."
- Adding multiple CS lines (an array of GPIOs in the overlay's `cs-gpios`) lets you connect several SPI devices while sharing the same SCK/MOSI/MISO - use this approach in a later lab if you want to drive multiple displays at once.

## Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| `SPI device not ready` | The overlay isn't actually being applied - check that the overlay filename (`sr100_rdk_sr100_m55.overlay`) matches the west board target |
| `spi_transceive_dt failed` | A problem with the SPI bus configuration itself - run `west build -t devicetree` to confirm the `&spi0` node merged correctly |
| Sent and Received don't match | Confirm the MOSI-MISO jumper wire is actually connected - SR110 has no software loopback, so the jumper is required |
| No console output at all | Confirm the external USB-TTL adapter is connected to UART0's alternate pins (`uart0_tx_c`/`uart0_rx_c`), not the board's default J25/GPIO23-24 - that console dies once SPI0 is enabled |
| Devicetree binding not found (`'zds,spi-loopback' compatible not found`) | Check that `CMakeLists.txt`'s `list(APPEND DTS_ROOT ...)` comes before `find_package(Zephyr...)` |

## Next

Lab 5 (`05_OLED_SSD1306_SPI`) connects a real OLED (SSD1306 in SPI mode) on top of the SPI0 bus verified here.
