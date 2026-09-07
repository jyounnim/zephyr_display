# Lab 03: OLED SSD1306 (I2C 모드, 주소 자동 감지)

## 1. 개요

보드는 **Synaptics SR110** (`sr100_rdk/sr100/m55`), 프레임워크는 **Zephyr RTOS**를 사용합니다.

0.96인치 SSD1306 128x64 모노크롬 OLED를 **I2C 모드**로 구동합니다. SSD1306 I2C 모듈은 실장된 저항(SA0 스트랩)에 따라 **0x3C 또는 0x3D** 두 주소 중 하나로 동작하는데, 이 랩은 01번 랩(I2C bus scanner)과 같은 방식으로 부팅 시 두 주소를 직접 스캔해서 실제로 응답하는 쪽을 자동으로 골라 씁니다.

Zephyr의 Display/CFB 서브시스템이나 in-tree `solomon,ssd1306` 드라이버를 쓰지 않고, **02번 랩(PCF8574 LCD)과 동일하게 raw I2C**로 SSD1306을 직접 제어합니다 (이유는 2절 참고).

이 랩은 **실기로 완전히 검증 완료**된 상태입니다.

> **디스플레이 스펙 요약**
> | 항목 | 내용 |
> |---|---|
> | 컨트롤러 | Solomon Systech **SSD1306** |
> | 해상도 | 128 x 64 픽셀, 1비트(모노크롬) |
> | 화면 크기 | 대개 0.96인치 |
> | 인터페이스 | I2C (이 랩) 또는 SPI (05번 랩) — 같은 칩이 두 모드를 다 지원 |
> | I2C 주소 | `0x3C` 또는 `0x3D` (SA0 스트랩에 따라 결정) |
> | 전원 방식 | 대부분 내장 charge pump로 OLED 구동 전압을 자체 생성 (외부 승압 불필요) |

## 2. 왜 raw I2C인가 (Zephyr 표준 드라이버를 안 쓰는 이유)

Zephyr의 `solomon,ssd1306` 드라이버는 devicetree 노드에 `reg = <0x3C>;`처럼 **주소를 빌드 타임에 고정**해서 씁니다. 그런데 이 랩의 목적 자체가 "부팅 시 실제 연결된 주소를 스캔해서 자동으로 고르는 것"이라, 애초에 정적 devicetree 바인딩과는 맞지 않습니다. 그래서 05번 랩(SPI 모드 SSD1306, 고정 주소 불필요)과 달리, 이 랩은 devicetree에 SSD1306 노드를 아예 선언하지 않고 `main.c`에서 직접 I2C 명령/데이터를 씁니다.

## 3. SSD1306 I2C 프로토콜 요약

SSD1306과의 모든 I2C 트랜잭션은 **컨트롤 바이트**로 시작합니다:

| 컨트롤 바이트 | 의미 |
|---|---|
| `0x00` | 뒤따르는 바이트(들)는 **명령(command)** |
| `0x40` | 뒤따르는 바이트(들)는 **픽셀 데이터(GDDRAM)** |

명령은 한 번에 하나씩(컨트롤 바이트+명령 바이트 2바이트) 보내고, 프레임버퍼(1024바이트) 전체는 컨트롤 바이트(`0x40`)를 맨 앞에 붙인 **1025바이트를 하나의 연속된 버퍼로 만들어 단일 `i2c_write()`** 트랜잭션으로 보냅니다. 컨트롤 바이트와 프레임버퍼를 별도 메시지로 나눠 보내는(`i2c_transfer()`) 방식은 이 플랫폼의 I2C 드라이버에서 메시지 사이에 원치 않는 STOP이 끼어들어 화면이 노이즈로 깨지는 문제가 있어, 이 방식은 쓰지 않습니다.

## 4. 배선

| 신호 | 역할 | SR110 연결 |
|---|---|---|
| VCC | 전원 | 3.3V (아래 5절 전원 주의 참고) |
| GND | 그라운드 | GND |
| SDA | 데이터 | I2C0 SDA (핀 그룹 `i2c0_ms_sda`) |
| SCL | 클럭 | I2C0 SCL (핀 그룹 `i2c0_ms_scl`) |
| RST (모듈에 이 핀이 **있는 경우에만**) | 하드웨어 리셋 | gpioa 4 (SoC GPIO4, **J25 5번 핀**) |

알리익스프레스 등에서 흔히 파는 저가 SSD1306 모듈은 대부분 **VCC/GND/SDA/SCL 4핀뿐**이고 RST 핀 자체가 없습니다 — 이 경우 위 RST 행은 무시하면 됩니다. 01/02번 랩과 동일한 I2C0 버스를 그대로 재사용합니다.

