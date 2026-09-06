# Lab 05: OLED SSD1306 (0.96") — SPI Mode

## 1. Overview

Board: **Synaptics SR110** (`sr100_rdk/sr100/m55`), framework: **Zephyr RTOS**.

This lab connects the **exact same chip (SSD1306)** as Lab 03 (I2C mode), but this time in **SPI mode**. Zephyr's `solomon,ssd1306` driver supports both I2C and SPI buses in a single driver (branching internally based on bus type) - so the **application code (`main.c`) is effectively identical to Lab 03**, and only the devicetree overlay changes. This lab is a hands-on look at Zephyr's driver-model bus abstraction in action.

This lab has been **fully verified on real hardware**.

<img width="411" height="282" alt="image" src="https://github.com/user-attachments/assets/4354501c-b178-4e85-9d22-0257906cc6ec" />


## 2. Requirements

- A 0.96" OLED, SSD1306, **7-pin SPI module** (VCC/GND/SCK/SDA(MOSI)/RES/DC/CS)

> ⚠️ A 4-pin I2C-only module cannot be used for this lab. Confirm your module breaks out the SPI-specific pins (especially DC and RES) before starting.

## 3. Wiring

| Signal | SR110 connection |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SCK | SPI0 CLK (SoC GPIO22, **J25 pin 11**) |
| SDA (MOSI) | SPI0 MOSI (SoC GPIO23, **J25 pin 14**) |
| CS | SPI0 CS, native hardware CS (SoC GPIO21, **J25 pin 12**) |
| DC | SoC GPIO18, **J24 pin 4** |
| RES | SoC GPIO17, **J24 pin 3** |

SSD1306 is write-only, so MISO isn't actually used, but it's still included in the overlay's pinctrl (SoC GPIO24, J25 pin 13) to keep pinctrl-0 consistent across this series' SPI labs.

> **Console note**: SR110 has only one SPI master (SPI0), and its signal lines physically overlap with the board's default console (UART1, GPIO23/24, J25 pins 13/14). So this lab's console has been moved to UART0's alternate pins (GPIO44/45, **J24 pins 13/14**) - connect an **external USB-TTL adapter to J24 pins 13/14**, not the usual J25 console, to see log output.
>
> **Power note**: the OLED's VCC is usually fine from the board's 3.3V pin, but the board header's 3.3V rail is shared with onboard components - if wiring up multiple displays at once, prefer an external 3.3V supply where practical.

## 4. File Layout

```
05_OLED_SSD1306_SPI/
├── 05_OLED_SSD1306_SPI_KR.md
├── 05_OLED_SSD1306_SPI_EN.md
└── lab/
    ├── src/
    │   └── main.c
    ├── boards/
    │   └── sr100_rdk_sr100_m55.overlay
    ├── CMakeLists.txt
    ├── prj.conf
    └── sample.yaml
```

## 5. Devicetree Overlay

```dts
&spi0 {
    #address-cells = <1>;
    #size-cells = <0>;
    status = "okay";
    pinctrl-0 = <&spi_mstr_mosi &spi_mstr_miso &spi_mstr_clk &spi_mstr_cs>;
    pinctrl-names = "default";

    oled_spi: ssd1306@0 {
        compatible = "solomon,ssd1306";
        reg = <0>;
        spi-max-frequency = <4000000>;
        data-cmd-gpios = <&gpioa 18 GPIO_ACTIVE_HIGH>;
        reset-gpios = <&gpioa 17 GPIO_ACTIVE_LOW>;
        width = <128>;
        height = <64>;
        segment-offset = <0>;
        page-offset = <0>;
        display-offset = <0>;
        multiplex-ratio = <63>;
        segment-remap;
        com-invdir;
        prechargep = <0x22>;
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
        zephyr,display = &oled_spi;
    };
};
```

CS uses `spi_mstr_cs`'s native hardware CS as-is - no separate `cs-gpios` needed, the controller manages CS internally via the `reg` value. Only DC/RES use SR110's dedicated GPIO controller (`gpioa`), GPIO18/17 (J24 pins 4/3).

## 6. prj.conf

```
CONFIG_SPI=y
CONFIG_DISPLAY=y
CONFIG_CHARACTER_FRAMEBUFFER=y
CONFIG_SSD1306=y
CONFIG_HEAP_MEM_POOL_SIZE=16384
```

## 7. Code — Nearly Identical to Lab 03

