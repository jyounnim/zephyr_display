# Lab 07: Nokia 5110 (PCD8544) 모노크롬 LCD — 84x48, raw SPI

## 1. 개요

보드는 **Synaptics SR110** (`sr100_rdk/sr100/m55`), 프레임워크는 **Zephyr RTOS**입니다.

옛날 노키아 휴대폰 액정으로 유명한 **PCD8544 컨트롤러 기반 84x48 모노크롬 LCD**("Nokia 5110" 모듈)를 raw SPI로 구동합니다. Zephyr Display/CFB 서브시스템은 쓰지 않고, 이 시리즈의 다른 랩들(SSD1306, I2C LCD, SPI 루프백)과 동일하게 애플리케이션 코드에서 직접 SPI/GPIO를 다룹니다.

이 모듈은 **MISO 라인이 아예 없습니다** (write-only 디스플레이) — 그래서 04번 랩(SPI 루프백)과 달리 이번 오버레이는 MOSI/SCLK/CS만 있으면 됩니다.

이 랩은 **실기로 완전히 검증 완료**된 상태입니다.

> **디스플레이 스펙 요약**
> | 항목 | 내용 |
> |---|---|
> | 컨트롤러 | Philips(NXP) **PCD8544** |
> | 해상도 | 84 x 48 픽셀, 1비트(모노크롬) |
> | 유래 | 노키아 5110/3310 등 2000년대 초 피처폰 액정 |
> | 인터페이스 | SPI 전용 (write-only, MISO 없음) |
> | 대비 조정 | 물리 트리머 없음 — 소프트웨어 Vop 명령으로만 조정 |

## 2. 준비물

- Nokia 5110 (PCD8544) LCD 모듈 (보통 8핀: RST, CE, DC, DIN, CLK, VCC, LIGHT, GND)
- Synaptics SR110 보드
- 콘솔 확인용 외부 USB-TTL 어댑터

## 3. 배선

| 신호 | 역할 | SR110 연결 |
|---|---|---|
| VCC | 전원 | 3.3V |
| GND | 그라운드 | GND |
| RST | 리셋 (active low) | gpioa 19 (SoC GPIO19, **J24 5번 핀**) |
| CE (CS) | 칩 선택 | SPI0 CS (핀 그룹 `spi_mstr_cs`, 네이티브 하드웨어 CS) |
| DC | Data/Command 선택 | gpioa 20 (SoC GPIO20, **J24 6번 핀**) |
| DIN (MOSI) | 데이터 입력 | SPI0 MOSI (핀 그룹 `spi_mstr_mosi`) |
| CLK (SCLK) | 클럭 | SPI0 CLK (핀 그룹 `spi_mstr_clk`) |
| LIGHT (BL) | 백라이트 | 3.3V 또는 GND 스위칭 (선택) |

> ✅ **RST/DC 핀 선정 근거**: GPIO19/20은 SoC의 I2S_DO/I2S_DI 핀인데, 이 랩은 I2S를 쓰지 않으므로 일반 GPIO로 재사용했습니다(회로도 SC950-C01116-01 RevE 10번 시트 확인, **J24 5/6번 핀**). 이 프로젝트에서는 "회로도상 안 쓰는 핀으로 보인다"는 것만으로 GPIO를 재사용하면 위험할 수 있다는 게 확인된 적이 있어(JTAG/디버그 모듈과 물리적으로 같은 핀을 공유하는 경우), GPIO19/20은 `sr100_pinctrl.dtsi`로 JTAG/디버그모듈 계열과 안 겹치는 걸 직접 확인하고 골랐습니다. 05번 랩(GPIO17/18, J24 3/4번 핀)과 다른 핀이라 두 랩을 동시에 배선해도 충돌하지 않습니다.
>
> **SPI0/콘솔 UART 충돌**: SR110의 유일한 SPI 마스터(SPI0)를 켜면 보드 기본 콘솔(UART1, GPIO23/24, J25 13/14번 핀)이 막힙니다. 이 랩의 오버레이는 04번 랩과 동일하게 콘솔을 UART0의 대체 핀(`uart0_tx_c`/`uart0_rx_c` = GPIO44/45, **J24 13/14번 핀**)으로 옮겨뒀습니다. 외부 USB-TTL 어댑터를 J24 13/14번 핀에 연결하십시오.
>
> **전원 주의**: 01번 랩 문서의 "전원 공급 관련 참고" 절 참고 — 보드 헤더의 3.3V 레일은 온보드 부품들과 공유되므로, 다른 디스플레이도 동시에 배선한다면 가능하면 외부 3.3V 전원을 쓰는 것을 권장합니다.

Nokia 5110/PCD8544 모듈은 원래 노키아 폰 내부 부품이라 **네이티브 로직 레벨이 2.7~3.3V**입니다. SR110(3.3V 전용 GPIO)와 전압 궁합이 좋아서, 레벨 시프터나 5V 관련 고민 없이 바로 연결하면 됩니다. 다만 모듈에 따라 온보드 레귤레이터/저항 분배망이 5V 입력을 가정하고 만들어진 경우도 있으니(중국산 저가 브레이크아웃 보드 중 일부), VCC 핀에 5V를 물려도 되는지는 각자 모듈의 실크스크린/판매처 설명을 한 번 확인하세요. 이 랩은 **3.3V 직결을 기준**으로 작성했습니다.

