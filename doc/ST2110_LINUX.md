# ST2110 Video Pipeline Optimization Summary

## 📌 개요 (Overview)
과거 수일간 Altera SoC(ARM) 리눅스 환경에서 발생하는 1080p 해상도 SMPTE ST 2110 UDP 비디오 수신 파이프라인의 영상 깨짐, 패킷 유실, 끊김 현상을 해결하기 위한 디버깅 및 최적화 작업 요약입니다. 기존에는 Nios II 환경에서는 정상 동작했으나, Linux 환경으로 이식하는 과정에서 다양한 드라이버 매핑 및 구조적 한계점이 발견되었습니다.

## 🐞 주요 문제점 및 해결 방안 (Issues & Solutions)

### 1. 픽셀 시프트 현상 (RTL Alignment Wrapper 오작동)
* **증상:** 수신되는 픽셀 데이터가 2~3 바이트씩 뒤로 밀리거나 화면이 초록색/보라색으로 오염되는 현상 (화면 노이즈).
* **원인 추적:** Linux에서 FPGA 레지스터(CSR)로 하드웨어 제어 명령(Control Word)을 보낼 때, SOP(Start of Packet)와 EOP(End of Packet) 플래그를 나타내는 비트맥 (Bitmask)이 `msgdma_transmit_burst()` 함수 내에서 잘못 매핑되어 있었습니다.
    * Nios: `(1 << 8)`, `(1 << 9)` 사용
    * Linux C: `(1 << 14)`, `(1 << 15)` 등 잘못된 상수를 사용 중이었음.
* **해결 방안:** `st2110_common.h` 및 `st2110_step2_dma.c`의 MSGDMA 제어 비트 플래그를 Nios의 헤더 파일과 동일하게 `MSGDMA_CSR_GENERATE_SOP` / `EOP` 등 정확한 규격으로 수정했습니다. 이후 완벽한 색상으로 1440 Byte -> 480 픽셀 워드 변환이 정렬되었습니다.

### 2. 패킷 유실 및 끊김 (UDP Socket Buffer Overflow)
* **증상:** 초당 수백/수천 개의 UDP Video 패킷이 쏟아질 때 중간중간 패킷 뭉치가 증발하여 화면 윗단이나 아랫단이 통째로 잘려나감. (Tearing)
* **원인 추적:** 기존 C 코드는 '수신(recvfrom) -> 정렬 매핑 -> DMA 전송 커맨드 -> 32ms 대기' 형태의 **단일 루프(Single Thread)** 로 동작했습니다. DMA가 복사를 수행하며 메인 CPU가 블로킹되는 32ms 동안 수신 대기를 하지 않으니 리눅스 커널의 네트워크 큐가 터져버린 것입니다.
* **해결 방안 (Thread Separation & Buffer Tuning):** 
    1. **POSIX Multi-Threading 적용:** 네트워크 수신 전용 백그라운드 스레드 (`network_rx_thread_func`)를 분리하여 단 1ms도 쉬지 않고 DDR3로 패킷을 직접 받아놓도록 개편.
    2. **Massive Socket Buffer:** 32ms 지연을 버틸 수 있도록 OS 수준(`sysctl net.core.rmem_max`)에서 UDP `SO_RCVBUF` 제한을 무려 **256MB** 로 확장하여 DMA가 바쁠 때도 패킷 보관 공간을 안전하게 확보했습니다.

### 3. V-Sync 타이밍 프레임 밀림 현상 (Jitter & Tearing)
* **증상:** 프레임 포인터를 바꾸는 시점이 모니터 주사율 타이밍과 어긋나 동영상의 대각선 이동 시 화면이 찢어지고 끊김. `gettimeofday` 15 FPS 소프트웨어 타이머가 V-Sync 주기와 동기화되지 않기 때문.
* **해결 방안:** 하드웨어 DMA 복사가 완료된 후, 즉시 프레임 포인터를 바꾸기보다 **다음번 V-Sync(수직 동기화) Rising Edge(Status 비트 29번)** 가 감지될 때까지 폴링 대기한 다음 포인터를 안전하게 교체(Double Buffering) 하도록 안전망을 구축했습니다. 눈에 띄게 프레임이 부드러워졌습니다.

## 🚀 최종 결과 (Result)
* 화면 깨짐 및 밀림 완벽 해소.
* `python st2110_tx.py --fps 24` 송출 시, 32ms 하드웨어 처리 지연 속에서도 스레딩 분리와 128MB 커널 큐의 조합으로 80%~95% 이상의 프레임 방어 성공. (보드 메모리 한계로 256MB 증설)
* 향후 CPU/메모리 처리 오버헤드를 줄이면 100% 방어(Wire-speed)도 가능할 것으로 보입니다.

