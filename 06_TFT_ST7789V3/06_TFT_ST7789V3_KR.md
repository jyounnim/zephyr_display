# Lab 06: TFT ST7789V3 (1.69인치 240x280, raw SPI)

## 1. 개요

보드는 **Synaptics SR110** (`sr100_rdk/sr100/m55`), 프레임워크는 **Zephyr RTOS**를 사용합니다.

Sitronix **ST7789V3** 컨트롤러를 쓰는 1.69인치 240x280 컬러 TFT 패널을 SPI로 구동합니다. Zephyr의 Display/CFB 서브시스템이나 in-tree `sitronix,st7789v` 드라이버를 쓰지 않고, 이 시리즈의 다른 SPI 디스플레이 랩과 동일하게 **raw SPI**로 컨트롤러를 직접 제어합니다.

이 랩은 **실기로 완전히 검증 완료**된 상태입니다.

> ⚠️ **이 랩은 레벨 시프터가 필요합니다.** 자세한 이유는 4절을 먼저 읽어주세요 — 배선이 이 시리즈의 다른 SPI 랩들과 다릅니다.

## 2. 준비물

- ST7789V3 1.69인치 240x280 컬러 TFT 모듈 (IOVCC 3.3V 사양)
- Synaptics SR110 보드
- **양방향 로직 레벨 시프터** (1.8V ↔ 3.3V, 5채널 이상 — 예: TXS0108E). 4절 참고
- 콘솔 확인용 외부 USB-TTL 어댑터

## 3. 배선

| 신호 | 역할 | SR110 쪽 (1.8V) | 레벨 시프터 | 디스플레이 쪽 (3.3V) |
|---|---|---|---|---|
| VCC | 전원 | - | - | 3.3V |
| GND | 그라운드 | GND | GND (양쪽 공통) | GND |
| SCL/SCLK | SPI 클럭 | SPI0 CLK (SoC GPIO22, **J25 11번 핀**) | CH_A ↔ CH_B | SCL |
| SDA/MOSI | SPI 데이터 | SPI0 MOSI (SoC GPIO23, **J25 14번 핀**) | CH_A ↔ CH_B | SDA |
| CS | 칩 셀렉트 | SPI0 CS, 네이티브 하드웨어 CS (SoC GPIO21, **J25 12번 핀**) | CH_A ↔ CH_B | CS |
| RES/RST | 리셋 | SoC GPIO17, **J24 3번 핀** | CH_A ↔ CH_B | RES |
| DC | Data/Command 선택 | SoC GPIO18, **J24 4번 핀** | CH_A ↔ CH_B | DC |
| BLK | 백라이트 | - | - | 3.3V (직결, 레벨 시프터 안 거침) |

레벨 시프터의 저전압 쪽(VCCA)은 **1.8V**, 고전압 쪽(VCCB)은 **3.3V**에 연결하고, TXS0108E를 쓴다면 **OE 핀은 1.8V(VCCA)**에 연결하십시오. MISO는 배선하지 않습니다 — 이 패널은 write-only입니다.

## 4. 왜 레벨 시프터가 필요한가

SR110의 SPI0/GPIO 핀은 회로도(SC950-C01116-01 RevE)에 **1.8V I/O 도메인**(`SR110_VDDIO1P8`)으로 명시되어 있습니다 — 보드 헤더에 3.3V 전원 핀이 있는 것과는 별개로, 실제 신호 로직 레벨은 1.8V입니다.

반면 이 ST7789V3 모듈은 **IOVCC 3.3V** 사양입니다. ST7789 계열의 일반적인 입력 High 인식 기준(VIH = 0.7 × IOVCC)을 적용하면:

```
VIH_min = 0.7 × 3.3V = 2.31V
```

SR110이 내보내는 1.8V는 이 임계값(2.31V)에 못 미칩니다. 이 상태로는 SPI 전송 자체는 에러 없이 "성공"으로 보이지만(SR110 입장에서는 자기가 신호를 내보냈다는 것만 확인할 뿐, 상대가 실제로 논리 1로 인식했는지 확인할 방법이 없습니다), 패널은 명령을 제대로 못 받아 화면이 계속 꺼진 상태로 남습니다.

**SCLK/MOSI/CS/RST/DC 5개 신호 전부**를 레벨 시프터로 1.8V→3.3V 변환해서 해결했습니다.

> **참고**: 이 시리즈의 05번(SSD1306)·08번(ST7735) 랩은 레벨 시프터 없이 1.8V로도 잘 동작합니다. 이건 모순이 아니라 — 반도체 입력 임계값은 데이터시트 스펙보다 마진이 있는 경우가 흔해서, 특정 칩/모듈은 스펙 미달 전압에서도 우연히 동작하는 것뿐입니다. **3.3V 사양 SPI 디스플레이를 1.8V MCU에 물릴 때는 항상 레벨 시프터를 기본값으로 쓰고, 안 써도 되는지는 실기로 확인하는 방향을 권장합니다.**
>
> **TXS0108E를 쓴다면 참고**: TXS0108E는 자동 방향 감지 방식이라 원래 SPI 클럭처럼 계속 토글하는 단방향 신호에는 잘 안 맞는 용도입니다. 배선 기생용량에 따라 다르지만 대략 1~2MHz 이상에서 불안정해질 수 있습니다. 이 랩은 4MHz로 검증됐지만, 본인 배선에서 같은 증상(에러 없이 화면 안 나옴)이 재현되면 클럭을 더 낮추거나 74LVC245/74AHCT125 같은 **단방향 버퍼**로 바꿔보십시오 — SCLK/MOSI/CS/RST/DC 5개 신호 전부 SR110→디스플레이 방향뿐이라 원래 단방향 버퍼가 더 적합한 선택입니다.

