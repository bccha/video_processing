# 비디오 프로세싱 파이프라인 - 테스트 결과
[**English**](./RESULT.md) | [**한국어**]

[⬅️ README로 돌아가기](../README_kor.md)

이 문서는 DE10-Nano 비디오 프로세싱 파이프라인에 대한 성능 벤치마크 및 하드웨어 검증 결과를 기록합니다.

## 1. DMA 성능 벤치마크

### 버스트 마스터 성능 (2026-02-12)

| 테스트 케이스 | 크기 | 소프트웨어 (클록) | 하드웨어 (클록) | MB/s (하드웨어) | 속도 향상 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| OCM to DDR | 4KB x 100 | 4,185,427 | 166,211 | 117.5 | **25배** |
| DDR to DDR | 1MB | 207,071,817 | 393,942 | 126.9 | **525배** |

> [!NOTE]
> DMA (Burst Master 4)는 CPU 부하를 획기적으로 줄여주며, 1MB 전송 시 500배 이상의 성능 향상을 제공합니다.

## 2. 비디오 출력 검증

### 540p (qHD) 구현 (2026-02-14)

**해상도**: 960×540 @ 60Hz  
**픽셀 클록**: 37.8336 MHz  
**대역폭**: 124 MB/s (50MHz 버스 가동률 62%)

#### ✅ 검증된 기능

| 기능 | 상태 | 상세 내용 |
|---------|--------|---------|
| **정적 이미지 디스플레이** | ✅ 통과 | Nios II가 DDR3에서 이미지를 성공적으로 로드 및 표시함 |
| **비디오 재생 (리눅스)** | ✅ 통과 | HPS `/dev/mem`을 통한 더블 버퍼링 스트리밍 확인 |
| **V-Sync 동기화** | ✅ 통과 | 티어링 없는 프레임 포인터 래칭(Latching) 확인 |
| **감마 보정** | ✅ 통과 | sRGB 및 Inverse Gamma 2.2 LUT 정상 동작 |
| **패턴 생성** | ✅ 통과 | 컬러, 그리드, 캐릭터 타일 등 8가지 모드 모두 확인 |
| **듀얼 클록 CDC** | ✅ 통과 | CSR(50MHz) 및 Pixel(37.8MHz) 도메인 안정성 확인 |

#### 성능 참고 사항
- **초기 재생**: 리눅스 페이지 캐시 활성화 시 60fps 유지
- **지속 재생**: SD 카드 병목으로 인해 10-15fps로 저하 (필요 124 MB/s vs 가용 ~20 MB/s)
- **RAM 사전 로드 (신규)**: ✅ **60fps 안정적 재생** (약 4.1초 재생 시간 제한)
- **프레임 버퍼 크기**: 프레임당 2,073,600 바이트 (약 2MB)
- **메모리 배치**: 0x20000000 기준 512MB 예약 영역 사용

## 3. 하드웨어 초기화 상태

### 현재 구성
- **HDMI PLL**: 37.8336 MHz (540p60)에 고정(Locked)
- **ADV7513 IC**: I2C를 통한 구성 성공
- **메모리 맵**: 0x30000000 (512MB 예약 영역)에 프레임 버퍼 위치
- **HPS 브릿지**: LWHPS2FPGA를 통해 0xFF240000의 HDMI CSR에 연결됨

### Qsys 연결성
```
hps_0.h2f_lw_axi_master → mm_bridge_0.s0 → hdmi_sync_mm.s0 (Base: 0x40000)
```

## 4. 실행 로그

### DMA 벤치마크 로그
```text
--- [TEST 1] OCM to DDR DMA (burst_master_0) ---
Starting SW Copy (4KB x 100)... Done (4185427 cycles, ~4.6 MB/s)
Starting HW DMA (4KB x 100)... Done (166211 cycles, ~117.5 MB/s)
Speedup: 25 x
SUCCESS: OCM to DDR Verified!

--- [TEST 2] DDR to DDR DMA (Burst Master 4) ---
Transfer Size: 1 MB
Initializing DDR3 data... Done.
Starting SW Copy (1MB)... Done (207071817 cycles, ~0.2 MB/s)
Starting HW DMA (1MB)... Done (393942 cycles, ~126.9 MB/s)
Speedup: 525 x
Verifying HW Output...
SUCCESS: DDR to DDR Verified! (Coeff=800)
```

### HDMI 초기화 로그
```text
Waiting for PLL Lock (37.83 MHz)...
PLL Locked! Initializing ADV7513 HDMI Transmitter...
HDMI Controller Configured. Ready for Video!

Generating 540p Pattern in DDR3... Done! (Total 518400 pixels written)
```

### 비디오 재생 로그 (리눅스)
```text
DE10-Nano Linux Video Player (Double Buffered / RAM Preload)
Video: video_qhd.bin (960x540)
Mapped Frame Buffers:
  Buffer A (Virtual): 0xb6f00000 (Physical: 0x30000000)
  Buffer B (Virtual): 0xb7100000 (Physical: 0x30200000)
Mapped CSR Base: 0xb6e00000
Started Playback (Double Buffering)...
.........
```

## 5. 고급 기능 검증

### 감마 보정 ✅
- **Mode 7 (캐릭터 타일)**: 동적 무지개 색상 효과 확인
- **감마 LUT 로딩**: sRGB 및 Inverse Gamma 2.2 검증 완료
- **실시간 토글**: CSR을 통한 감마 활성화/비활성화 동작 확인

### 타이밍 분석 ✅
- **Setup Slack**: 양수 (위반 없음)
- **Hold Slack**: 양수 (위반 없음)
- **클록 도메인 교차 (CDC)**: SDC를 통해 적절히 제약됨
- **V-Sync 래칭**: 상승 엣지에서 쉐도우 포인터 업데이트 확인

## 6. 알려진 제한 사항
- **SD 카드 대역폭**: 지속 재생 시 약 10-15fps로 제한됨
- **메모리 제약**: 512MB DDR3 예약 영역 사용 (사전 로드 시 최대 약 250 프레임)
- **오디오 미지원**: 현재 비디오 전용으로 구현됨

## 7. 향후 계획

### 4단계: 대역폭 확장
**목표**: 720p@60Hz (필요 대역폭 222 MB/s) 지원

**접근 방식**:
- 버스 폭을 4바이트에서 8/16바이트로 확장
- 클록 주파수는 50MHz 유지
- 목표 대역폭: 400 MB/s (50MHz @ 8-byte 기준)

**기대 효과**:
- 720p@60Hz를 80%의 여유 대역폭으로 처리 가능
- 성능 마진 개선 및 향후 고해상도 대응 가능

---
*최종 업데이트: 2026-02-14*