---

## 🧠 하드웨어 아키텍처 및 설정 가이드 (Hardware & Configuration Guide)

수일간의 디버깅을 통해 얻은 Altera SoC 하드웨어-소프트웨어 통합 설정 노하우입니다.

### 1. Dual SDRAM Bus (FPGA to HPS Multi-port 접근)
DDR3 메모리 대역폭 한계로 인한 프레임 드랍을 막기 위해 HPS(Hard Processor System)의 SDRAM 컨트롤러 멀티포트를 활용했습니다.
* **설계 구조:** `f2h_sdram0` 와 `f2h_sdram1` 2개의 브리지(Bus)를 동시에 뚫어 사용했습니다.
* **사용 목적:** 
  * 한쪽 버스는 **네트워크 스레드**가 수신한 UDP 패킷 픽셀을 DDR3에 쓸 때(Write) 사용.
  * 다른 한쪽 버스는 **MSGDMA (초당 83MB)**가 DDR3에서 화면 데이터를 읽어 HDMI로 쏠 때(Read) 사용.
* **효과:** 버스를 2개로 분리함으로써 병목 없이 Read/Write가 병렬로 처리되어 프레임 렌더링 성능이 기하급수적으로 높아졌습니다.

### 2. Linux에서의 MSGDMA CSR & Descriptor 레지스터 매핑 방법
Nios II 기반에서는 BSP(Board Support Package)가 자동으로 물리 주소를 매핑해주지만, 아키텍처가 다른 Linux OS 환경에서는 C 코드로 하드웨어 레지스터 주소 공간을 가상 메모리(Virtual Memory)로 직접 뚫어줘야 합니다.

* **CSR (Control & Status Register) 매핑:**
  * LWH2F (Lightweight HPS-to-FPGA Bridge) 영역(물리 주소 `0xFF200000`)을 `/dev/mem`을 열어 `mmap()`으로 바인딩합니다.
  * Qsys(Platform Designer)에 설정된 오프셋을 더하여 포인터 연산을 합니다.
  * `virtual_lwh2f_base + (READ_DMA_CSR_BASE & 0x1FFFFF)`
* **Descriptor 매핑:**
  * **주위깊게 봐야할 점:** MSGDMA 디스크립터 영역이 `0xFF200000` LWH2F 안에 묶여있는지, 아니면 H2F Bridge(`0xC0000000` 영역)에 묶여있는지 반드시 Platform Designer를 켜서 확인해야 합니다! 영역이 다르면 다른 브리지를 `mmap` 해야 합니다.
  * 리눅스에서 `IOWR_32DIRECT` 등의 함수를 매크로(Macro)로 직접 정의해야 했습니다 (`#define IOWR_32DIRECT(BASE, OFFSET, DATA)`).
  * **Timing Parameter:** 제어 비트 설정 시 리눅스는 BSP 매크로가 없으므로 `st2110_common.h`의 `MSGDMA_CSR_GENERATE_SOP` / `EOP` 등의 플래그를 Nios의 `/inc/altera_msgdma_descriptor_regs.h` 드라이버를 열어보고 **정확한 비트 번호 (Bit 8, Bit 9 등)** 를 하드코딩 해주어야 Alignment Wrapper가 깨지지 않습니다.

### 3. MSGDMA Register Map (CSR & Descriptor)
Linux 환경에서 C 코드로 하드웨어를 직접 제어할 때 실수를 방지하기 위한 핵심 레지스터 명세입니다. (`altera_msgdma_csr_regs.h` 및 `altera_msgdma_descriptor_regs.h` 기준)

#### 📝 MSGDMA CSR (Control & Status Register) 가이드
CSR은 DMA의 현재 상태를 읽거나, 전체 리셋, 인터럽트 활성화 등을 수행하는 제어부입니다. 메모리 맵(mmap) 기준 `CSR_BASE + Offset` 에 위치합니다.
| Offset | 이름 | 접근 | 설명 |
| :--- | :--- | :--- | :--- |
| `0x0`  | **Status** | R/Clr | DMA의 현재 상태. `Busy (Bit 0)`, `Desc Buffer Empty (Bit 1)`, `Full (Bit 2)` 확인 등. `1`을 쓰면 (W1C) 인터럽트가 초기화됨. |
| `0x4`  | **Control** | R/W | `Stop (Bit 0)`, `Software Reset (Bit 1)`, `Global Interrupt Mask (Bit 4)` 제어. |
| `0x8`  | **Descriptor Fill Level** | R | 현재 버퍼에 쌓여있는 Descriptor 개수 (Write `[31:16]`, Read `[15:0]`). |
| `0xC`  | **Response Fill Level** | R | Response FIFO에 쌓인 응답 개수 `[15:0]`. |

