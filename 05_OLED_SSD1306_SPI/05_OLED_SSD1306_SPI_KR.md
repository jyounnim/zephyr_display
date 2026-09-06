# Lab 05: OLED SSD1306 (0.96") — SPI 모드

## 1. 개요

보드는 **Synaptics SR110** (`sr100_rdk/sr100/m55`), 프레임워크는 **Zephyr RTOS**를 사용합니다.

03번 실습(I2C 모드)과 **완전히 같은 칩(SSD1306)**을 이번엔 **SPI 모드**로 연결합니다. Zephyr의 `solomon,ssd1306` 드라이버는 I2C와 SPI 양쪽 버스를 하나의 드라이버가 함께 지원합니다(내부적으로 버스 타입에 따라 분기) — 그래서 **애플리케이션 코드(`main.c`)는 03번과 사실상 동일**하고, devicetree 오버레이만 바뀝니다. 이 점을 통해 Zephyr 드라이버 모델의 버스 추상화를 실습에서 직접 확인합니다.

이 랩은 **실기로 완전히 검증 완료**된 상태입니다.

<img width="411" height="282" alt="image" src="https://github.com/user-attachments/assets/3ef0e70e-9b87-4942-a9c8-8b13a7606c63" />


## 2. 준비물

- 0.96" OLED, SSD1306, **7핀 SPI 모듈** (VCC/GND/SCK/SDA(MOSI)/RES/DC/CS)

> ⚠️ 4핀 I2C 전용 모듈로는 이 실습이 불가능합니다. SPI 핀(특히 DC, RES)이 따로 나온 모듈인지 먼저 확인하십시오.

## 3. 배선

| 신호 | SR110 연결 |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SCK | SPI0 CLK (SoC GPIO22, **J25 11번 핀**) |
| SDA (MOSI) | SPI0 MOSI (SoC GPIO23, **J25 14번 핀**) |
| CS | SPI0 CS, 네이티브 하드웨어 CS (SoC GPIO21, **J25 12번 핀**) |
| DC | SoC GPIO18, **J24 4번 핀** |
| RES | SoC GPIO17, **J24 3번 핀** |

SSD1306은 쓰기 전용이라 MISO는 실제로 쓰지 않지만, 이 시리즈의 다른 SPI 랩들과 pinctrl을 통일하기 위해 오버레이에는 MISO(SoC GPIO24, J25 13번 핀)도 함께 포함되어 있습니다.

> **콘솔 주의**: SR110은 SPI 마스터가 SPI0 하나뿐인데, 이 SPI0의 신호선이 보드 기본 콘솔(UART1, GPIO23/24, J25 13/14번 핀)과 물리적으로 겹칩니다. 그래서 이 랩의 콘솔은 UART0의 대체 핀(GPIO44/45, **J24 13/14번 핀**)으로 옮겨져 있습니다 — 평소 쓰던 J25 콘솔이 아니라 **J24 13/14번 핀에 외부 USB-TTL 어댑터**를 연결해야 로그가 보입니다.
>
> **전원 주의**: OLED VCC는 보드 3.3V 핀으로도 대체로 충분하지만, 보드 헤더의 3.3V 레일은 온보드 부품들과 공유되므로 다른 디스플레이도 동시에 배선한다면 가능하면 외부 3.3V 전원을 쓰는 것을 권장합니다.

## 4. 폴더 구성

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

CS는 `spi_mstr_cs` 네이티브 하드웨어 CS를 그대로 씁니다 — 별도 `cs-gpios` 지정 없이 `reg` 값으로 컨트롤러가 내부적으로 CS를 제어합니다. DC/RES만 SR110의 전용 GPIO 컨트롤러(`gpioa`)의 GPIO18/17(J24 4/3번 핀)을 씁니다.

## 6. prj.conf

```
CONFIG_SPI=y
CONFIG_DISPLAY=y
CONFIG_CHARACTER_FRAMEBUFFER=y
CONFIG_SSD1306=y
CONFIG_HEAP_MEM_POOL_SIZE=16384
```

## 7. 코드 — 03번과 거의 동일

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

