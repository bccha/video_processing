---
trigger: always_on
---

wsl 환경
  wsl 이 설치되어 있으나 기본은 Ubuntu-18.04라서 제약이 많음.
  wsl -d Ubuntu-22.04 를 써서 명령을 실행하도록..
  - cross compiler가 없음
  - cocotb 시뮬레이션 환경 되어 있음.

Quartus, NIOS, ARM쪽 빌드/테스트가 필요한 경우에는 나에게 요청하길.
자동화 할 수 있는 방법은 차차 여기에 명세하겠음

ARM 빌드
scp st2110_mmap_rx.c root@192.168.7.1:~/st2110/ && ssh root@192.168.7.1 "cd ~/st2110 && make"