**[주의사항]** 작동 중인 DMA를 강제로 초기화하려면 Control 레지스터(`0x4`)의 비트 1 (`Software Reset`)에 `1`을 씁니다. 상태 레지스터(`0x0`)의 비트 0 (`Busy`)이 `0`인지 확인 한 뒤 새 Descriptor를 넣어야 안전합니다.

#### 📝 MSGDMA Descriptor (Extended Format) 가이드
디스크립터는 DMA가 "어디서(Read) 어디로(Write) 얼만큼(Length)" 복사할지 지시하는 32바이트(Extended 기준) 크기의 작업 지시서입니다. 
ST2110 비디오 파이프라인처럼 Avalon-ST Video 패킷을 다룰 때는 **Extended Format**을 사용하여 SOP/EOP 플래그를 정확히 제어해야 합니다.
| Offset | 이름 | 설명 |
| :--- | :--- | :--- |
| `0x00` | **Read Address** `[31:0]` | 데이터 원본 물리 주소 (예: DDR3 네트워크 링버퍼 주소). |
| `0x04` | **Write Address** `[31:0]` | 데이터 목적지 물리 주소 (예: 화면 출력 FIFO). |
| `0x08` | **Length** `[31:0]` | 전송할 바이트 단위 길이. |
| `0x1C` | **Control (Enhanced)** `[31:0]` | 패킷 제어 및 전송 커맨드. (가장 중요) |

**[Control (0x1C) 상세 비트 마스크]**
* **`GO` (Bit 31, `1 << 31`):** 이 비트를 `1`로 셋팅하여 디스크립터를 작성하면 DMA가 해당 작업을 Dispatcher에 큐잉하고 전송을 시작합니다.
* **`GENERATE_SOP` (Bit 8, `1 << 8`):** 전송의 시작이 영상 프레임 패킷의 시작점(Start of Packet)임을 나타냅니다. 영상의 **첫 픽셀 묶음** 전송 시 반드시 비트를 켜야 합니다.
* **`GENERATE_EOP` (Bit 9, `1 << 9`):** 패킷의 끝점(End of Packet)임을 나타냅니다. 한 영상 프레임/라인의 **마지막 픽셀 묶음** 전송 시 넣어주어야 다음 프레임과 화면이 꼬이지 않습니다.
* **`TRANSFER_COMPLETE_IRQ` (Bit 14, `1 << 14`):** 해당 디스크립터 전송 완료 시 인터럽트를 발생시킵니다.
* **`EARLY_DONE_ENABLE` (Bit 24, `1 << 24`):** Avalon-ST 수신 측에서 EOP를 조기 감지하여 DMA를 멈추게 할 때 사용합니다.

리눅스 C 프로그래밍 시, 위 매크로 상수들(특히 `Bit 8, 9`의 SOP/EOP)을 우연히 다른 엉뚱한 비트(예: 14, 15번)로 설정해버리면 MSGDMA Alignment Wrapper가 패킷의 시작과 끝을 인지하지 못해 화면 픽셀이 통째로 엇갈리는 **픽셀 시프트 (Pixel Shift)** 증상이 나타납니다.

### 4. FPS를 60까지 끌어올리기 위한 최적화 (향후 과제)
현재 24 FPS 송출 시 80~90% 수준의 방어율을 보입니다. 대역폭 지연을 줄이고 60 FPS를 안정적으로 달성하기 위한 방법입니다.

* **1. Zero-Copy 네트워킹 도입 (`AF_XDP` 또는 `DPDK`)**
  * 현재 리눅스 소켓 `recvfrom`은 커널 공간에서 유저 공간(buffer array)으로 메모리를 한 번 복사(Copy)한 뒤, 그것을 다시 FPGA `mmap` 영역에 C 코드로 `memcpy()` 하는 방식입니다.
  * 고속 패킷 캡처 기술(XDP)을 사용하거나 커널을 우회하여 FPGA의 이더넷 MAC에서 들어오는 DMA를 DDR3로 직접 다이렉트로 꽃아버려야 CPU 부하율을 0%에 가깝게 떨어뜨릴 수 있습니다.
* **2. DMA 전송량 단축 및 Descriptor Chain 최적화**
  * 현재 60번의 `msgdma_transmit_burst_pipeline()` 호출을 통해 1프레임을 그립니다. 이를 MSGDMA의 **Descriptor Polling / Descriptor Chain 기능**을 켜서, 1440 Byte x 1080 Packet을 하나의 링크드 리스트로 던지고 CPU는 인터럽트(IRQ)만 받는 구조로 변경해야 32ms 대기열(Blocking time)이 사라집니다.
