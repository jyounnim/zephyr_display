# 1. I2C Bus Scanner — Zephyr (Synaptics SR110, sr100_rdk/sr100/m55)

> **SR110 포팅 노트 (2026-09-01)**: 원래 ESP32-S3-DevKitC-1용으로 작성된 랩을 SR110으로 포팅했습니다. I2C 개념 설명(2절)은 플랫폼 무관하게 그대로 유효하지만, **배선 핀, devicetree 오버레이, 그리고 프로빙 방식(read vs write)은 SR110 기준으로 완전히 바뀌었습니다** — 특히 프로빙 방식은 ESP32-S3와 정반대로 뒤집혔으니 5절을 반드시 새로 읽어주세요.

부팅 시 별도 스레드에서 I2C0을 한 번 스캔하고, ACK를 보내는 디바이스를
찾아 `i2cdetect` 스타일의 그리드로 출력하는 예제입니다. 새 센서/디스플레이
모듈을 보드에 연결했을 때 어떤 주소에 잡히는지 확인하는 용도로 씁니다.
`Zephyr_display` 프로젝트의 2번(OLED I2C), 이후 SHARP/Nokia/ST7735
실습에서 배선을 확인할 때도 계속 재사용합니다.

## 폴더 구성

```
Zephyr_display/
└── 01_I2C_bus_scanner/
    ├── lab/
    │   ├── src/
    │   │   └── main.c              # 스캐너 로직 (주석 영문)
    │   ├── boards/
    │   │   └── sr100_rdk_sr100_m55.overlay   # I2C0 활성화 오버레이
    │   ├── CMakeLists.txt
    │   ├── prj.conf
    │   └── sample.yaml
    └── 01_I2C_bus_scanner_KR.md     # 본 문서
```

## I2C 버스 개념 정리

| 개념 | 설명 |
|---|---|
| 버스 공유 | SDA/SCL 두 선에 여러 장치가 병렬로 매달림 |
| 주소 | 각 장치가 7비트 주소(0x08~0x77 범위)를 가짐 — 이게 스캔이 가능한 이유 |
| ACK/NACK | 마스터가 주소를 보내면, 그 주소를 쓰는 장치만 ACK로 응답. 아무도 없으면 NACK |
| 스캔 원리 | "이 주소에 누구 있어?"를 물어볼 표준 명령은 없지만, **주소만 보내고 데이터 없이 끝내는(zero-length) 트랜잭션**을 순회하면서 ACK 여부만 확인하면 어떤 장치든 존재 여부를 알 수 있음 |

## 배선

I2C의 두 신호선 이름은 **SDA**(Serial **DA**ta, 데이터)와 **SCL**(Serial **CL**ock, 클럭)입니다.

> ⚠️ **SCK/SCLK는 SPI 용어입니다.** I2C의 클럭 라인은 항상 **SCL**이라고 부릅니다 — 모듈 실크스크린에 "SCK"나 "CLK"로 인쇄된 경우도 있어 헷갈리기 쉬운데, I2C 모듈(핀이 보통 VCC/GND/SDA/SCL 4개)이라면 그 핀이 곧 SCL입니다.

| 신호 | 역할 | SR110 연결 (이 실습 오버레이 기준) |
|---|---|---|
| VCC | 전원 | 3.3V |
| GND | 그라운드 | GND |
| **SDA** | 데이터 | I2C0 SDA (핀 그룹 `i2c0_ms_sda`) |
| **SCL** | 클럭 (SPI의 SCK에 해당) | I2C0 SCL (핀 그룹 `i2c0_ms_scl`) |

ESP32-S3는 GPIO 매트릭스로 임의의 GPIO 번호에 `pinmux = <I2C0_SDA_GPIO8>` 식으로 자유롭게 배정할 수 있었지만, SR110은 SoC 패키지 단에서 핀 배치가 고정돼 있어 이런 매크로 자체가 없습니다. I2C0의 핀 그룹 이름은 `i2c0_ms_scl` / `i2c0_ms_sda`로 이미 고정돼 있고(suffix 없음 — I2C1의 대체 핀 그룹만 `_b` suffix가 붙음), 오버레이에서는 이 이름을 참조만 하면 됩니다. 실제 헤더의 어느 핀이 이 신호에 해당하는지는 Synaptics Platform Guide/보드 실크스크린에서 확인하세요.

```dts
&i2c0_ms_scl {
    bias-pull-up;
};
&i2c0_ms_sda {
    bias-pull-up;
};
```

## 동작 방식

1. `K_THREAD_DEFINE`으로 정의된 전용 스레드(`scan_tid`)가 부팅 시 자동 시작되어 I2C0 스캔 수행 (`main()`은 아무 일도 안 하고 바로 반환)
2. 각 주소(0x08~0x77)에 **길이 0짜리 write**를 시도해 ACK 여부로 디바이스 존재 판단
3. 스캔 종료 후 발견된 디바이스 개수와 주소 맵 출력
4. 한 번 스캔하고 스레드 종료 (반복 없음)

## 프로빙 방식 — SR110에서는 write가 아니라 1바이트 read (ESP32-S3와 정반대)

원래 ESP32-S3 버전은 Zephyr 공식 샘플(`samples/drivers/i2c/i2c_scanner`)과 동일하게 **길이 0짜리 write**로 프로빙했습니다. ESP32 계열 I2C 드라이버가 `i2c_read()`의 NACK 감지를 완전히 신뢰할 수 없다는 Zephyr GitHub 이슈 #45008("esp32: i2c_read() error was returned successfully at the bus nack")을 피하기 위해서였습니다.

