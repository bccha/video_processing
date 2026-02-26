# U-Boot: FPGA-to-SDRAM 브릿지 초기화 스크립트 설정

이 문서는 FPGA 패브릭에서 HPS (Hard Processor System) 측의 SDRAM에 올바르게 접근하기 위해 필요한 부트로더(U-Boot) 단계의 초기화 절차와 부트 스크립트 작성 방법을 설명합니다.

## 1. 개요 및 배경 (Altera 기술 문서 분석)

FPGA와 HPS SDRAM을 연결하는 `fpga2sdram` AXI 브릿지는 특정 순서에 따라 신중하게 초기화되어야 합니다. 그렇지 않으면 OS 부팅 과정 또는 런타임 중에 HPS 서브시스템이 정지(Hang)되는 심각한 오류가 발생할 수 있습니다. 

### 왜 U-Boot 단계에서 초기화해야 하는가?

브릿지 설정 과정 중 가장 중요한 제약 조건은 **SDRAM 컨트롤러의 `STATICCFG` 레지스터 내 `APPLYCFG` 비트를 설정하는 시점**에 있습니다.

*   이 비트는 **SDRAM DDR 인터페이스가 완전히 유휴(Idle) 상태일 때만** 기록할 수 있습니다. (ARM 코어, DMA 등의 전송이 전혀 없는 상태)
*   Linux 등의 상위 운영체제가 실행 중인 상태에서는 백그라운드 프로세스와 지속적인 메모리 접근으로 인해 이 조건(완전한 유휴 상태)을 충족하기가 실질적으로 불가능합니다.
*   따라서, 이 초기화 시퀀스는 운영체제가 메모리를 본격적으로 제어하기 전 단계인 **U-Boot 부트로더에서 수행**되어야 합니다.

**재구성(Reconfiguration) 시 주의점:**
전원 켜짐(Cold Boot) 이후 최초 1회 U-Boot에서 `fpga2sdram` 포트 конфигураци(데이터 폭, 방향 등)가 올바르게 적용되면, 이후 리눅스 런타임 중에도 FPGA를 재구성(Reconfigure)할 수 있습니다. 단, **새로운 디자인의 `fpga2sdram` 포트 설정이 최초 U-Boot에서 적용한 설정과 정확히 일치**해야 합니다. (이때는 포트 리셋 -> 프로그래밍 -> 포트 리셋 해제 과정만 거치며, `APPLYCFG` 비트 설정은 생략됩니다.)

---

## 2. 초기화 시퀀스 (Initialization Sequence)

안전한 브릿지 활성화를 위한 필수 단계는 다음과 같습니다.

1.  **FPGA 포트 리셋**: `fpga2sdram` 주변기기의 FPGA 포트를 리셋 상태로 전환합니다. (SDRAM 컨트롤러 제어 그룹의 `FPGAPORTRST` 레지스터 값을 `0`으로 설정)
2.  **FPGA 구성(Configuration)**: `fpga2sdram` 포트 구성 정보가 포함된 이미지(.rbf)로 FPGA 패브릭을 프로그래밍합니다. 이 과정을 통해 브릿지 입력단의 구성 포트(폭, 방향 등)가 활성화됩니다.
3.  **설정 적용(Apply Configuration)**: 구성된 입력 상태를 FPGA2SDRAM 브릿지 주변기기에 적용합니다. (SDRAM 컨트롤러 제어 그룹의 `STATICCFG` 레지스터의 `APPLYCFG` 비트에 `1`을 기록). **주의: 위에서 언급했듯, 이 과정은 SDRAM이 완전한 유휴 상태일 때만 가능합니다.**
4.  **FPGA 포트 리셋 해제**: 적용된 구성을 바탕으로 `fpga2sdram` 포트의 리셋 상태를 해제하여 통신을 시작합니다. (`FPGAPORTRST` 레지스터에서 해당 비트를 `1`로 설정)

---

## 3. 부트 스크립트 (`u-boot.scr`) 구현

위의 시퀀스를 매 부팅 시마다 수동으로 입력하는 것을 방지하기 위해 U-Boot 부트 스크립트 소스(예: `bootscript.txt`)를 작성하고 컴파일하여 사용합니다.

### 3.1. `bootscript.txt` 소스 코드 예시

```sh
# FAT 파티션(0:1)에 sdr.rbf 파일이 존재하는지 검사
if test -e mmc 0:1 sdr.rbf; then
  echo "Found sdr.rbf"
  
  # [단계 1] fpga2sdram 브릿지 리셋 (fpgaportrst 비활성화)
  mw 0xFFC25080 0x0
  
  # FPGA 비트스트림을 메모리로 로드 (메모리 주소 0x3000000는 예시)
  fatload mmc 0:1 0x3000000 sdr.rbf
  
  # [단계 2] 로드된 이미지로 FPGA 프로그래밍 (0x700000은 파일 크기 한계값 예시)
  fpga load 0 0x3000000 0x700000
  
  # [단계 3] 설정 적용 (staticcfg 레지스터의 applycfg 비트 활성화)
  mw 0xFFC2505C 0xA
  
  # [단계 4] fpga2sdram 브릿지 리셋 해제
  mw 0xFFC25080 0xFFFF
else
  echo "sdr.rbf not found, doing nothing"
fi;
```
*(참고: `sdr.rbf`, 메모리 로드 주소 및 크기 한계값은 실제 환경에 맞게 조정해야 합니다.)*

### 3.2. 스크립트 컴파일 및 배포

1.  **컴파일**: U-Boot에서 제공하는 `mkimage` 도구를 사용하여 텍스트 소스를 실행 가능한 형태인 `u-boot.scr` 파일로 컴파일합니다.
    ```bash
    mkimage -C none -A arm -T script -d bootscript.txt u-boot.scr
    ```
2.  **배포**: 컴파일된 `u-boot.scr` 파일과 대상 FPGA 비트스트림 파일(`sdr.rbf` 등)을 타겟 보드의 부팅 파티션(FAT 파티션, 일반적으로 `/dev/mmcblk0p1`) 최상위 디렉토리에 복사합니다.

보드를 재부팅하면 U-Boot가 이 스크립트를 감지하고 자동으로 안전한 브릿지 초기화 시퀀스를 수행하게 됩니다.

---

## 4. 추가 요구 사항: 커널 메모리 제한

FPGA 전용으로 사용할 SDRAM 영역은 운영체제(Linux 커널)나 다른 사용자 애플리케이션이 임의로 접근하여 데이터를 오염시키지 못하도록 보호되어야 합니다.

*   부팅 파티션(FAT 파티션) 내에 위치한 `extlinux/extlinux.conf` 파일의 `APPEND` 라인 끝에 `mem=512M` (또는 원하는 용량 제한) 파라미터를 추가하여 커널이 인식하는 전체 메모리 크기를 제한하십시오. 이를 통해 상위 주소 공간을 FPGA 전용으로 확보할 수 있습니다. 
