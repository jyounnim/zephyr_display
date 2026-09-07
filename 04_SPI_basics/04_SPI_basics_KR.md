# Lab 04: SPI 기초 개념 — 왜 "SPI 스캐너"는 없는가

> **SR110 포팅 노트 (2026-09-01)**: 원래 ESP32-S3-DevKitC-1용으로 작성된 랩입니다. I2C/SPI 개념 비교(아래 절)는 플랫폼 무관하게 그대로 유효하지만, **SR110에는 ESP32-S3식 GPIO 매트릭스가 없어 "같은 핀을 MOSI/MISO로 동시에 매핑"하는 소프트웨어 트릭 자체가 불가능**합니다. 대신 실제 점퍼선으로 MOSI-MISO를 연결해야 합니다. 그리고 SR110의 유일한 SPI 마스터(SPI0)를 켜면 보드 기본 콘솔 UART가 전부 막히는 하드웨어 제약이 있어, 콘솔을 다른 UART 핀 그룹으로 옮겨야 합니다 — 아래 "SPI0/콘솔 충돌" 절 참고.

## 이 실습에서 배우는 것

원래 계획엔 "SPI 스캐너"가 있었는데, 검토 결과 **SPI는 구조적으로 I2C 같은 스캔이 불가능**합니다. 이 실습은 그 이유를 이해하고, 대신 **SPI 버스 자체가 정상 동작하는지 확인하는 루프백(loopback) 자가진단**을 만들어봅니다.

## I2C와 SPI, 근본적으로 다른 점

| | I2C | SPI |
|---|---|---|
| 신호선 | 2개(SDA, SCL) 공유 버스 | 최소 3~4개(SCK, MOSI, MISO, CS) |
| 장치 구분 방법 | **주소**(7비트) — 소프트웨어로 순회 가능 | **CS(Chip Select) 핀** — 어느 핀을 활성화하느냐로 결정, 배선도로만 알 수 있음 |
| "누구 있어?" 질문 | 가능 (주소에 ACK/NACK) | **불가능** — 프로토콜 자체에 주소 개념이 없음 |
| 여러 장치 연결 | 같은 두 선에 그냥 병렬 연결 | SCK/MOSI/MISO는 공유해도 되지만, 장치마다 **별도의 CS선**이 필요 |

**핵심**: I2C 스캔이 가능했던 이유는 "주소에 대고 물어보면 그 주소를 쓰는 장치만 대답한다"는 메커니즘이 있어서입니다. SPI는 애초에 이런 메커니즘이 없습니다 — CS를 활성화하는 순간 그 라인에 연결된 장치는 "내가 선택됐다"고만 인식할 뿐, 자기가 "누구인지" 먼저 알려주는 절차가 프로토콜에 없습니다. 그래서 "이 버스에 뭐가 연결되어 있지?"를 코드로 알아내는 범용적인 방법 자체가 존재하지 않습니다.

## 그럼 SPI가 제대로 연결됐는지는 어떻게 확인하나

두 단계로 나눠서 생각하면 됩니다.

1. **버스 자체(SCK/MOSI/MISO 배선, SPI 페리페럴 설정)가 정상인가** → 이건 범용적으로 확인 가능합니다. 이 실습에서 다루는 **루프백 테스트**가 이걸 확인하는 방법입니다
2. **특정 칩과 실제로 통신되는가** → 이건 그 칩 고유의 명령을 알아야만 확인 가능합니다 (예: SPI Flash 칩의 JEDEC ID 읽기 명령, 특정 디스플레이의 초기화 시퀀스 등). 05~08번 실습에서 실제 디스플레이와 통신하는 게 바로 이 단계입니다

## 루프백 테스트란

MOSI(마스터가 보내는 선)와 MISO(마스터가 받는 선)를 **물리적으로 점퍼선을 연결하거나, 아예 같은 GPIO 핀을 핀먹스 레벨에서 공유**시키면, 내가 보낸 데이터가 그대로 되돌아옵니다. 데이터가 정확히 일치하면 "적어도 SPI 페리페럴과 클럭, 핀 라우팅은 정상"이라는 걸 확인할 수 있습니다.

원래 ESP32-S3 버전은 **점퍼선 없이**, 오버레이에서 MISO와 MOSI를 같은 GPIO(8번)에 매핑하는 방식을 썼습니다. ESP32-S3의 GPIO 매트릭스는 입력 라우팅과 출력 라우팅을 핀 단위로 독립적으로 설정할 수 있어서, 같은 물리 핀을 "SPI2 출력(MOSI)"이자 동시에 "SPI2 입력(MISO)"으로 쓸 수 있었습니다.

