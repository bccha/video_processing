# [U-Boot] FPGA-to-SDRAM 브릿지 초기화 가이드

## 개요
FPGA 패브릭에서 HPS (Hard Processor System) 측의 SDRAM에 접근하려면 `fpga2sdram` AXI 브릿지를 활성화해야 합니다. 이 브릿지의 초기화는 **반드시 U-Boot 부트로더 단계에서 수행**되어야 하며, 상위 운영체제(Linux 등)가 부팅된 이후에는 초기화가 불가능합니다. 

## 중요한 사전 필수 조건 및 베이스라인 (Prerequisites & Baseline)

성공적으로 작동하는 SDRAM 버스 연결 및 Linux OS 환경을 구성하기 위해, 현재 이 프로젝트는 Terasic DE10-Nano의 공식 데모 중 하나를 완벽한 베이스라인(Baseline)으로 참조하고 있습니다.

*   **베이스라인 데모 (Known Good Reference):** `DE10\Demonstrations\SoC_FPGA\Nios_Access_DDR3`
    *   초기 검증 시, 이 데모 폴더 내에 있는 `demo_batch\soc_system.rbf` 파일을 SD 카드에 넣고 부팅하면 SDRAM 버스가 즉시 정상적으로 열리는 것이 확인되었습니다. 즉, 이 데모의 Pre-loader(U-Boot) 환경 설정과 OS 조합이 SDRAM 브릿지를 여는 최적의 뼈대입니다.
*   **리눅스 OS 이미지 플래싱:** 위 환경과 호환되도록, 반드시 **`DE10_Nano_LXDE.img` (kernel 4.5)** 이미지를 구해서 **balenaEtcher** 같은 툴로 SD 카드에 구운(Flashing) 뒤 사용해야 합니다.
    *   *주의사항:* 해당 LXDE 데스크톱 이미지를 사용할 경우, 데스크톱 콘솔 환경과 충돌하여 `usb0` (가상 이더넷/RNDIS)를 통한 랜 연결은 동작하지 않는다는 것을 감안해야 합니다.
    *   *GUI 충돌 방지:* 데스크톱 이미지이므로 부팅 시 X11 윈도우 시스템이 HDMI를 점유하려 듭니다. 우리가 구성한 커스텀 비디오 파이프라인 출력이 방해받지 않도록 윈도우(GUI) 매니저를 강제로 완전히 죽여야 합니다. 리눅스 부팅 후 루트 권한으로 다음 명령어들을 실행해 주세요:
        ```bash
        sudo systemctl stop lightdm
        sudo systemctl disable lightdm
        sudo systemctl mask lightdm
        ```
*   **부트로더(U-Boot) 커널 메모리 제한 설정 (필수):** SD 카드를 굽고 처음 부팅할 때, FPGA가 독점적으로 사용할 SDRAM 영역(여유 공간)을 확보하기 위해 리눅스 커널 메모리를 512MB로 강제 제한해야 합니다. 부팅 도중 키보드 Enter를 눌러 U-Boot 셸로 진입한 뒤, 다음 명령어를 실행하여 설정을 저장합니다:
    ```bash
    setenv mmcboot "setenv bootargs console=ttyS0,115200 root=/dev/mmcblk0p2 rw rootwait mem=512M; bootz 0x8000 - 0x00000100"
    env save
    ```
*   **FPGA 비트스트림 포맷 변환 (커스텀 빌드 시):** 베이스라인 확인을 마치고 우리의 커스텀 프로젝트(Quartus 빌드 결과물인 `.sof`)를 올릴 때는 반드시 디자인을 **`.rbf` (Raw Binary File) 형식으로 변환**한 다음 SD 카드의 FAT 영역에 덮어써야 합니다. (Quartus 내의 Convert Programming File 툴 사용)

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
  
  # 5. fpga2sdram 브릿지 전체 포트 개방 및 리셋 해제
  # (0x1FF = 포트 0~5번까지 읽기/쓰기 권한을 모두 오픈)
  mw 0xFFC25080 0x000001FF
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

### 3.3. 타겟 보드 배포 및 최종 부팅

1.  **배포 (중요): SDRAM 접근이 필요한 FPGA 디자인 빌드 후, 해당 `.rbf` 파일의 이름을 반드시 `sdr.rbf`로 변경하여** 앞서 컴파일한 `u-boot.scr` 플래시 파일과 함께 보드의 SD 카드 FAT 파티션(보통 `/dev/mmcblk0p1`) 최상단에 복사합니다. 
    *(작성된 U-boot 스크립트가 하드코딩된 `sdr.rbf` 파일명을 찾도록 되어 있기 때문입니다)*

이미 앞선 사전 설정 단계(Prerequisites)에서 `env save` 로 부트 파라미터(`mem=512M`)를 저장해 두었으므로, 보드를 재부팅하면 U-Boot가 구워진 스크립트(`u-boot.scr`)를 감지하여 브릿지를 안전하게 초기화하고 512M로 제한된 커널을 띄웁니다.

---

## 4. (참고사항) SDRAM 다중 포트(f2h_sdram1 등) 권한 개방 및 Linux 유틸리티

기본적으로 HPS SDRAM 컨트롤러는 `f2h_sdram0` 이외의 추가 명령 포트들(예: `f2h_sdram1`, `f2h_sdram2`)에 대한 접근이 마스킹되어 닫혀 있는 경우가 많습니다.
Qsys에서 다중 포트를 뚫었더라도, 리눅스 상에서 해당 포트를 통해 메모리에 Write/Read를 시도하면 시스템이 멈추거나 동작하지 않습니다.

이 문제를 해결하려면 **FPGA 포트 보호 레지스터(`FPGAPORTRST`, 주소: `0xFFC25080`)** 에 `0x000001FF` (모든 포트의 읽기/쓰기 허용 마스크) 값을 명시적으로 써야만 합니다.

### 방법 A: Linux 부팅 스크립트 (`rc.local`) 사용
Linux 부팅이 끝난 시점에서 루트 권한의 유틸리티를 실행하여 포트를 여는 방법입니다. (임시 우회책)
*   **적용:** `sudo nano /etc/rc.local`을 열어 `exit 0` 위에 C 프로그램 유틸리티 경로(예: `/home/root/test/apply`)를 적어둡니다. 유틸리티는 내부에 `mmap`을 통해 `0xFFC25080` 번지에 `0x1FF`를 씁니다.

### 방법 B: U-Boot 스크립트 수정 (권장 해결책)
애초에 커널이 부팅되기 전에 U-Boot 단계에서 `mw` (Memory Write) 명령어로 초기화를 끝내고 넘어가는 것이 가장 깔끔한 방법입니다.
*   **적용:** 위의 **[3.1. bootscript.txt 소스 작성]** 에 기재된 대로, `mw 0xFFC25080 0x000001FF` 명령줄을 스크립트 하단에 반드시 추가한 후 `mkimage` 로 컴파일하여 사용합니다.
