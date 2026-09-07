# Lab 08: ST7735 128x160 컬러 TFT — raw SPI

## 1. 개요

보드는 **Synaptics SR110** (`sr100_rdk/sr100/m55`), 프레임워크는 **Zephyr RTOS**를 사용합니다.

이 시리즈에서 처음 다루는 **컬러** 디스플레이입니다 — **ST7735 컨트롤러 기반 128x160 컬러 TFT**("1.8인치 SPI TFT"라는 이름으로 흔히 팔리는 모듈)를 raw SPI로 구동합니다. 지금까지와 동일하게 Zephyr Display/CFB 서브시스템은 쓰지 않고 애플리케이션 코드에서 직접 SPI/GPIO를 다룹니다.

이 랩은 **실기로 완전히 검증 완료**된 상태입니다.

> **디스플레이 스펙 요약**
> | 항목 | 내용 |
> |---|---|
> | 컨트롤러 | Sitronix **ST7735** |
> | 해상도 | 128 x 160 픽셀, RGB565(16비트 컬러) |
> | 화면 크기 | 대개 1.8인치 |
> | 인터페이스 | SPI 전용 (write-only, MISO 없음) |
> | 특이사항 | 제조 배치별 "탭 색상" 스티커에 따라 오프셋이 다름 — 4절 참고 |
> | IOVCC | 대체로 2.8~3.3V — SR110의 1.8V I/O에서 레벨 시프터 없이 동작 확인됨 |

## 2. 준비물

- ST7735 128x160 컬러 TFT 모듈 ("green tab" 계열, 아래 3절 참고)
- Synaptics SR110 보드
- 콘솔 확인용 외부 USB-TTL 어댑터

Nokia 5110(07번 랩)과 마찬가지로 이 모듈도 **MISO 라인이 없는 write-only 디스플레이**라, 오버레이엔 MOSI/SCLK/CS만 있으면 됩니다.

## 3. 배선

| 신호 | 역할 | SR110 연결 |
|---|---|---|
| VCC | 전원 | 3.3V (아래 참고) |
| GND | 그라운드 | GND |
| SCL (SCLK) | 클럭 | SPI0 CLK (SoC GPIO22, **J25 11번 핀**) |
| SDA (MOSI) | 데이터 입력 | SPI0 MOSI (SoC GPIO23, **J25 14번 핀**) |
| CS | 칩 선택 | SPI0 CS, 네이티브 하드웨어 CS (SoC GPIO21, **J25 12번 핀**) |
| RST | 리셋 (active low) | SoC GPIO17, **J24 3번 핀** |
| DC (A0/RS로 표기된 모듈도 있음) | Data/Command 선택 | SoC GPIO18, **J24 4번 핀** |
| LED (BLK) | 백라이트 | 3.3V (상시 켜짐) |

> **콘솔 주의**: SR110의 유일한 SPI 마스터(SPI0)를 켜면 보드 기본 콘솔(UART1, GPIO23/24, J25 13/14번 핀)이 막힙니다. 이 랩의 오버레이는 콘솔을 UART0의 대체 핀(GPIO44/45, **J24 13/14번 핀**)으로 옮겨뒀습니다 — 외부 USB-TTL 어댑터를 그쪽에 연결하십시오.
>
> **전원 주의**: TFT는 이 시리즈 디스플레이 중 백라이트 전류가 큰 편입니다. 다른 디스플레이와 동시에 배선한다면 가능하면 외부 3.3V 전원을 쓰는 것을 권장합니다.

ST7735 IC 자체는 대체로 2.8~3.3V 로직이라 SR110과 전압 궁합이 좋은 편입니다. 다만 브레이크아웃 보드에 따라 온보드 레귤레이터가 있어 5V 입력을 받는 것도 있으니, 로직 자체는 보드에서 3.3V로 나오는 게 일반적이라는 점을 참고해 모듈 스펙을 확인하십시오.

## 4. "탭 색상"(Tab Color) — 왜 이게 중요한가

ST7735 128x160 모듈은 실제 패널 유리 뒤에 "빨강/초록/검정" 등 색깔 탭 스티커가 붙어서 유통되는데, 이게 **패널 제조 배치에 따른 미세한 오프셋 차이**를 표시하는 관례입니다. 같은 ST7735 컨트롤러라도 탭 색상에 따라:

- 화면 표시 영역의 시작 좌표 오프셋(CASET/RASET에 더해지는 값)이 다름
- 기본 MADCTL(회전/색상 순서) 값이 다를 수 있음

이 랩은 **가장 흔한 "green tab"** 기준(컬럼 +2, 로우 +1 오프셋)으로 작성했습니다. 화면이 위/왼쪽으로 몇 픽셀 잘려 보이거나 반대쪽에 검은 여백이 생기면, 십중팔구 이 오프셋이 실제 모듈과 안 맞는 것 — `main.c`의 `ST7735_XSTART`/`ST7735_YSTART`를 조정하십시오 (red tab은 보통 둘 다 0).

## 5. 초기화 시퀀스

Adafruit_ST7735 라이브러리의 "Rcmd1 + Rcmd3"(ST7735R, green tab) 테이블과 동일한 값으로 교차 검증해서 작성했습니다 — 이 조합은 사실상 업계 표준처럼 쓰이는 시퀀스라, 커맨드/인자/딜레이 값을 그 라이브러리 소스와 바이트 단위로 대조했습니다.

| 명령 | 인자 | 의미 |
|---|---|---|
| SWRESET (`0x01`) | - | 소프트웨어 리셋, 150ms 대기 |
| SLPOUT (`0x11`) | - | 슬립 아웃(절전 해제), 500ms 대기 |
| FRMCTR1-3 (`0xB1-0xB3`) | 각각 3/3/6바이트 | 프레임 레이트 제어 |
| INVCTR (`0xB4`) | 1바이트 | 디스플레이 반전 제어 |
| PWCTR1-5 (`0xC0-0xC4`) | - | 전원 제어 (전압 레귤레이터 설정) |
| VMCTR1 (`0xC5`) | 1바이트 | VCOM 전압 제어 |
| INVOFF (`0x20`) | - | 색상 반전 끔 |
| MADCTL (`0x36`) | `0xC8` | 메모리 접근 제어 — 회전/BGR 순서 (green tab 기본값) |
| COLMOD (`0x3A`) | `0x05` | 픽셀 포맷 = 16비트(RGB565) |
| GMCTRP1/GMCTRN1 (`0xE0`/`0xE1`) | 각 16바이트 | 감마 보정 테이블 (양/음) |
| NORON (`0x13`) | - | 정상 디스플레이 모드 |
| DISPON (`0x29`) | - | 화면 켜짐 |

RST 핀은 하드웨어 리셋(로우 펄스 10ms → 120ms 대기) 후 이 시퀀스를 실행합니다.

> **SR110 참고**: 이 랩은 전원제어/프레임레이트/감마 보정까지 포함한 이 상세 초기화 시퀀스 덕분에 실기에서 바로 정상 동작했습니다. 최소 명령(SWRESET/SLPOUT/COLMOD/MADCTL/INVON/NORON/DISPON)만으로는 SPI 통신 자체는 에러 없이 성공해도 화면에 아무것도 안 나오는 컨트롤러가 있을 수 있다는 걸 이 시리즈의 다른 컬러 TFT 랩에서 확인했습니다 — 컬러 TFT를 새로 포팅할 때는 처음부터 이렇게 상세한 초기화 시퀀스를 쓰는 것을 권장합니다.

## 6. 픽셀 포맷과 주소 지정

- `COLMOD=0x05` → **RGB565** (16비트/픽셀: R 5비트, G 6비트, B 5비트), 상위 바이트 먼저 전송
- `CASET`/`RASET`로 (x0,y0)-(x1,y1) 사각 영역을 지정한 뒤 `RAMWR`로 데이터를 스트리밍하면, 지정된 영역을 행 우선(row-major) 순서로 채웁니다
- 이 랩은 프레임버퍼를 통째로 들고 있지 않습니다 — Nokia 5110(07번 랩)은 84x6=504바이트라 배열로 들고 있어도 부담 없었지만, 이 화면은 128x160x2=40,960바이트라 정적 배열로 들기엔 부담스러워서, 필요한 사각 영역(글자 하나, 색상 막대 하나)만 그때그때 주소 지정 후 스트리밍하는 방식으로 구현했습니다

## 7. 코드 구조