## 4. PCD8544 명령 체계

PCD8544는 "basic instruction set"과 "extended instruction set" 두 모드를 오가며 설정합니다.

| 명령 | 값 | 의미 |
|---|---|---|
| Function Set (extended) | `0x21` | extended 모드로 진입 |
| Set Vop (contrast) | `0x80 \| Vop` | 명암 설정 — 이 랩은 기본값 `0xB0` 사용 |
| Temperature Control | `0x04` | 온도 계수 0 |
| Bias System | `0x14` | bias 1:48 |
| Function Set (basic) | `0x20` | basic 모드로 복귀 |
| Display Control | `0x0C` | 정상(비반전) 표시 모드 |

**대비(Vop) 값은 모듈마다 편차가 큽니다.** HD44780 LCD(02번 랩)에는 물리 트리머가 있어서 손으로 돌리면 됐지만, PCD8544는 **트리머가 없고 이 Vop 명령 값이 곧 소프트웨어 콘트라스트**입니다. 화면이 안 보이거나(너무 연함) 전부 까맣게 나오면(너무 진함) `main.c`의 `PCD8544_SET_VOP_DEFAULT`(기본 `0xB0`)를 `0x80`~`0xFF` 범위에서 조정하세요.

## 5. 주소 지정과 프레임버퍼

- 화면은 84 x 48 픽셀 = 가로 84칼럼 x 세로 6페이지(페이지당 8픽셀)
- `0x80|x` 로 X(칼럼) 주소, `0x40|y` 로 Y(페이지) 주소를 지정
- 주소 지정 후 데이터를 계속 흘려보내면 **X가 자동 증가하다가 84에서 다음 페이지로 자동 줄바꿈**됩니다 — 그래서 전체 프레임버퍼(84x6=504바이트)를 (0,0)에서 시작해 한 번에 전부 써버리는 것으로 화면 전체를 갱신할 수 있습니다

## 6. 코드 구조

- `pcd8544_send()` / `pcd8544_cmd()` / `pcd8544_data()`: DC 핀을 0(명령)/1(데이터)로 설정한 뒤 `spi_write_dt()`로 전송하되, **SR110 SPI0의 8바이트 하드웨어 FIFO 제한 때문에 모든 전송을 8바이트 단위로 쪼갭니다** (8바이트를 넘는 단일 전송은 `-116`/`-ETIMEDOUT`으로 실패). 이 랩은 프레임버퍼 전체(504바이트)를 한 번에 전송하므로 이 청크 분할이 없으면 매번 실패합니다.
- `pcd8544_init()`: RST 펄스 → extended 명령들(Vop/온도/bias) → basic 복귀 → 정상 표시 모드
- `framebuffer[84*6]`: 화면 전체를 담는 배열, `fb_draw_char`/`fb_draw_string`으로 5x7 폰트를 채워 넣고 `pcd8544_update()`로 한 번에 전송 (내부적으로는 8바이트씩 쪼개져서 나감)
- `main()`: SPI/GPIO 준비 확인 → 초기화 → "Hello World!" / "Nokia 5110" 두 줄 출력

## 7. Devicetree

```dts
&spi0 {
    #address-cells = <1>;
    #size-cells = <0>;
    status = "okay";
    pinctrl-0 = <&spi_mstr_mosi &spi_mstr_miso &spi_mstr_clk &spi_mstr_cs>;
    pinctrl-names = "default";

    pcd8544: pcd8544@0 {
        compatible = "zds,pcd8544";
        reg = <0>;
        spi-max-frequency = <1000000>;
        reset-gpios = <&gpioa 19 GPIO_ACTIVE_LOW>;
        dc-gpios = <&gpioa 20 GPIO_ACTIVE_HIGH>;
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

`reset-gpios`/`dc-gpios` 속성 이름은 Zephyr 자체의 `mipi-dbi-spi` 디스플레이 바인딩이 쓰는 이름과 동일하게 맞췄습니다 — 이 랩은 그 프레임워크를 쓰지 않고 커스텀 바인딩(`zds,pcd8544`)이지만, 이름 관례를 공식 바인딩과 통일해두면 나중에 실제 Zephyr 디스플레이 드라이버로 갈아탈 때도 헷갈리지 않습니다. `spi_mstr_miso`는 이 모듈이 쓰지 않지만, 05번 랩과 pinctrl-0을 통일하기 위해 함께 넣어뒀습니다.

## 8. 커스텀 devicetree 바인딩

```yaml
description: |
  PCD8544-based Nokia 5110 monochrome LCD (84x48), driven over raw SPI
  from application code - no Zephyr Display/CFB subsystem involved.

compatible: "zds,pcd8544"

include: spi-device.yaml

