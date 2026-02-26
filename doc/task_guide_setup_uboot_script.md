# [U-Boot] FPGA-to-SDRAM 브릿지 초기화 가이드

## 개요
FPGA 패브릭에서 HPS (Hard Processor System) 측의 SDRAM에 접근하려면 `fpga2sdram` AXI 브릿지를 활성화해야 합니다. 이 브릿지의 초기화는 **반드시 U-Boot 부트로더 단계에서 수행**되어야 하며, 상위 운영체제(Linux 등)가 부팅된 이후에는 초기화가 불가능합니다. 

본 문서는 보드 부팅 시 자동으로 브릿지를 초기화하고 FPGA를 구성(Configuration)하기 위한 U-Boot 스크립트(`u-boot.scr`)의 작성 및 적용 방법을 설명합니다.

---

## 1. U-Boot 초기화의 필요성 (Altera 가이드라인)

HPS-FPGA SDRAM 브릿지 활성화 시퀀스는 다음과 같은 제약 사항을 갖습니다.

*   **`APPLYCFG` 비트 설정 조건**: 브릿지 설정을 최종 적용하려면 SDRAM 컨트롤러 제어 그룹의 `STATICCFG` 레지스터 내 `APPLYCFG` 비트를 `1`로 설정해야 합니다.
*   **완전한 유휴(Idle) 상태 요구**: 이 비트는 SDRAM 인터페이스에 **어떤 트랜잭션(ARM 코어, DMA 등)도 발생하지 않는 완벽한 유휴 상태에서만 기록 가능**합니다.
*   **Linux 런타임 제약**: Linux와 같은 OS가 실행 중일 때는 백그라운드 작업과 메모리 접근이 지속적으로 발생하므로 위 조건을 충족할 수 없습니다. 따라서 OS가 메모리를 본격적으로 제어하기 전인 U-Boot 단계에서만 안전한 초기화가 가능합니다. 이를 위반할 경우 시스템 전체가 멈추는(Hang) 치명적 오류가 발생합니다.

*참고: 최초 U-Boot 단계에서 포트 구성(데이터 폭, 방향 등)이 성공적으로 적용되었다면, 이후 Linux 런타임 중에도 동일한 포트 구성을 가진 다른 FPGA 이미지로 재구성(Reconfigure)하는 것은 가능합니다.*

---

## 2. 초기화 시퀀스

안전한 브릿지 활성화를 위한 절차는 다음과 같습니다.

1.  **FPGA 포트 리셋**: `FPGAPORTRST` 레지스터 값을 `0`으로 설정하여 `fpga2sdram` 주변기기의 FPGA 포트를 리셋 상태로 전환합니다.
2.  **FPGA 프로그래밍**: `.rbf` (Raw Binary File) 이미지로 FPGA 패브릭을 구성합니다. 이 과정에서 브릿지 입력단의 구성 포트가 활성화됩니다.
3.  **설정 적용**: `STATICCFG` 레지스터의 `APPLYCFG` 비트를 `1`로 설정하여 구성을 브릿지에 적용합니다. (SDRAM 유휴 상태 필수)
4.  **FPGA 포트 리셋 해제**: `FPGAPORTRST` 레지스터 비트를 `1`로 설정하여 리셋 상태를 해제하고 통신을 활성화합니다.

---

## 3. 부트 스크립트 작성 및 적용

매 부팅 시 위 시퀀스를 자동화하기 위해 U-Boot 스크립트를 작성합니다.

### 3.1. `bootscript.txt` 소스 작성

텍스트 편집기를 열어 아래 내용을 작성하고 `bootscript.txt`로 저장합니다. 

```sh
# FAT 파티션(0:1)에 sdr.rbf(FPGA 비트스트림) 파일 존재 여부 확인
if test -e mmc 0:1 sdr.rbf; then
  echo "Found sdr.rbf"
  
  # 1. fpga2sdram 브릿지 리셋 (fpgaportrst 비활성화)
  mw 0xFFC25080 0x0
  
  # 2. SD 카드에서 메모리로 .rbf 파일 로드 
  # (주소 0x3000000는 SDRAM 내 임시 로드 주소로, 환경에 따라 변경 가능)
  fatload mmc 0:1 0x3000000 sdr.rbf
  
  # 3. 로드된 이미지로 FPGA 프로그래밍 (0x700000은 넉넉히 잡은 파일 사이즈)
  fpga load 0 0x3000000 0x700000
  
  # 4. 설정 적용 (staticcfg 레지스터의 applycfg 비트 활성화)
  mw 0xFFC2505C 0xA
  
  # 5. fpga2sdram 브릿지 리셋 해제
  mw 0xFFC25080 0xFFFF
else
  echo "sdr.rbf not found, doing nothing"
fi;
```
> [!WARNING] **줄바꿈(Line-ending) 문자 주의!**
> Windows 환경(CRLF)에서 메모장 등으로 이 스크립트를 작성하면 U-Boot 실행 시 알 수 없는 `syntax error`가 발생하며 스크립트가 중단됩니다. 
> 반드시 에디터 하단의 줄바꿈 설정을 **Unix/Linux(LF)로 변경**하고 저장하거나, Linux/WSL 환경에서 `dos2unix bootscript.txt` 명령어로 윈도우 줄바꿈 문자를 제거한 뒤에 `mkimage` 로 컴파일하셔야 합니다.

*(주의: 사용하는 `.rbf` 파일명과 일치해야 합니다. 예제에서는 `sdr.rbf`를 사용했습니다.)*

### 3.2. 스크립트 컴파일 (`u-boot.scr` 생성)

작성한 텍스트 파일을 U-Boot 환경에서 실행할 수 있는 바이너리 스크립트로 변환합니다. `mkimage` 도구가 필요합니다. (Ubuntu 기준: `sudo apt-get install u-boot-tools` 로 설치)

```bash
# bootscript.txt가 있는 디렉토리에서 실행
mkimage -C none -A arm -T script -d bootscript.txt u-boot.scr
```

### 3.3. 타겟 보드 배포 및 커널 메모리 제한 확인

1.  **배포 (중요): SDRAM 접근이 필요한 FPGA 디자인 빌드 후, 해당 `.rbf` 파일의 이름을 반드시 `sdr.rbf`로 변경하여** 앞서 컴파일한 `u-boot.scr` 파일과 함께 보드의 SD 카드 FAT 파티션(보통 `/dev/mmcblk0p1`) 최상단에 복사합니다. 
    *(스크립트 전체가 이 `sdr.rbf`라는 파일 이름을 찾도록 하드코딩 되어 있기 때문입니다)*
2.  **커널 메모리 제한**: FPGA가 사용할 SDRAM 영역을 Linux 커널이 침범하지 않도록 설정해야 합니다. FAT 파티션 내 `extlinux/extlinux.conf` 파일을 열어 `APPEND` 라인 끝에 커널 메모리 제한 파라미터(예: `mem=512M`)를 추가하십시오.

보드를 재부팅하면 U-Boot가 스크립트를 감지하여 브릿지를 안전하게 초기화합니다.