> **화면 반전 참고**: 이 모듈은 `PIXEL_FORMAT_MONO10`(기본으로 먼저 시도되는 값)을 쓰면 배경이 흰색, 글자가 검정색으로 나옵니다. `PIXEL_FORMAT_MONO01`을 먼저 시도하도록 순서를 바꾸면 일반적인 모노크롬 OLED처럼 검정 배경/흰 글자로 나옵니다 (위 코드에 이미 반영됨). 다른 SSD1306 모듈에서는 반대일 수 있으니, 화면이 반전되어 보이면 이 순서를 바꿔서 시도해보십시오.

## 8. 빌드 & 실행

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\05_OLED_SSD1306_SPI\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <openocd 경로> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

콘솔은 UART0의 대체 핀(GPIO44/45)으로 옮겨져 있습니다 — 외부 USB-TTL 어댑터를 **J24 13/14번 핀**에 연결하고 **230400bps 8N1**로 여십시오.

> **SDK 환경 설정 (최초 1회)**: 이 랩은 `CONFIG_DISPLAY=y`를 켜는데, Synaptics SDK(`zephyr_srsdk`) 배포판에 `drivers/display/` 폴더가 빠져 있어서 `add_subdirectory given source "display" which is not an existing directory` CMake 에러가 날 수 있습니다. 저희 코드/오버레이 문제가 아니라 SDK 패키징 누락이며, 아래처럼 빈 폴더를 한 번만 만들어두면 해결됩니다:
>
> ```bash
> mkdir -p <syna_zephyr 워크스페이스>/zephyr_srsdk/drivers/display
> echo '# Placeholder - Synaptics SDK v1.0.0 has no custom display drivers' \
>      > <syna_zephyr 워크스페이스>/zephyr_srsdk/drivers/display/CMakeLists.txt
> ```

## 9. 실행 & 확인

화면에 `SSD1306 (SPI)`와 1초마다 증가하는 `Count: N`이 검정 배경/흰 글자로 표시되면 정상입니다.

## 10. 관찰 포인트 — 03번(I2C)과 나란히 비교하기

| | 03번 (I2C) | 05번 (SPI) |
|---|---|---|
| 신호선 개수 | 2개 (SDA/SCL) | 4개 (SCK/MOSI/CS) + DC/RES |
| 오버레이 `compatible` | `solomon,ssd1306` (I2C 바인딩) | `solomon,ssd1306` (SPI 바인딩, 같은 이름) |
| 주소 지정 | `reg = <0x3d>` (I2C 주소, 런타임 스캔으로 결정) | `reg = <0>` (SPI CS 인덱스) + `dc-gpios`/`reset-gpios` 추가 |
| **애플리케이션 코드(`main.c`)** | **동일** | **동일** |

**핵심은 마지막 줄입니다** — 버스가 완전히 바뀌었는데도 `main.c`는 문자열 하나("(I2C)" → "(SPI)") 빼고 똑같습니다. Display + CFB API를 쓰는 한, 그 아래 버스가 I2C든 SPI든 애플리케이션은 신경 쓸 필요가 없습니다.

## 11. 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| 빌드 시 CMake 에러: `add_subdirectory given source "display" which is not an existing directory` | 8절 "SDK 환경 설정" 참고 — SDK 패키징 누락, 코드/오버레이 문제 아님 |
| 화면에 아무것도 안 나옴 | 7핀 SPI 모듈이 맞는지 확인 (4핀 I2C 전용 모듈이면 애초에 불가능) |
| `device_is_ready()`가 false | `data-cmd-gpios`/`reset-gpios` 극성 확인, J24 3/4번 핀에 실제로 RES/DC를 배선했는지 재확인 |
| 콘솔에 아무 출력도 안 보임 | 외부 USB-TTL 어댑터가 **J24 13/14번 핀**에 연결됐는지 확인 — 보드 기본 콘솔(J25 13/14번 핀)은 SPI0가 켜지면 죽습니다 |
| 배경이 흰색, 글자가 검정색으로 나옴 | 7절 "화면 반전 참고" — `PIXEL_FORMAT_MONO01`을 먼저 시도하도록 순서 확인 |
| I2C 모드(03번)는 됐는데 SPI 모드만 안 됨 | 같은 오버레이 안에 `&i2c0`과 `&spi0`을 동시에 활성화했다면 `chosen { zephyr,display = ...}`은 하나만 유효합니다 — SPI 쪽으로 지정했는지 확인 |

## 12. 다음

06번 실습(`06_TFT_ST7789V3`)에서 SPI 기반의 컬러 TFT를 다룹니다.