## 5. 콘솔 주의

SR110의 유일한 SPI 마스터(SPI0)를 켜면 보드 기본 콘솔(UART1, GPIO23/24, J25 13/14번 핀)이 막힙니다. 이 랩의 오버레이는 콘솔을 UART0의 대체 핀(GPIO44/45, **J24 13/14번 핀**)으로 옮겨뒀습니다 — 외부 USB-TTL 어댑터를 그쪽에 연결하십시오.

## 6. 패널 GRAM 오프셋

ST7789 컨트롤러 자체의 네이티브 GRAM은 240x320입니다. 이 랩에서 쓰는 1.69인치 모듈의 실제 유리 패널은 240x280이라, 컨트롤러 GRAM의 가운데 부분만 잘라서 씁니다 — 그래서 주소 창(addressing window)을 지정할 때마다 고정된 오프셋을 더해줘야 합니다.

이 랩은 X 오프셋 0, Y 오프셋 20을 기본값으로 씁니다(1.69인치 240x280 ST7789V3 모듈에서 흔히 문서화되는 값). 오프셋은 오버레이의 `x-offset`/`y-offset` 프로퍼티에 있으니, 화면이 밀려 보이거나 잘려 보이면 여기를 조정하면 됩니다.

## 7. Devicetree

```dts
&spi0 {
    #address-cells = <1>;
    #size-cells = <0>;
    status = "okay";
    pinctrl-0 = <&spi_mstr_mosi &spi_mstr_miso &spi_mstr_clk &spi_mstr_cs>;
    pinctrl-names = "default";

    st7789v_disp: st7789v@0 {
        compatible = "zds,st7789v";
        reg = <0>;
        spi-max-frequency = <4000000>;
        reset-gpios = <&gpioa 17 GPIO_ACTIVE_LOW>;
        dc-gpios = <&gpioa 18 GPIO_ACTIVE_HIGH>;
        width = <240>;
        height = <280>;
        x-offset = <0>;
        y-offset = <20>;
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

`main.c`는 `SPI_DT_SPEC_GET()` / `GPIO_DT_SPEC_GET()`으로 이 노드에서 SPI 스펙과 RST/DC GPIO 스펙을 그대로 가져다 씁니다. CS는 devicetree에 별도 `cs-gpios` 없이 `spi_mstr_cs` 핀먹스만으로 네이티브 하드웨어 CS를 사용합니다.

> ✅ **SPI0 FIFO 8바이트 제한**: SoC devicetree(`sr100_m55.dtsi`)에 SPI0/SPI1의 하드웨어 FIFO 깊이가 `fifo-depth = <8>;` — 8바이트로 명시되어 있습니다. 8바이트를 넘는 단일 `spi_write_dt()` 호출은 중간에 FIFO를 다시 채우는 인터럽트가 필요한데, 이 인터럽트가 이 플랫폼에서 제대로 안 걸려서 `-116`(`-ETIMEDOUT`) 에러로 실패합니다. `main.c`의 모든 SPI 전송은 이를 피하기 위해 **8바이트 단위로 쪼개서** 보냅니다. CS 방식(네이티브/소프트웨어)과 무관하게 이 SoC의 SPI0을 raw SPI로 쓰는 모든 랩에 적용되는 하드웨어 제약이니, 다른 SPI 디바이스를 포팅할 때도 기억해 두면 좋습니다.

## 8. 초기화 시퀀스

포치(porch)/게이트/VCOM/전원제어/감마 보정까지 포함한 상세 초기화 시퀀스를 씁니다 — ESPHome의 `st7789v` 컴포넌트, Bodmer의 TFT_eSPI 라이브러리, Adafruit의 라즈베리파이 fbtft 드라이버가 공통으로 쓰는 값과 교차 검증했습니다.

| 명령 | 인자 | 의미 |
|---|---|---|
| SWRESET (`0x01`) | - | 소프트웨어 리셋, 150ms 대기 |
| SLPOUT (`0x11`) | - | 슬립 아웃, 255ms 대기 |
| COLMOD (`0x3A`) | `0x55` | 픽셀 포맷 = 16비트(RGB565) |
| PORCTRL (`0xB2`) | 5바이트 | 포치 설정 |
| GCTRL (`0xB7`) | 1바이트 | 게이트 제어 |
| VCOMS (`0xBB`) | 1바이트 | VCOM 전압 |
| LCMCTRL (`0xC0`) | 1바이트 | LCM 제어 |
| VDVVRHEN (`0xC2`) | 2바이트 | VDV/VRH 활성화 |
| VRHS (`0xC3`) | 1바이트 | VRH 설정 |
| VDVS (`0xC4`) | 1바이트 | VDV 설정 |
| FRCTRL2 (`0xC6`) | 1바이트 | 프레임 레이트 제어 |
| PWCTRL1 (`0xD0`) | 2바이트 | 전원 제어 |
| MADCTL (`0x36`) | `0x00` | 회전/RGB 순서 |
| INVON (`0x21`) | - | 색상 반전 켬 |
| PVGAMCTRL/NVGAMCTRL (`0xE0`/`0xE1`) | 각 14바이트 | 감마 보정 테이블 |
| NORON (`0x13`) | - | 정상 표시 모드 |
| DISPON (`0x29`) | - | 화면 켜짐 |

화면이 상하좌우 반전되어 보이면(모듈마다 유리 패널이 뒤집혀 실장된 경우가 있음) `MADCTL`/`INVON` 값을 조정해서 재시도하십시오 (자세한 비트 조합은 10절 참고).

## 9. Build & Run

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\06_TFT_ST7789V3\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <openocd 경로> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

콘솔은 UART0의 대체 핀(`uart0_tx_c`/`uart0_rx_c` = GPIO44/45)으로 옮겨져 있습니다 — 외부 USB-TTL 어댑터를 **J24 13/14번 핀**에 연결하고 **230400bps 8N1**로 여십시오.

### 예상 시리얼 출력

```
=== TFT ST7789V3 (SPI, 240x280) ===
ST7789V3 initialized, color bars + "Hello World!" drawn
```

패널 화면 상단에 `Hello World!`가, 그 아래에 빨강/초록/파랑/노랑/시안/마젠타/흰색 색상 막대가 검정 배경 위에 채워져 있어야 합니다.

## 10. 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| 화면이 완전히 안 나옴 (검은 화면 그대로, 로그는 에러 없이 정상) | **레벨 시프터 없이 직결한 경우 가장 흔한 원인입니다.** 4절 참고 — SCLK/MOSI/CS/RST/DC 5개 신호 모두 1.8V→3.3V 레벨 시프터를 거쳤는지 확인 |
| 레벨 시프터를 달았는데도 동일 증상 | TXS0108E를 쓰고 있다면 SPI 클럭이 너무 빠를 수 있습니다 — `spi-max-frequency`를 1MHz 이하로 낮춰서 재시도, 또는 단방향 버퍼(74LVC245 등)로 교체 |
| 콘솔에 아무 출력도 안 보임 | 외부 USB-TTL 어댑터가 **J24 13/14번 핀**에 연결됐는지 확인 — 보드 기본 콘솔(J25 13/14번 핀)은 SPI0가 켜지면 죽습니다 |
| `spi_write_dt(...) failed, ret=-116` | SPI0 FIFO 8바이트 제한 문제 — `ST7789_CHUNK_BYTES`가 8 이하로 되어 있는지 확인 (이미 반영되어 있음) |
| 화면이 켜지긴 하는데 이미지가 밀려 보이거나 일부만 보임 | 6절 참고 — `x-offset`/`y-offset` 값을 모듈 데이터시트 기준으로 조정 |
| 색이 반전되어 보임 (예: 검은 배경이 흰 배경으로 나옴) | `main.c`의 `ST7789_INVON`(`0x21`) 호출을 `0x20`(`INVOFF`)으로 바꿔서 시도 |
| 빨강/파랑이 서로 바뀌어 보임 (RGB ↔ BGR) | `st7789_init()`의 `madctl` 값(`0x00`)에서 비트 3(`0x08`)을 토글해서 시도 |
| 화면이 위아래/좌우로 뒤집혀 보이거나 회전되어 있음 | `madctl` 비트 조합(`0x00`/`0x60`/`0xA0`/`0xC0` 등)을 모듈 실장 방향에 맞게 시도 |
| 화면은 다 채워지는데 텍스트만 안 보이거나 깨짐 | `font5x7` 글꼴 테이블에 없는 문자를 쓰고 있는지, 텍스트 색과 배경색이 같지는 않은지 확인 |

**Lab 06 완전히 검증 완료 (2026-09-04)** — 레벨 시프터(1.8V↔3.3V) 필요성, SPI0 버스(네이티브 CS), RST/DC(GPIO17/18), SPI0 FIFO 8바이트 청크 분할, 확장 초기화 시퀀스까지 전부 실기로 확인 완료.

## 11. 파일 구성

```
06_TFT_ST7789V3/
├── 06_TFT_ST7789V3_KR.md
├── 06_TFT_ST7789V3_EN.md
└── lab/
    ├── CMakeLists.txt
    ├── prj.conf
    ├── sample.yaml
    ├── boards/
    │   └── sr100_rdk_sr100_m55.overlay
    ├── dts/
    │   └── bindings/
    │       └── display/
    │           └── zds,st7789v.yaml
    └── src/
        └── main.c
```

## 12. 다음

07번 랩(`07_Nokia5110_display`)으로 이어집니다.