**SR110은 이 트릭을 쓸 수 없습니다.** SoC 패키지 단에서 각 pad의 alternate function이 고정돼 있어서, MOSI와 MISO는 처음부터 서로 다른 물리 핀입니다. 그래서 SR110 버전은 **물리적인 점퍼선으로 MOSI-MISO pad를 직접 연결**해야 합니다.

## SPI0/콘솔 UART 충돌 — SR110의 가장 중요한 제약

SR110은 SPI *마스터* 컨트롤러가 **SPI0 하나뿐**입니다(SPI1도 있지만 slave 전용이라 이 테스트에는 쓸 수 없습니다). 그런데 base devicetree(`sr100_rdk_m55.dts`)에 이미 다음과 같이 명시돼 있습니다:

> "the default pinmux configuration for this board uses GP23/GP24 for UART1_TX/UART1_RX respectively, which conflicts with the spi_mstr_mosi/spi_mstr_miso muxes."

즉 SPI0의 MOSI/MISO/CLK/CS 4개 신호가 각각 UART1_TX, UART1_RX, UART0_RX, UART0_TX와 **정확히 같은 물리 pad**를 alternate function으로 공유합니다. SPI0를 켜면 보드 기본 콘솔(UART1, GPIO23/24, J25 헤더로 외부 USB-TTL 연결)이 통째로 못 쓰게 됩니다.

**해결책**: Synaptics 공식 SDK의 `samples/dma/sr100_rdk_m55.overlay`가 정확히 이 문제를 겪고, 콘솔을 UART0의 *대체* 핀 그룹(`uart0_tx_c`/`uart0_rx_c`, GLOBAL 핀 도메인 — 기본 그룹인 `uart0_tx_b`/`uart0_rx_b`는 spi_mstr_clk/cs와 또 겹치므로 안 됨)으로 옮겨서 이 충돌을 회피한 것을 확인했습니다. 이 랩의 오버레이도 동일한 패턴을 따릅니다.

> ✅ **회로도로 확정 (SC950-C01116-01 RevE, 10번 시트 "PIN HEADERS, JTAG, DMIC")**: `uart0_tx_c`는 SoC 핀 **GPIO44**, `uart0_rx_c`는 **GPIO45**이고, 둘 다 **J24("Right 20pin CONN") 13/14번 핀**에 나와 있습니다(TX=13번, RX=14번). 외부 USB-TTL 어댑터의 RX를 J24 13번, TX를 J24 14번, GND를 보드 GND에 연결하면 콘솔 로그를 볼 수 있습니다. 보드 기본 콘솔 경로(UART1, J25 13/14번 핀 = GPIO23/24)와는 다른 헤더이니 혼동하지 마십시오 — SPI0가 이 랩에서 GPIO23/24를 가져가므로 J25 쪽 콘솔은 죽습니다.
>
> **MOSI-MISO 점퍼선 위치도 회로도로 확정**: `spi_mstr_mosi`=GPIO23, `spi_mstr_miso`=GPIO24이고 둘 다 **J25("Left 20pin CONN") 13/14번 핀**에 나와 있습니다(MISO=13번, MOSI=14번). **J25 13번과 14번 핀을 점퍼선으로 직결**하면 됩니다.

## 준비물

- MOSI-MISO 점퍼선 1개 (J25 13번-14번 핀 직결, SR110은 ESP32-S3와 달리 소프트웨어 루프백이 불가능하므로 필수)
- 콘솔 확인용 외부 USB-TTL 어댑터 (J24 13/14번 핀 연결, 위 "SPI0/콘솔 UART 충돌" 절 참고)

## 폴더 구성

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

SR110은 `spi_mstr_mosi`/`spi_mstr_miso`가 서로 다른 고정 pad이므로, 위 "준비물"에서 안내한 대로 **이 두 pad 사이를 실제 점퍼선으로 연결**해야 루프백이 성립합니다. 나머지(`uart1` disable + `uart0`를 `uart0_tx_c`/`uart0_rx_c`로 재설정)는 위 "SPI0/콘솔 UART 충돌" 절에서 설명한 콘솔 회피 조치입니다.

## 커스텀 devicetree 바인딩

특정 실제 칩이 아니라 "버스 테스트용 자리"만 필요해서, 최소한의 바인딩만 만듭니다.

