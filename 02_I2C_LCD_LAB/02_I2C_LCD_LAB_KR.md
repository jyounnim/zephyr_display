# Lab 02: I2C LCD (PCF8574 + HD44780, "LiquidCrystal-I2C" 스타일)

## 1. 개요

보드는 **Synaptics SR110** (`sr100_rdk/sr100/m55`), 프레임워크는 **Zephyr RTOS**를 사용합니다.

> **SR110 포팅 노트 (2026-09-01)**: 원래 ESP32-S3-DevKitC-1용으로 작성된 랩입니다. PCF8574 비트맵(3절)과 HD44780 초기화 시퀀스(6절)는 플랫폼 무관하게 그대로 유효하지만, 배선 핀(4절)과 devicetree 오버레이, 그리고 부팅 시 bus scan의 프로빙 방식은 SR110 기준으로 바뀌었습니다 — 01번 랩(`01_I2C_bus_scanner`)에서 확인된 것과 동일하게, SR110에서는 zero-length/1-byte dummy `i2c_write()` 프로빙이 아니라 **1바이트 `i2c_read()`** 프로빙을 써야 합니다.

흔히 "LCM1602 IIC" 모듈이라고 부르는, HD44780 호환 16x2 캐릭터 LCD 뒤에 **PCF8574(A) I2C GPIO 익스팬더**가 붙어 있는 백팩 보드를 사용합니다. 아두이노의 `LiquidCrystal_I2C` 라이브러리가 하는 일을 그대로 Zephyr에서 raw I2C로 재구현한 것입니다 (Zephyr의 Display/CFB 서브시스템은 쓰지 않음).

- PCF8574가 I2C로 받은 8비트 값을 그대로 8개의 GPIO 핀(P0-P7)으로 내보내고,
- 그 8개 핀이 HD44780의 RS/RW/EN + 백라이트 트랜지스터 + 4비트 데이터버스(D4-D7)에 배선되어 있습니다.

즉 "LCD를 향한 I2C 통신"이 아니라, **"GPIO 익스팬더를 향한 I2C 통신 + 그 뒤의 병렬 LCD 프로토콜을 흉내"** 내는 구조입니다.

<img width="576" height="482" alt="image" src="https://github.com/user-attachments/assets/cb50b0f9-3fa3-49b9-9e91-e79ad8e8e346" />


## 2. 주소: 0x27 vs 0x3F

이 백팩은 어떤 익스팬더 칩이 실장되어 있느냐에 따라 I2C 주소가 다릅니다.

| 실장 칩 | 기본 I2C 주소 |
| --- | --- |
| PCF8574 | 0x27 |
| PCF8574A | 0x3F |

사용자가 가지고 있는 모듈은 **0x3F (PCF8574A)** 입니다. 코드는 부팅 시 I2C 버스를 스캔해서 0x3F가 있으면 그것을, 없으면 0x27을 시도하도록 만들어 두었습니다 (둘 다 없으면 0x3F를 기본값으로 두고 경고만 출력).

## 3. PCF8574 비트 맵 (LiquidCrystal_I2C 표준 배선)

거의 모든 "LCM1602 IIC" 백팩과 그 아두이노 라이브러리 포크들이 공통으로 쓰는 배선입니다.

| PCF8574 핀 | 연결 대상 | 비고 |
| --- | --- | --- |
| P0 | LCD RS | 0=명령, 1=데이터 |
| P1 | LCD RW | 이 랩에서는 항상 0 (write-only, busy flag 안 읽음) |
| P2 | LCD EN | 이 핀을 펄스로 토글해야 니블이 래치됨 |
| P3 | 백라이트 트랜지스터 게이트 | 1=백라이트 켜짐 |
| P4-P7 | LCD D4-D7 | 4비트 데이터 버스, 상위 니블부터 전송 |

