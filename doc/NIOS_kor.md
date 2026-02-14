# Nios II 대화형 메뉴 시스템
[**English**](./NIOS.md) | [**한국어**]
[⬅️ README로 돌아가기](../README_kor.md)

이 문서는 HDMI 비디오 파이프라인을 제어하는 데 사용되는 대화형 콘솔 메뉴의 구조와 기능을 설명합니다.

## 📌 개요
이 애플리케이션은 JTAG UART 기반의 대화형 메뉴를 제공하여 사용자가 실시간으로 DMA 성능 테스트를 수행하고, 하드웨어를 초기화하며, RTL 패턴 제네레이터를 제어할 수 있도록 합니다.

## 🌳 메뉴 트리 구조
시스템의 복잡성 증가에 따라 메뉴는 계층 구조로 구성되어 있습니다.

### 1. 메인 메뉴
시스템 전반의 테스트와 하드웨어 초기화를 담당하는 최상위 메뉴입니다.

- **[1] DMA 테스트 (OCM to DDR3)**: 4KB 데이터 이동 성능을 측정합니다.
- **[2] 버스트 테스트 (DDR3 to DDR3)**: 파이프라인 처리를 포함한 1MB 데이터 이동 성능을 측정합니다.
- **[3] HDMI 초기화**: I2C를 통해 ADV7513을 720p 모드로 설정합니다.
- **[4] 컬러 바 생성**: DDR3 프레임 버퍼에 테스트 패턴을 작성합니다.
- **[5] RTL 패턴 변경**: 내부 RTL 패턴 생성(Red, Green, Blue 등)을 위한 하위 메뉴입니다.
- **[6] 감마 보정 설정**: **[신규]** LUT 및 토글 제어를 위한 중첩 하위 메뉴입니다.
- **[C] 커스텀 캐릭터 로드**: 타일 렌더링을 위한 16x16 비트맵을 업로드합니다.
- **[r] RTL 리셋**: 패턴 제네레이터를 기본 상태로 되돌립니다.
- **[q] 종료**: 애플리케이션을 종료합니다.

---

### 2. 감마 보정 하위 메뉴 (중첩)
메인 메뉴의 `[6]`번 옵션을 통해 진입하며, 하드웨어 룩업 테이블(LUT) 설정을 관리합니다.

- **[1] 활성화 토글**: 감마 하드웨어 블록의 ON/OFF 상태를 실시간으로 전환합니다.
- **[2] Gamma 2.2 로드**: 일반적인 디스플레이를 위한 표준 전력 법칙(Power-law) LUT입니다.
- **[3] sRGB Gamma 로드**: 암부 표현력을 개선하기 위한 조각별 선형/전력 함수 LUT입니다.
- **[4] Inverse Gamma 2.2 로드**: 선형 패널에서 검은색이 "들뜨는" 현상을 방지하기 위한 특수 LUT입니다.
- **[b] 뒤로 가기**: 메인 메뉴로 돌아갑니다.

## 📝 메뉴 샘플 (실제 실행 로그)

```text
DE10-Nano Video/DMA Test Environment Initialized
Checking Timer... Timer OK! (Delta=161197)
Initializing Span Extender to 0x20000000... Done.

========== DE10-Nano HDMI Pipeline Menu ==========
 [1] Perform OCM-to-DDR DMA Test (4KB)
 [2] Perform DDR-to-DDR Burst Master Test (1MB)
 [3] Initialize HDMI (ADV7513 via I2C)
 [4] Generate 720p Color Bar Pattern in DDR3
 [5] Change RTL Test Pattern (Red, Green, Blue, etc.)
 [6] Gamma Correction Settings (Table, Toggle, Standard)
 [C] Load Custom Character Bitmap
 [r] Reset RTL Pattern Generator
 [q] Quit
--------------------------------------------------
Select an option: 1

--- [TEST 1] OCM to DDR DMA (burst_master_0) ---
Starting SW Copy (4KB x 100)... Done (4179649 cycles, ~4.6 MB/s)
Starting HW DMA (4KB x 100)... Done (167027 cycles, ~116.9 MB/s)
Speedup: 25 x
SUCCESS: OCM to DDR Verified!

Select an option: 6

--- Gamma Correction Settings ---
 [1] Toggle Enable (Current: OFF)
 [2] Load Gamma 2.2 (Standard)
 [3] Load sRGB Gamma (Standard)
 [4] Load Inverse Gamma 2.2 (for Linear Panel)
 [b] Back to Main Menu
Enter choice: 1
Gamma Correction Enabled
```

---
> [!TIP]
> 시스템과 상호작용하려면 JTAG UART 터미널(`nios2-terminal`)을 사용하세요. 모든 입력은 대소문자를 구분하지 않으며 즉시 처리됩니다.