> ✅ **RST 핀이 필요한 경우가 있습니다**: 소프트웨어 초기화 시퀀스(8절)만으로는 SSD1306 컨트롤러의 내부 상태가 완전히 리셋되지 않아, 부팅 직후 화면이 점 노이즈 상태에서 벗어나지 않는 모듈이 있습니다. 이 경우 RST 핀을 Low→High로 한 번 펄스해야 정상화됩니다 — `main.c`의 `oled_hw_reset()`이 이 역할을 합니다.
>
> **GPIO4를 고른 이유**: SR110은 회로도상 "안 쓰는 핀처럼 보인다"는 이유만으로 GPIO를 재사용하면 위험할 수 있습니다 (JTAG나 디버그 모듈과 물리적으로 같은 핀을 공유하는 경우가 있음). GPIO4는 `sr100_pinctrl.dtsi`를 직접 확인해서 JTAG/디버그모듈/SD 계열과 전혀 안 겹치는 것으로 골랐고(같은 그룹의 다른 기능은 미사용 카메라 VSYNC, 미사용 UART 흐름제어뿐), 실기로 정상 부팅과 리셋 동작까지 확인했습니다.
>
> 모듈에 RST 핀이 없는 4핀 구성이라면 `main.c`의 `OLED_USE_HW_RESET`을 `0`으로 바꾸면 됩니다 (기본값은 `1`).

## 5. 전원 주의 (01번 랩 문서 참고)

> ✅ **실기로 확인됨**: 보드 헤더의 3.3V 레일로 전원을 공급하면 I2C write 자체가 에러로 실패하는 경우가 있습니다 — 스캔은 성공해도 그 이후 초기화 명령 전송이 실패할 수 있습니다. 외부 3.3V 전원으로 바꾸면 해결됩니다. 이 시리즈에서 I2C/SPI 외부 디바이스를 붙일 때는 **처음부터 외부 전원을 쓰는 것**을 원칙으로 하십시오. 자세한 배경은 01번 랩 문서의 "전원 공급 관련 참고" 절을 참고하십시오.

## 6. Devicetree 오버레이

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

02번 랩과 완전히 동일한 오버레이입니다 — SSD1306용 자식 노드가 없다는 점(2절 참고)만 다릅니다.

## 7. 주소 자동 감지 동작 방식

```c
static const uint8_t oled_addr_candidates[] = { 0x3C, 0x3D };

static bool ssd1306_find_address(const struct device *bus, uint8_t *addr_out)
{
    for (size_t i = 0; i < ARRAY_SIZE(oled_addr_candidates); i++) {
        uint8_t addr = oled_addr_candidates[i];
        if (i2c_probe_addr(bus, addr)) {   /* 1바이트 read 프로빙, 01번 랩과 동일 */
            *addr_out = addr;
            return true;
        }
    }
    return false;
}
```

- 0x3C를 먼저 시도하고, 실패하면 0x3D를 시도합니다 (SSD1306이 쓸 수 있는 주소는 이 둘뿐이므로 01번 랩처럼 0x08~0x77 전체를 훑을 필요는 없습니다).
- 프로빙 방식은 01번 랩에서 확정된 대로 **1바이트 `i2c_read()`**를 씁니다.
- 찾은 주소를 이후 모든 명령/데이터 전송에 그대로 사용합니다 — devicetree의 고정 `reg` 대신 런타임 변수로 주소를 다루는 구조입니다.

> ✅ **부팅 직후 안정화 시간**: 전원이 들어온 직후 SSD1306의 POR(Power-On Reset)이 끝나기 전에 프로빙하면 실제로 연결된 모듈도 못 찾을 수 있습니다. `main()` 시작 시 `k_sleep(K_MSEC(100))`으로 안정화 시간을 확보하고, 각 주소마다 짧은 간격을 두고 최대 3회 재시도하도록 되어 있습니다.

## 8. 초기화 시퀀스

128x64 SSD1306의 표준 초기화 시퀀스입니다 (내부 charge pump 사용 — 대부분의 브레이크아웃 모듈은 패널 구동용 외부 Vcc가 없으므로 `0x8D, 0x14`로 내부 승압을 켭니다):