```yaml
description: Generic placeholder SPI device node, used for a bus-level loopback self-test

compatible: "zds,spi-loopback"

include: spi-device.yaml
```

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20.0)

# 커스텀 "zds,spi-loopback" 바인딩이 이 앱 자신의 dts/bindings/ 아래에 있으므로,
# find_package(Zephyr...)가 실행되기 전에 DTS_ROOT를 미리 확장해야 합니다.
# 순서가 바뀌면 devicetree 컴파일러가 이 바인딩을 못 찾아서
# "'zds,spi-loopback' compatible not found" 에러로 빌드가 실패합니다.
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

`snps,designware-spi`(SR110의 SPI0 드라이버) 역시 devicetree에서 노드가 `status = "okay"`이면 자동으로 활성화되므로, ESP32 버전과 마찬가지로 별도의 `CONFIG_xxx_SPI=y` 심볼을 추가할 필요가 없습니다.

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

## 코드

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

## 빌드 & 실행

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\04_SPI_basics\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <openocd 경로> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

콘솔은 위 "SPI0/콘솔 UART 충돌" 절에서 설명한 대로 UART0의 대체 핀(`uart0_tx_c`/`uart0_rx_c` = GPIO44/45)으로 옮겨져 있습니다 — 외부 USB-TTL 어댑터를 J24 13/14번 핀에 연결하고 **230400bps 8N1**로 여십시오.

## 실행 & 확인

```
=== SPI Basics: bus loopback self-test ===
Sent:     01 02 03 04 AA 55 FF 00
Received: 01 02 03 04 AA 55 FF 00
PASS: received bytes match sent bytes - SPI peripheral, clock, and pin routing are all working.
```

`Sent`와 `Received`가 정확히 일치하는지 확인하세요.

## 관찰 포인트

- 이 테스트가 통과했다고 해서 "SPI로 아무 장치나 연결하면 다 될 것"이라는 뜻은 아닙니다 — **버스 자체(전기적 신호, 페리페럴 설정)가 정상**이라는 것만 확인된 겁니다. 실제 장치와의 통신은 그 장치의 프로토콜을 정확히 구현해야 합니다 (05~08번 실습에서 하게 될 일)
- I2C 스캐너(01번)와 이 루프백 테스트(04번)를 나란히 놓고 비교해보면, **"두 프로토콜이 겉보기엔 비슷해 보여도(클럭+데이터), 설계 철학 자체가 다르다"**는 걸 실감할 수 있습니다 — I2C는 "버스 위의 여러 장치를 발견하는" 데 최적화되어 있고, SPI는 "이미 아는 장치와 빠르게 통신하는" 데 최적화되어 있습니다
- CS 라인을 여러 개 두면(오버레이의 `cs-gpios`에 여러 GPIO를 배열로 지정) 같은 SCK/MOSI/MISO를 공유하면서 여러 SPI 장치를 연결할 수 있습니다 — 이후 실습에서 여러 디스플레이를 동시에 연결하고 싶다면 이 방식을 씁니다

## 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| `SPI device not ready` | 오버레이가 실제로 적용 안 됨 — 오버레이 파일명(`sr100_rdk_sr100_m55.overlay`)이 west board target과 일치하는지 확인 |
| `spi_transceive_dt failed` | SPI 버스 자체 설정 문제 — `west build -t devicetree`로 `&spi0` 노드가 제대로 병합됐는지 확인 |
| Sent와 Received가 다름 | MOSI-MISO 점퍼선이 실제로 연결됐는지 확인 (SR110은 소프트웨어 루프백이 안 되므로 점퍼선이 필수) |
| 콘솔에 아무 출력도 안 보임 | UART0의 대체 핀(`uart0_tx_c`/`uart0_rx_c`)에 외부 USB-TTL 어댑터를 연결했는지, 보드 기본 J25/GPIO23-24 쪽을 보고 있는 건 아닌지 확인 — SPI0가 켜지면 그쪽 콘솔은 죽습니다 |
| Devicetree 바인딩을 못 찾음 (`'zds,spi-loopback' compatible not found`) | `CMakeLists.txt`의 `list(APPEND DTS_ROOT ...)`가 `find_package(Zephyr...)` 이전에 있는지 확인 |

## 다음

05번 실습(`05_OLED_SSD1306_SPI`)에서 이번에 확인한 SPI0 버스 위에 실제 OLED(SSD1306 SPI 모드)를 연결합니다.