**SR110(`snps,designware-i2c` 드라이버)에서는 상황이 정반대인 것이 실기로 확인됐습니다**: zero-length write는 물론 1바이트 dummy write 프로빙도 실제로 존재하는 디바이스를 놓치고, 대신 **1바이트 read 프로빙이 정상적으로 ACK/NACK을 감지**합니다. 그래서 이 포팅에서는 프로빙 방식을 완전히 뒤집었습니다.

```c
static bool i2c_probe_addr(const struct device *bus, uint8_t addr)
{
    uint8_t dummy;
    int ret = i2c_read(bus, &dummy, 1, addr);
    return (ret == 0);
}
```

> ⚠️ 이 예제를 또 다른 SoC/보드로 다시 포팅할 계획이라면, "read가 되네 write가 되네"를 절대 가정하지 말고 그 플랫폼에서 다시 실기로 검증하세요 — 이 랩 자체가 "같은 문제, 플랫폼마다 정반대 해법"의 실사례입니다.

## Devicetree — I2C0 활성화

SR110의 I2C0은 base devicetree에서 기본적으로 `status = "disabled"`이고 **pinctrl-0 자체가 아예 없어서**, 오버레이에서 새로 켜줘야 합니다 (ESP32-S3처럼 "보드 기본값을 같은 이름으로 재선언해서 덮어쓰는" 트릭이 필요 없음 — SR110은 애초에 기본값이 없으므로 그냥 켜기만 하면 됩니다).

```dts
&i2c0_ms_scl {
    bias-pull-up;
};

&i2c0_ms_sda {
    bias-pull-up;
};

&i2c0 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_STANDARD>;
    pinctrl-0 = <&i2c0_ms_scl &i2c0_ms_sda>;
    pinctrl-names = "default";
};
```

I2C0의 핀 그룹(`i2c0_ms_scl`/`i2c0_ms_sda`)은 SPI0/UART0/UART1이 공유하는 핀 그룹과 완전히 분리된 별도 LPS 도메인 슬롯이라, 04/05/07/08번 SPI 랩들과 달리 콘솔 UART와 충돌할 일이 없습니다.

## Build

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\01_I2C_bus_scanner\lab\
```

## Flash & 콘솔

```bash
python srsdk_tools/openocd_flash.py --openocd <openocd 경로> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

이 랩은 I2C0만 쓰고 UART 핀을 건드리지 않으므로, 콘솔은 보드 기본값(UART1, GPIO23=TX/GPIO24=RX, J25 헤더에 외부 USB-TTL 어댑터 연결) 그대로 **230400bps 8N1**로 사용하면 됩니다.

## 결과 예

```
=== I2C Bus Scanner (SR110) ===

Scanning I2C0...
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- 3c -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
70: -- -- -- -- -- -- -- --
Scan complete on I2C0: 1 device(s) found
```

(위 예시는 0x3C에 SSD1306 OLED 하나만 연결된 경우입니다 — 2번 실습에서 실제로 이 상태를 만들게 됩니다)

## 관찰 포인트

- 이 스캐너는 **`Zephyr_display` 프로젝트 전체에서 반복 재사용할 진단 도구**입니다 — 이후 실습에서 "장치가 응답 안 함" 같은 문제가 생기면, 가장 먼저 이 스캐너부터 돌려서 실제로 그 주소가 잡히는지 확인하는 습관을 들이세요
- ESP32-S3(zero-length write)와 SR110(1바이트 read)에서 정반대 프로빙 방식이 필요했던 이번 포팅은, **"같은 문제라도 플랫폼(드라이버)마다 정답이 다를 수 있다"**는 걸 보여주는 사례입니다 — 다른 SoC로 또 옮길 때는 이 부분을 절대 그대로 재사용하지 말고 반드시 실기로 재검증하세요
- 스캔 결과에 예상 밖의 주소가 나오면, 그 자체로 유용한 정보입니다 — 배선 오류(다른 장치가 잘못 붙음), 혹은 알고 있던 것과 다른 주소를 쓰는 모듈(핀 점퍼로 주소가 바뀌는 경우 등)일 수 있습니다

## 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| `[I2C0] device not ready` | 오버레이가 실제로 적용 안 됨 — 오버레이 파일명(`sr100_rdk_sr100_m55.overlay`)이 west board target과 일치하는지 확인 |
| 아무 주소도 안 잡힘 | 배선(SDA/SCL) 확인, 풀업 저항 확인, `pinctrl-0`에 `i2c0_ms_scl`/`i2c0_ms_sda`가 실제로 들어갔는지 `west build -t devicetree` 결과로 확인 |
| 특정 주소만 반복적으로 못 잡힘, 특히 write로는 되던 게 read에서 안 됨 | 프로빙 방식이 read↔write로 잘못 되돌아가 있는지 확인 — SR110은 반드시 **1바이트 read** 프로빙이어야 함 (위 프로빙 방식 절 참고) |
| 컴파일 에러 (`i2c0_ms_scl`/`i2c0_ms_sda`를 못 찾음) | `sr100_pinctrl.dtsi`가 include 안 된 상태 — board.dts가 정상 로드됐는지, west 타겟이 `sr100_rdk/sr100/m55`가 맞는지 확인 |

## 다음

2번 실습(`02_I2C_LCD_LAB`)에서 이 스캐너로 확인한 배선 위에 실제 I2C LCD를 올립니다.