만약 실제로 가진 모듈이 이 배선과 다르면(일부 저가형 클론이 다르게 배선된 사례가 보고됨), 커서 위치나 글자가 이상하게 나오는 형태로 증상이 나타납니다 - 통신 자체(I2C ACK)는 정상으로 보일 수 있습니다. 8절 트러블슈팅 표 참고.

## 4. 배선

| 신호 | Synaptics SR110 | 백팩 |
| --- | --- | --- |
| VCC | (아래 5절 참고) | VCC |
| GND | GND | GND |
| SDA | I2C0 SDA (핀 그룹 `i2c0_ms_sda`) | SDA |
| SCL | I2C0 SCL (핀 그룹 `i2c0_ms_scl`) | SCL |

01번 랩과 동일한 I2C0 버스를 그대로 재사용합니다. SR110의 I2C0은 base devicetree에서 기본 `status = "disabled"`이고 pinctrl-0 자체가 없어서, 오버레이에서 새로 켜기만 하면 됩니다 (ESP32-S3처럼 보드 기본값을 같은 이름으로 재선언해 덮어쓰는 트릭이 필요 없습니다). 자세한 배경은 01번 랩 문서를 참고하십시오.

## 5. 전원/신호 레벨 주의 (중요)

이 백팩 + LCD 조합은 보통 **5V 기준으로 설계**되어 있습니다:

- PCF8574 자신의 SDA/SCL 풀업 저항이 보통 모듈 위에 이미 있고, VCC에 연결되어 있습니다. VCC를 5V로 주면 I2C 버스의 idle-high 레벨도 5V가 됩니다.
- SR110의 GPIO도 **3.3V 전용**이며 5V 입력을 정격으로 보장하지 않습니다. 5V 버스를 SR110 GPIO에 직결하면 장기적으로 GPIO가 손상될 수 있습니다.
- LCD 자체의 명암(contrast)도 보통 5V 기준으로 맞춰져 있어서, 3.3V로 구동하면 트리머 포텐셔미터를 조정해도 글자가 흐리게 나올 수 있습니다.

**권장 순서**:

1. 먼저 모듈을 3.3V로 구동해 보고 (VCC -> 보드 3V3), 명암 트리머를 조정해서 글자가 보이는지 확인합니다. 많은 HD44780 패널이 3.3V에서도 동작합니다.
2. 3.3V에서 전혀 안 보이거나 백라이트만 켜지고 글자가 없다면, 5V가 필요한 패널일 가능성이 큽니다. 이 경우:
   - VCC는 보드 5V 핀에서 공급하되,
   - SDA/SCL은 **양방향 로직 레벨 시프터**를 거쳐 SR110 GPIO와 연결하는 것을 권장합니다.
3. "일단 되는지 보자"고 5V 버스를 3.3V GPIO에 직결하는 것은 실제로 많은 취미 프로젝트에서 별 문제 없이 동작하는 사례가 많지만, 정격 밖의 사용이라는 점은 분명히 인지하고 진행하십시오.

이 랩의 코드/오버레이는 어느 전원 방식을 선택하든 동일하게 동작합니다 (전원 배선은 소프트웨어가 알 수 없는 영역).

## 6. HD44780 초기화 시퀀스

RW을 안 쓰고(write-only) busy flag를 읽지 않기 때문에, 초기화와 매 명령 사이에는 데이터시트가 요구하는 지연시간만큼 넉넉하게 기다리는 방식으로 구현했습니다 (Hitachi HD44780U 데이터시트 Figure 24, "4-Bit Interface" 절차):

1. 전원 인가 후 40ms 이상 대기 (코드는 안전하게 50ms)
2. `0x3` 니블 전송, 5ms 대기
3. `0x3` 니블 전송, 150us 대기
4. `0x3` 니블 전송, 150us 대기
5. `0x2` 니블 전송 (4비트 모드로 전환), 150us 대기
6. 이후부터는 정상적인 2-니블(상위→하위) 바이트 명령:
   - Function Set `0x28` (4비트, 2라인, 5x8 폰트)
   - Display Off `0x08` (설정 중 화면 끔)
   - Clear Display `0x01` (2ms 대기 - HD44780에서 느린 명령)
   - Entry Mode Set `0x06` (커서 증가, 화면 시프트 없음)
   - Display Control `0x0C` (화면 켬, 커서/블링크 끔)