```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>

#define DISPLAY_STACK_SIZE 2048
#define DISPLAY_PRIORITY   5

static void display_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    if (!device_is_ready(dev)) {
        printk("DisplayThread: display device not ready\n");
        return;
    }

    /* This panel needs MONO01 tried first - MONO10 gives a
     * white-background/black-text look on this specific module. */
    if (display_set_pixel_format(dev, PIXEL_FORMAT_MONO01) != 0) {
        display_set_pixel_format(dev, PIXEL_FORMAT_MONO10);
    }

    if (cfb_framebuffer_init(dev)) {
        printk("DisplayThread: framebuffer init failed\n");
        return;
    }

    cfb_framebuffer_clear(dev, true);
    display_blanking_off(dev);

    printk("DisplayThread: ready (SPI mode)\n");

    int counter = 0;

    while (1) {
        char buf[32];

        snprintf(buf, sizeof(buf), "Count: %d", counter++);

        cfb_framebuffer_clear(dev, false);
        cfb_print(dev, "SSD1306 (SPI)", 0, 0);
        cfb_print(dev, buf, 0, 16);
        cfb_framebuffer_finalize(dev);

        k_sleep(K_SECONDS(1));
    }
}

K_THREAD_DEFINE(display_id, DISPLAY_STACK_SIZE, display_thread_entry,
                NULL, NULL, NULL, DISPLAY_PRIORITY, 0, 0);

int main(void)
{
    printk("main: started, DisplayThread is running independently\n");
    return 0;
}
```

> **Display polarity note**: this module shows a white background with black text if `PIXEL_FORMAT_MONO10` (the format tried first by default) is used. Trying `PIXEL_FORMAT_MONO01` first instead gives the usual black-background/white-text monochrome-OLED look (already reflected in the code above). Other SSD1306 modules may be the opposite - if your display looks inverted, try swapping this order.

## 8. Build & Run

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\05_OLED_SSD1306_SPI\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <path to openocd> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

The console has moved to UART0's alternate pins (GPIO44/45) - connect an external USB-TTL adapter to **J24 pins 13/14** and open it at **230400bps 8N1**.

> **One-time SDK setup**: this lab enables `CONFIG_DISPLAY=y`, which can trigger a `add_subdirectory given source "display" which is not an existing directory` CMake error, since the Synaptics SDK (`zephyr_srsdk`) distribution is missing its `drivers/display/` folder. This is an SDK packaging gap, not a bug in this lab's code/overlay - create an empty placeholder folder once to fix it:
>
> ```bash
> mkdir -p <syna_zephyr workspace>/zephyr_srsdk/drivers/display
> echo '# Placeholder - Synaptics SDK v1.0.0 has no custom display drivers' \
>      > <syna_zephyr workspace>/zephyr_srsdk/drivers/display/CMakeLists.txt
> ```

## 9. Running & Verifying

The display should show `SSD1306 (SPI)` and a `Count: N` counter incrementing once per second, in black-background/white-text.

## 10. Side-by-Side With Lab 03 (I2C)

| | Lab 03 (I2C) | Lab 05 (SPI) |
|---|---|---|
| Signal lines | 2 (SDA/SCL) | 4 (SCK/MOSI/CS) + DC/RES |
| Overlay `compatible` | `solomon,ssd1306` (I2C binding) | `solomon,ssd1306` (SPI binding, same name) |
| Addressing | `reg = <0x3d>` (I2C address, determined by a runtime scan) | `reg = <0>` (SPI CS index) + `dc-gpios`/`reset-gpios` added |
| **Application code (`main.c`)** | **Identical** | **Identical** |

**The last row is the point.** Despite a completely different bus underneath, `main.c` is unchanged except for one string ("(I2C)" → "(SPI)"). As long as you're using the Display + CFB API, the application doesn't need to care whether the bus below is I2C or SPI.

## 11. Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| Build-time CMake error: `add_subdirectory given source "display" which is not an existing directory` | See section 8's "One-time SDK setup" - an SDK packaging gap, not a code/overlay issue |
| Nothing shows on screen | Confirm you have the 7-pin SPI module (a 4-pin I2C-only module can't work here) |
| `device_is_ready()` returns false | Check `data-cmd-gpios`/`reset-gpios` polarity, and confirm RES/DC are actually wired to J24 pins 3/4 |
| No console output at all | Confirm the external USB-TTL adapter is on **J24 pins 13/14** - the board's default console (J25 pins 13/14) dies once SPI0 is enabled |
| White background, black text | See section 7's "Display polarity note" - confirm `PIXEL_FORMAT_MONO01` is tried first |
| I2C mode (Lab 03) works but SPI mode doesn't | If both `&i2c0` and `&spi0` are enabled in the same overlay, only one `chosen { zephyr,display = ...}` can win - confirm it points to the SPI node |

## 12. Next

Lab 06 (`06_TFT_ST7789V3`) covers an SPI-based color TFT.
