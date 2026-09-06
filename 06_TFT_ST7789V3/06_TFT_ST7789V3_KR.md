# Lab 06: TFT ST7789V3 (1.69인치 240x280, raw SPI)

## 1. 개요

보드는 **Synaptics SR110** (`sr100_rdk/sr100/m55`), 프레임워크는 **Zephyr RTOS**를 사용합니다.

> **포팅 히스토리 (2026-09-03)**: 원래 ESP32-S3-DevKitC-1용으로 작성된 랩입니다 (원래 06번 자리에 있던 Sharp memory display 모듈에 문제가 있어 이 랩으로 교체됨). 처음 SR110으로 포팅할 때 소프트웨어 CS, CS 연속 유지, TX+RX 강제 전송 등 여러 방법을 시도했지만 전부 화면이 안 나왔습니다. **05번 랩(SSD1306 SPI)과 08번 랩(ST7735)이 네이티브 하드웨어 CS + 평범한 개별 SPI 쓰기로 실기 성공**한 것을 확인한 뒤, SPI0/CS/핀/청크 분할 쪽은 문제가 아니라는 게 확정됐습니다. 08번(전원제어/감마 보정까지 포함한 상세 초기화)과 이 랩(명령 7개짜리 최소 초기화)을 비교해서 **초기화 시퀀스 자체가 부족했다는 게 원인으로 확인**됐습니다 — 포치(porch)/게이트/VCOM/전원제어/감마 설정 명령을 추가한 표준 초기화 시퀀스로 교체한 뒤 **실기로 정상 동작까지 확인**했습니다.

Sitronix **ST7789V3** 컨트롤러를 쓰는 1.69인치 240x280 컬러 TFT 패널을 SPI로 구동합니다. Zephyr의 Display/CFB 서브시스템이나 in-tree `sitronix,st7789v` 드라이버를 쓰지 않고, 이 시리즈의 다른 SPI 디스플레이 랩과 동일하게 **raw SPI**로 컨트롤러를 직접 제어합니다.

## 2. 배선

| 신호 | 역할 | SR110 연결 |
|---|---|---|
| VCC | 전원 | 3.3V (아래 4절 전원 주의 참고) |
| GND | 그라운드 | GND |
| SCL/SCLK | SPI 클럭 | SPI0 CLK (SoC GPIO22, **J25 11번 핀**) |
| SDA/MOSI | SPI 데이터 | SPI0 MOSI (SoC GPIO23, **J25 14번 핀**) |
| CS | 칩 셀렉트 | SPI0 CS, 네이티브 하드웨어 CS (SoC GPIO21, **J25 12번 핀**) |
| RES/RST | 리셋 | SoC GPIO17, **J24 3번 핀** |
| DC | Data/Command 선택 | SoC GPIO18, **J24 4번 핀** |
| BLK | 백라이트 | 3.3V (아래 참고) |

MISO는 배선하지 않습니다 — 이 패널은 MCU 쪽에서 보면 write-only입니다 (오버레이의 `pinctrl-0`에는 이 시리즈 관례대로 MISO(SoC GPIO24, J25 13번 핀)도 같이 넣어뒀습니다).

**BLK(백라이트) 핀**: 모듈에 BLK 핀이 별도로 나와 있다면 3.3V에 연결해야 백라이트가 켜집니다 — GPIO로 제어할 필요 없이 전원에 그대로 묶으면 됩니다.

> ✅ **RST/DC는 05번 랩과 동일한 핀을 재사용합니다.** GPIO17/18은 05번 랩(SSD1306 SPI)에서 이미 정상 부팅과 실제 디스플레이 동작까지 실기로 검증된 핀입니다 — 05번과 06번을 동시에 배선하지 않는다면(각자 별도 빌드이므로) 안전하게 재사용할 수 있습니다. CS도 05번과 동일하게 네이티브 하드웨어 CS를 씁니다.

## 3. 패널 GRAM 오프셋