## 7. 빌드 & 실행

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\02_I2C_LCD_LAB\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <openocd 경로> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

이 랩도 I2C0만 사용하므로 콘솔은 보드 기본값(UART1, GPIO23=TX/GPIO24=RX, J25 헤더 + 외부 USB-TTL 어댑터, **230400bps 8N1**) 그대로 사용하면 됩니다.

### 예상 시리얼 출력

```
I2C LCD (PCF8574 + HD44780) lab starting
Scanning I2C0 bus (0x08-0x77)...
  found device at 0x3f
Using LCD backpack address 0x3f
LCD initialized and "Hello World!" / "SR110 Zephyr" written
```

LCD 화면에는 1번째 줄에 `Hello World!`, 2번째 줄에 `SR110 Zephyr`가 표시되어야 합니다.

## 8. 트러블슈팅

| 증상 | 가능한 원인 | 확인/조치 |
| --- | --- | --- |
| 스캔에서 0x27/0x3F 둘 다 안 잡힘 | 배선(SDA/SCL 반대 연결 포함), 전원 미공급 | 배선 재확인, 콘솔(외부 USB-TTL, 230400bps 8N1) 로그의 스캔 결과 전체 확인 |
| 스캔은 되는데 화면에 아무것도 안 보임 | 백라이트는 켜지는데 글자만 안 보이면 대개 명암(contrast) 문제 | 백팩의 트리머 포텐셔미터를 돌려보기 (드라이버 문제 아님) |
| 백라이트도 안 켜짐 | 전원(VCC/GND) 문제, 5V 필요 모듈을 3.3V로 구동 | 5절의 전원 가이드 참고 |
| 트리머를 끝까지 돌려도 글자가 흐리고 정면에서는 잘 안 보임(옆에서 봐야 겨우 읽힘) | VDD를 3.3V로 구동 중 - HD44780은 보통 V0가 VDD보다 4~5V 낮아야 진한 명암이 나오는데, VDD=3.3V면 V0가 내려갈 수 있는 폭 자체가 부족해서 트리머로도 한계가 있음 | 5절 가이드대로 VDD를 5V로 올리고 SDA/SCL은 레벨 시프터를 거쳐 연결 |
| 글자가 깨지거나 이상한 위치에 표시됨 | 3절 비트 맵과 실제 모듈 배선이 다름, 또는 EN 펄스 타이밍 문제 | 다른 제조사 모듈이면 비트 맵 재확인 필요 |
| 첫 글자만 깨지고 나머지는 정상 | 초기화 시퀀스 타이밍 부족 | `lcd_init()`의 대기시간을 늘려서 재시도 |
| I2C 쓰기 자체가 실패(ret != 0) | 배선/전원, 또는 두 개 이상 I2C 슬레이브가 같은 주소를 공유 | `i2c_write` 리턴값과 스캔 결과 로그 확인 |
| 스캔에서 실제로 연결한 디바이스가 하나도 안 잡힘 (write는 성공했었는데) | 프로빙 방식이 `i2c_write`로 되돌아가 있는지 확인 | SR110에서는 **1바이트 `i2c_read()`** 프로빙이어야 정상 동작합니다 (01번 랩 문서 5절 참고) |

## 9. 파일 구성

```
02_I2C_LCD_LAB/
├── 02_I2C_LCD_LAB_KR.md
├── 02_I2C_LCD_LAB_EN.md
└── lab/
    ├── CMakeLists.txt
    ├── README.rst
    ├── sample.yaml
    ├── prj.conf
    ├── boards/
    │   └── sr100_rdk_sr100_m55.overlay
    └── src/
        └── main.c
```
