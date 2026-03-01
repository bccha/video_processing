# Quartus SDC 최적화 (빌드 시간 단축 및 라우팅 에러 방지)

## 개요
Quartus 컴파일 과정에서 Fitter(배치 및 배선) 단계가 수십 분 이상 멈춰있거나, 엄청난 양의 `Warning (16684)` (라우팅 혼잡 에러)가 발생한다면 가장 먼저 SDC (Synopsys Design Constraints) 파일의 **비동기 클럭 그룹(Asynchronous Clock Groups)** 설정을 점검해야 합니다.

## 1. 이종 클럭 간 삽질(False Path)의 문제점
50MHz 보드 클럭과 비디오 파이프라인에서 생성한 PLL 클럭(ex: 37MHz), 그리고 HPS 측 SDRAM 고속 버스 클럭 등은 주파수가 서로 다르고 엣지(Edge)가 무작위로 만나는 **비동기(Asynchronous)** 상태입니다. 
- 만약 이 클럭들이 만나는 지점(Cross Domain)을 SDC에 명시하지 않으면, Quartus Fitter는 이들 클럭 엣지가 우연히 스쳐 지나가는 그 짧은 나노초 단위의 순간까지도 모든 타이밍 요구사항을 완벽하게 맞추기 위해 **수백만 번의 재배치 연산(무한루프)** 을 수행합니다.
- 결과적으로 무의미한 타이밍 클로저(Timing Closure)를 달성하려다가 배선로(Routing resource)가 폭발하고 빌드 시간이 거하급수적으로 길어지게 됩니다. (최악의 경우 Timing 분석에서 새빨간 Slack 에러가 도배됩니다.)

## 2. 해결 방법 (set_clock_groups 적용)

이러한 문제를 방지하기 위해 `DE10_NANO_SOC_GHRD.sdc` 파일 내에 시스템의 이종 클럭 그룹들을 서로 완벽히 분리시켜 주는 코드를 추가합니다.

### SDC 코드 예시
```tcl
set_clock_groups -asynchronous \
    -group [get_clocks {FPGA_CLK1_50 FPGA_CLK2_50 FPGA_CLK3_50}] \
    -group [get_clocks {u0|pll_0|altera_pll_i|*|divclk}] \
    -group [get_clocks {*|hps_0|hps_io|border|h2f_user0_clock}]
```

*   `-group` 으로 묶인 각 줄은 서로 독립적인 하나의 클럭 도메인을 의미합니다.
*   `-asynchronous` 옵션은 Quartus에게 **"이 그룹들끼리 데이터를 주고받는 경로(Cross Domain Path)에 대해서는 절대로 타이밍 분석을 하거나 맞춰주려고 연산 낭비하지 마!"** 라고 단호하게 지시하는 역할을 합니다. (어차피 HPS-FPGA 버스나 Qsys 내부의 Dual-clock FIFO 등이 하드웨어적으로 알아서 동기화를 처리하기 때문입니다.)

## 3. 적용 효과
*   **컴파일 시간 대폭 단축:** 피터(Fitter)가 쓸데없는 경로를 포기하므로 컴파일 진행 바가 멈춰있는 현상이 사라집니다.
*   **라우팅 에러 억제:** 불가능한 타이밍을 우회하려고 배선을 사방으로 꼬아대던 `Router estimated peak interconnect usage` 폭주 현상이 줄어듭니다.
*   **TimeQuest 결과 클린:** 불필요한 빨간색 타이밍 에러(Timing Violation)가 리포트에서 사라지며, 실제 영상 출력 등 하드웨어 구동의 안정성이 확보됩니다.