ST7789 컨트롤러 자체의 네이티브 GRAM은 240x320입니다. 이 랩에서 쓰는 1.69인치 모듈의 실제 유리 패널은 240x280이라, 컨트롤러 GRAM의 가운데 부분만 잘라서 씁니다 — 그래서 주소 창(addressing window)을 지정할 때마다 고정된 오프셋을 더해줘야 합니다.

이 랩은 X 오프셋 0, Y 오프셋 20을 기본값으로 씁니다(1.69인치 240x280 ST7789V3 모듈에서 흔히 문서화되는 값, 보드와 무관한 패널 자체 특성이라 ESP32-S3 버전과 동일합니다). 오프셋은 오버레이의 `x-offset`/`y-offset` 프로퍼티와 `main.c`의 `X_OFFSET`/`Y_OFFSET`(둘 다 devicetree에서 읽어옴)에 있으니, 화면이 밀려 보이거나 잘려 보이면 여기를 조정하면 됩니다.

## 4. 전원 주의 (01번 랩 문서 참고)

> ⚠️ 01번 랩에서 실기로 확인된 대로, 외부 디바이스를 보드 헤더의 3.3V 레일로 전원 공급하면 I2C뿐 아니라 SPI 쓰기에서도 문제가 생길 수 있습니다. 가능하면 외부 3.3V 전원을 사용하십시오.

## 5. Devicetree

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

`main.c`는 `SPI_DT_SPEC_GET()` / `GPIO_DT_SPEC_GET()`으로 이 노드에서 SPI 스펙과 RST/DC GPIO 스펙을 그대로 가져다 씁니다. CS는 devicetree에 별도 `cs-gpios` 없이 `spi_mstr_cs` 핀먹스만으로 네이티브 하드웨어 CS를 사용합니다 — `snps,designware-spi` 드라이버가 `reg` 값(`reg = <0>`)으로 내부 CS를 제어합니다.

> ✅ **SPI0 FIFO 8바이트 제한 (실기 확인)**: SoC devicetree(`sr100_m55.dtsi`)에 SPI0/SPI1의 하드웨어 FIFO 깊이가 `fifo-depth = <8>;` — 8바이트로 명시되어 있습니다. 8바이트를 넘는 단일 `spi_write_dt()` 호출은 중간에 FIFO를 다시 채우는 인터럽트가 필요한데, 이 인터럽트가 이 플랫폼에서 제대로 안 걸려서 `-116`(`-ETIMEDOUT`) 에러로 실패하는 것이 실기로 확인됐습니다. `main.c`의 모든 SPI 전송은 이를 피하기 위해 **8바이트 단위로 쪼개서** 보냅니다. 이건 CS 방식(네이티브/소프트웨어)과 무관하게 이 SoC의 SPI0을 raw SPI로 쓰는 모든 랩에 적용되는 하드웨어 제약이니, 나중에 다른 SPI 디바이스를 포팅할 때도 기억해 두면 좋습니다.

> 🔍 **유력한 근본 원인 (아직 실기 미확인): 초기화 시퀀스 부족**: 위 CS/FIFO 문제를 다 해결한 뒤에도 화면은 계속 검정이었습니다. 08번 랩(ST7735)이 정확히 같은 SPI0/CS/핀/청크 조건에서 성공한 걸 보고 두 랩을 비교한 결과, 이 랩의 초기화 시퀀스가 `SWRESET`/`SLPOUT`/`COLMOD`/`MADCTL`/`INVON`/`NORON`/`DISPON` **7개 명령뿐**이라 포치(`PORCTRL`)/게이트(`GCTRL`)/VCOM(`VCOMS`)/전원제어(`LCMCTRL`/`VDVVRHEN`/`VRHS`/`VDVS`/`FRCTRL2`/`PWCTRL1`)/감마 보정(`PVGAMCTRL`/`NVGAMCTRL`) 설정이 전부 빠져 있다는 걸 확인했습니다. 명령 자체는 정상적으로 전달됐지만(그래서 SPI 에러는 없었음), 패널의 아날로그/타이밍 설정이 안 끝나서 실제로 화면에 아무것도 표시되지 않았을 가능성이 높습니다. ESPHome의 `st7789v` 컴포넌트, Bodmer의 TFT_eSPI 라이브러리, Adafruit의 라즈베리파이 fbtft 드라이버가 공통으로 쓰는 표준 초기화 시퀀스로 교체했으나, **아직 실기 테스트 전**입니다.