properties:
  reset-gpios:
    type: phandle-array
    required: true
    description: >
      Reset pin. Active low - pulse low to reset the PCD8544 controller.

  dc-gpios:
    type: phandle-array
    required: true
    description: >
      Data/Command select pin. Driven low before a command byte is
      written, high before a data byte is written.
```

## 9. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20.0)

list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nokia5110_lab)

target_sources(app PRIVATE src/main.c)
```

04번 랩과 동일하게, 커스텀 바인딩을 쓰기 때문에 `DTS_ROOT`를 `find_package(Zephyr...)`보다 먼저 추가해야 합니다.

## 10. prj.conf

```
CONFIG_SPI=y
CONFIG_GPIO=y
CONFIG_PRINTK=y
```

## 11. 폴더 구성

```
07_Nokia5110_display/
├── 07_Nokia5110_display_KR.md
├── 07_Nokia5110_display_EN.md
└── lab/
    ├── src/
    │   └── main.c
    ├── boards/
    │   └── sr100_rdk_sr100_m55.overlay
    ├── dts/
    │   └── bindings/
    │       └── display/
    │           └── zds,pcd8544.yaml
    ├── CMakeLists.txt
    ├── prj.conf
    └── sample.yaml
```

## 12. Build & Run

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\07_Nokia5110_display\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <openocd 경로> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

콘솔은 UART0의 대체 핀(`uart0_tx_c`/`uart0_rx_c` = GPIO44/45)으로 옮겨져 있습니다 — 외부 USB-TTL 어댑터를 **J24 13/14번 핀**에 연결하고 **230400bps 8N1**로 여십시오.

### 예상 시리얼 출력

```
Nokia 5110 (PCD8544) lab starting
Nokia 5110 initialized and "Hello World!" / "Nokia 5110" written
```

화면 1페이지(맨 위)에 `Hello World!`, 2페이지에 `Nokia 5110`이 표시되어야 합니다.

## 13. 관찰 포인트

- **Vop(대비) 값이 이 랩에서 가장 조정이 필요할 가능성이 높은 부분**입니다 — 물리 트리머가 없는 대신 소프트웨어 값이라, 화면이 안 보인다고 배선부터 의심하기 전에 `PCD8544_SET_VOP_DEFAULT` 값부터 몇 가지 바꿔보는 걸 권장합니다
- MISO가 없는 write-only 디스플레이라는 점에서 04번 랩(SPI 루프백)과 좋은 대조가 됩니다 — 루프백 테스트로 검증했던 "버스 자체가 정상"이라는 전제 위에, 이번엔 실제 장치의 명령 프로토콜을 얹는 실습입니다
- `reset-gpios`/`dc-gpios`처럼 Zephyr 공식 바인딩과 이름을 맞추는 습관은, 나중에 이 커스텀 드라이버를 진짜 Zephyr Display 드라이버로 옮길 때 devicetree를 거의 그대로 재사용할 수 있게 해줍니다

## 14. 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| `SPI device not ready` / `RST/DC GPIO not ready` | 오버레이 미적용 — 파일명이 board target과 일치하는지 확인 |
| 화면이 완전히 흰색(또는 아무 표시 없음) | Vop가 너무 낮음(대비 부족) — `PCD8544_SET_VOP_DEFAULT`를 `0xB0`에서 조금씩 올려보기(`0xB8`, `0xC0` 등) |
| 화면이 전부 까맣게 나옴/체커보드 패턴 | Vop가 너무 높음(대비 과함) — 값을 낮춰보기, 또는 RST 시퀀스가 제대로 안 됐는지 확인 |
| 시리얼에 init 실패 로그, SPI write 에러 | 배선(특히 CE/CS, DIN/MOSI, CLK) 재확인. 보드 레일 전원이면 외부 3.3V로 바꿔서 재시도 |
| `spi_write_dt(...) failed, ret=-116` | SPI0 FIFO 8바이트 제한 문제 — `PCD8544_CHUNK_BYTES`가 8 이하로 되어 있는지 확인 (이미 반영되어 있음) |
| 글자가 알아볼 수 없이 깨짐/위치가 이상함 | `fb_draw_string`의 6픽셀 간격 로직, 또는 페이지(page) 인자가 0~5 범위인지 확인 |
| 콘솔에 아무 출력도 안 보임 | 외부 USB-TTL 어댑터가 **J24 13/14번 핀**에 연결됐는지 확인 — 보드 기본 콘솔(J25 13/14번 핀)은 SPI0가 켜지면 죽습니다 |
| `'zds,pcd8544' compatible not found` | `CMakeLists.txt`의 `list(APPEND DTS_ROOT ...)`가 `find_package(Zephyr...)` 이전에 있는지 확인 |

**Lab 07 완전히 검증 완료** — SPI0 버스, RST/DC(GPIO19/20), SPI0 FIFO 8바이트 청크 분할까지 전부 실기로 확인 완료.

## 15. 다음

08번 랩(`08_TFT_ST7735`)에서 컬러 TFT로 넘어갑니다 — 이 랩의 raw SPI + 커스텀 바인딩 패턴을 그대로 재사용하되, 컬러 디스플레이 특유의 검증 방법(색상 막대)이 추가됩니다.