- `st7735_send()` / `st7735_cmd()` / `st7735_data()`: DC 핀 설정 후 `spi_write_dt()`로 전송하되, **SR110 SPI0의 8바이트 하드웨어 FIFO 제한 때문에 모든 전송을 8바이트 단위로 쪼갭니다** (8바이트를 넘는 단일 전송은 `-116`/`-ETIMEDOUT`으로 실패). 초기화 시퀀스의 16바이트짜리 감마 보정 명령(`GMCTRP1`/`GMCTRN1`)이 이 청크 분할 없이는 실패합니다.
- `init_seq[]` + `st7735_run_init_sequence()`: 초기화 시퀀스를 (명령, 인자, 딜레이) 테이블로 표현 — 라이브러리 원본과 바이트 단위로 대조하기 쉽게 하기 위한 구조
- `st7735_set_addr_window()`: CASET/RASET/RAMWR로 사각 영역 지정
- `st7735_fill_rect()`: 지정 영역을 단색으로 채움 (64픽셀 단위로 `st7735_data()`를 호출하고, 그 안에서 다시 8바이트로 쪼개짐)
- `st7735_draw_char()`/`st7735_draw_string()`: 5x7 폰트를 픽셀 단위로 그려서 6x8 영역씩 전송
- `main()`: 화면 전체를 검정으로 채움 → "Hello World!"(흰색) / "ST7735 TFT"(청록색) 출력 → **R/G/B/Y/M/C/W 색상 막대** 출력

## 8. 왜 색상 막대를 그렸는가 — 내장 진단 도구

**컬러 디스플레이에서 가장 흔한 버그(R/G/B 채널 순서 뒤바뀜, 픽셀 바이트 순서 실수)는 흑백 디스플레이에서는 아예 보이지도 않습니다.** 그래서 텍스트만 찍고 끝내지 않고, R/G/B/Y/M/C/W 색상 막대를 화면에 함께 그리도록 만들었습니다 — 예를 들어 "R" 막대가 파란색으로 나오면 MADCTL의 BGR/RGB 비트가 뒤집혔다는 뜻이고, 색이 전부 미묘하게 이상하면 픽셀 데이터를 리틀엔디안으로 보내고 있는 게 아닌지 의심할 수 있습니다.

## 9. Devicetree

```dts
&spi0 {
    #address-cells = <1>;
    #size-cells = <0>;
    status = "okay";
    pinctrl-0 = <&spi_mstr_mosi &spi_mstr_miso &spi_mstr_clk &spi_mstr_cs>;
    pinctrl-names = "default";

    st7735: st7735@0 {
        compatible = "zds,st7735";
        reg = <0>;
        spi-max-frequency = <4000000>;
        reset-gpios = <&gpioa 17 GPIO_ACTIVE_LOW>;
        dc-gpios = <&gpioa 18 GPIO_ACTIVE_HIGH>;
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

`reset-gpios`/`dc-gpios` 이름은 Zephyr 공식 `mipi-dbi-spi` 바인딩과 맞췄고, GPIO 컨트롤러는 `&gpioa`(전용/비공유 32핀 컨트롤러)를 사용합니다. CS는 네이티브 하드웨어 CS(`spi_mstr_cs`)를 그대로 씁니다 — 별도 `cs-gpios` 없이 `reg` 값으로 컨트롤러가 내부적으로 CS를 제어합니다. `spi_mstr_miso`는 이 모듈이 쓰지 않지만, 이 시리즈의 다른 SPI 랩과 pinctrl을 통일하기 위해 함께 넣어뒀습니다.

## 10. 커스텀 devicetree 바인딩

```yaml
description: |
  ST7735-based 128x160 color TFT ("green tab" variant), driven over raw
  SPI from application code - no Zephyr Display/CFB subsystem involved.

compatible: "zds,st7735"

include: spi-device.yaml

properties:
  reset-gpios:
    type: phandle-array
    required: true
    description: >
      Reset pin. Active low - pulse low to reset the ST7735 controller.

  dc-gpios:
    type: phandle-array
    required: true
    description: >
      Data/Command select pin. Driven low before a command byte is
      written, high before a data byte is written.
```

## 11. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20.0)

list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(st7735_tft_lab)

target_sources(app PRIVATE src/main.c)
```

커스텀 바인딩을 쓰기 때문에 `DTS_ROOT`를 `find_package(Zephyr...)`보다 먼저 추가해야 합니다.

## 12. prj.conf

```
CONFIG_SPI=y
CONFIG_GPIO=y
CONFIG_PRINTK=y
```

## 13. 폴더 구성