| 명령 | 값 | 의미 |
|---|---|---|
| `0xAE` | - | 디스플레이 끄기 |
| `0xD5` | `0x80` | 디스플레이 클럭 분주비 |
| `0xA8` | `0x3F` (63) | Multiplex ratio = 높이-1 |
| `0xD3` | `0x00` | 디스플레이 오프셋 |
| `0x40` | - | 시작 라인 = 0 |
| `0x8D` | `0x14` | Charge pump 활성화 |
| `0x20` | `0x00` | 메모리 어드레싱 모드 = horizontal |
| `0xA1` | - | Segment remap (컬럼 127 = SEG0) |
| `0xC8` | - | COM 출력 스캔 방향 리매핑 |
| `0xDA` | `0x12` | COM 핀 하드웨어 설정 (128x64용) |
| `0x81` | `0xCF` | 명암(contrast) |
| `0xD9` | `0xF1` | Pre-charge period |
| `0xDB` | `0x40` | VCOMH deselect level |
| `0xA4` | - | RAM 내용 그대로 표시 재개 |
| `0xA6` | - | 정상(반전 아님) 표시 |
| `0xAF` | - | 디스플레이 켜기 |

화면이 상하좌우 반전되어 보이면(모듈마다 유리 패널이 뒤집혀 실장된 경우가 있음) `0xA1`↔`0xA0`(segment remap), `0xC8`↔`0xC0`(COM scan direction)을 서로 바꿔서 재시도하십시오.

## 9. 빌드 & 실행

```powershell
west build -p always -b sr100_rdk/sr100/m55 .\03_OLED_SSD1306_I2C\lab\
```

```bash
python srsdk_tools/openocd_flash.py --openocd <openocd 경로> --flash-offset 0x0 \
    --file-offset 0x0 --cfg_path srsdk_tools/Input_Config/sr100_m55.cfg \
    --image build/zephyr/zephyr_flash.bin
```

I2C0만 쓰고 UART 핀은 건드리지 않으므로, 콘솔은 보드 기본값(UART1, GPIO23=TX/GPIO24=RX, J25 헤더 + 외부 USB-TTL 어댑터, **230400bps 8N1**) 그대로 사용하면 됩니다.

### 예상 시리얼 출력

```
=== OLED SSD1306 (I2C, SR110) ===
Scanning for SSD1306 at 0x3C / 0x3D...
  found device at 0x3D
SSD1306 initialized at 0x3D, "Hello World!" written
```

OLED 화면에는 1번째 줄에 `Hello World!`, 3번째 줄(page 2)에 `Addr 0x3D`가 표시되어야 합니다.

## 10. 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| `No SSD1306 found at 0x3C or 0x3D` | 배선(SDA/SCL) 확인, 풀업 확인. **보드 레일 전원이면 외부 3.3V로 바꿔서 재시도** (5절 참고) |
| `I2C0 device not ready` | 오버레이가 실제로 적용 안 됨 — 오버레이 파일명이 west board target과 일치하는지 확인 |
| 화면이 켜지긴 하는데 아무것도 안 보임 | `ssd1306_update()`가 실패 없이 리턴했는지 로그 확인. 컬럼/페이지 주소 범위(`0x21`/`0x22`)가 실제 패널 해상도(128x64)와 일치하는지 확인 |
| 초기화는 에러 없이 끝났는데 화면이 계속 점 노이즈 상태 | 모듈에 RST 핀이 있다면 배선 확인 (4절 참고). `OLED_USE_HW_RESET=1`, `OLED_RST_PIN=4`가 기본값입니다 |
| 화면이 상하 또는 좌우 반전 | 8절 마지막 문단 참고 — `0xA1`/`0xC8` 극성을 반대로 시도 |
| 글자가 깨지거나 이상한 위치 | `fb_draw_char`의 페이지/컬럼 계산, 또는 폰트에 없는 문자를 쓰고 있는지 확인 |

**Lab 03 완전히 검증 완료** — 주소 자동 감지, 전원, 프레임버퍼 전송 방식, RST 핀 처리까지 전부 실기로 확인 완료.

## 11. 파일 구성

```
03_OLED_SSD1306_I2C/
├── 03_OLED_SSD1306_I2C_KR.md
├── 03_OLED_SSD1306_I2C_EN.md
└── lab/
    ├── CMakeLists.txt
    ├── prj.conf
    ├── sample.yaml
    ├── boards/
    │   └── sr100_rdk_sr100_m55.overlay
    └── src/
        └── main.c
```

## 12. 다음

04번 랩(`04_SPI_basics`)부터는 SPI0 버스로 넘어갑니다 — SPI0가 보드 기본 콘솔 UART와 핀을 공유하는 문제가 있어 접근 방식이 꽤 달라집니다. 자세한 내용은 해당 랩 문서 참고.