## 6. 빌드 & 실행

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

## 7. 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| 화면이 완전히 안 나옴 (검은 화면 그대로, 명령은 에러 없이 전부 성공) | 초기화 시퀀스 부족일 가능성 — 5절 "유력한 근본 원인" 참고. `main.c`의 `init_seq[]`에 `PORCTRL`/`GCTRL`/`VCOMS`/`LCMCTRL`/`VDVVRHEN`/`VRHS`/`VDVS`/`FRCTRL2`/`PWCTRL1`/`PVGAMCTRL`/`NVGAMCTRL`이 전부 들어있는지 확인 |
| 화면이 완전히 안 나옴 (배선 자체가 의심될 때) | 배선(VCC/GND/SCLK/MOSI/CS/RST/DC) 재확인. 보드 레일 전원이면 외부 3.3V로 바꿔서 재시도 (4절 참고) |
| 콘솔에 아무 출력도 안 보임 | 외부 USB-TTL 어댑터가 **J24 13/14번 핀**에 연결됐는지 확인 — 보드 기본 콘솔(J25 13/14번 핀)은 SPI0가 켜지면 죽습니다 |
| `spi_write_dt(...) failed, ret=-116` | SPI0 FIFO 8바이트 제한 문제 — `ST7789_CHUNK_BYTES`가 8 이하로 되어 있는지 확인 (이미 반영되어 있음) |
| 백라이트는 켜지는데 화면이 안 보임 | 패널 초기화 자체는 됐다는 뜻 — BLK 배선이 아니라 SPI/GPIO 배선 문제. RST/DC 극성, CS 핀먹스(`spi_mstr_cs`) 확인 |
| 화면이 켜지긴 하는데 이미지가 밀려 보이거나 일부만 보임 | 3절 참고 — `x-offset`/`y-offset` 값을 모듈 데이터시트 기준으로 조정 |
| 색이 반전되어 보임 (예: 검은 배경이 흰 배경으로 나옴) | `main.c`의 `ST7789_INVON`(`0x21`) 호출을 `0x20`(`INVOFF`)으로 바꿔서 시도 |
| 빨강/파랑이 서로 바뀌어 보임 (RGB ↔ BGR) | `st7789_init()`의 `madctl` 값(`0x00`)에서 비트 3(`0x08`)을 토글해서 시도 |
| 화면이 위아래/좌우로 뒤집혀 보이거나 회전되어 있음 | `madctl` 비트 조합(`0x00`/`0x60`/`0xA0`/`0xC0` 등)을 모듈 실장 방향에 맞게 시도 |
| 화면은 다 채워지는데 텍스트만 안 보이거나 깨짐 | `font5x7` 글꼴 테이블에 없는 문자를 쓰고 있는지, 텍스트 색과 배경색이 같지는 않은지 확인 |

**Lab 06 진행 상황 (2026-09-03)** — SPI0 버스(네이티브 CS), RST/DC(GPIO17/18, 05번과 공용), SPI0 FIFO 8바이트 청크 분할까지는 실기로 확인 완료. 초기화 시퀀스 확장(포치/게이트/VCOM/전원제어/감마 보정)은 08번 랩과의 비교로 도출한 유력한 해법이지만 **아직 실기 검증 전**입니다.

## 8. 파일 구성

```
06_TFT_ST7789V3/
├── 06_TFT_ST7789V3_KR.md
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

## 9. 다음

07번 랩(`07_Nokia5110_display`)으로 이어집니다.