```
08_TFT_ST7735/
├── 08_TFT_ST7735_KR.md
├── 08_TFT_ST7735_EN.md
└── lab/
    ├── src/
    │   └── main.c
    ├── boards/
    │   └── sr100_rdk_sr100_m55.overlay
    ├── dts/
    │   └── bindings/
    │       └── display/
    │           └── zds,st7735.yaml
    ├── CMakeLists.txt
    ├── prj.conf
    └── sample.yaml
```

## 14. Build & Run

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\08_TFT_ST7735\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <openocd 경로> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

콘솔은 UART0의 대체 핀(GPIO44/45)으로 옮겨져 있습니다 — 외부 USB-TTL 어댑터를 **J24 13/14번 핀**에 연결하고 **230400bps 8N1**로 여십시오.

### 예상 시리얼 출력

```
ST7735 TFT lab starting
ST7735 initialized and demo screen (text + color bars) drawn
```

화면에는 검정 배경 위에 흰색 `Hello World!`, 청록색 `ST7735 TFT`, 그리고 그 아래 빨강/초록/파랑/노랑/마젠타/시안/흰색 순서의 색상 막대 7개가 보여야 합니다.

## 15. 관찰 포인트

- **색상 막대가 이번 랩의 실질적인 첫 번째 트러블슈팅 도구**입니다 — 텍스트가 하얗게 나오는지보다, 막대 색깔과 순서가 R/G/B/Y/M/C/W 그대로 맞는지부터 확인하십시오
- 화면 가장자리가 몇 픽셀 잘려 보이거나 어긋나 보이면 탭 색상(오프셋) 문제일 가능성이 높습니다 (4절 참고) — 이는 물리적으로 대응할 방법이 없는, 순수하게 소프트웨어 상수로 해결해야 하는 문제입니다
- 프레임버퍼 없이 필요한 영역만 그때그때 그리는 이번 방식은, Nokia 5110의 "매번 전체 프레임버퍼를 통으로 전송" 방식과 대조됩니다 — 화면이 커질수록 전체 프레임버퍼를 메모리에 들고 있는 게 부담스러워지는 지점을 보여주는 사례이기도 합니다

## 16. 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| `SPI device not ready` / `RST/DC GPIO not ready` | 오버레이 미적용 — 오버레이 파일명(`sr100_rdk_sr100_m55.overlay`)이 west board target과 일치하는지 확인 |
| 화면이 완전히 하양/검정/무반응 | RST/DC 배선 재확인. SPI는 I2C와 달리 ACK가 없어서 배선이 틀려도 에러 없이 조용히 이렇게만 나타남 |
| `spi_write_dt(...) failed, ret=-116` | SPI0 FIFO 8바이트 제한 문제 — `ST7735_CHUNK_BYTES`가 8 이하로 되어 있는지 확인 (이미 반영되어 있음) |
| 콘솔에 아무 출력도 안 보임 | 외부 USB-TTL 어댑터가 **J24 13/14번 핀**에 연결됐는지 확인 — 보드 기본 콘솔(J25 13/14번 핀)은 SPI0가 켜지면 죽습니다 |
| 색상 막대의 색이 서로 뒤바뀜(예: R이 파랗게 보임) | MADCTL의 BGR/RGB 비트가 이 모듈과 안 맞음 — `init_seq[]`의 MADCTL(`0x36`) 인자값(`0xC8`)에서 BGR 비트를 토글해보기 |
| 이미지가 위/왼쪽으로 잘리거나 반대쪽에 여백이 생김 | 탭 색상 오프셋 불일치 — `ST7735_XSTART`/`ST7735_YSTART`를 0으로 바꿔보기 (red/black tab 계열) |
| 화면이 좌우/상하 반전되어 보임 | MADCTL의 MX/MY 비트 문제 — `0xC8`의 상위 2비트(MY, MX)를 조정 |
| 글자가 깨지거나 위치가 이상함 | `st7735_draw_char`의 6x8 픽셀 배치, 또는 폰트 테이블 확인 |
| `'zds,st7735' compatible not found` | `CMakeLists.txt`의 `list(APPEND DTS_ROOT ...)`가 `find_package(Zephyr...)` 이전에 있는지 확인 |

## 17. 다음

이 랩까지 오면서 raw SPI + 커스텀 바인딩으로 흑백 캐릭터 LCD(02번, I2C) → 흑백 그래픽 LCD(07번, SPI) → 컬러 TFT(08번, SPI)까지 확장했습니다.
