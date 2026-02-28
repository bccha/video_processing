#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>


#define PAGE_SIZE 4096
#define PAGE_MASK (~(PAGE_SIZE - 1))

int main(int argc, char *argv[]) {
  // 사용법을 제대로 입력하지 않았을 경우
  if (argc < 3) {
    printf("FPGA/SDRAM 접근 테스트용 유틸리티 (devmem 대체)\n");
    printf("Usage:\n");
    printf("  Read  : %s r <hex_address>\n", argv[0]);
    printf("  Write : %s w <hex_address> <hex_value>\n\n", argv[0]);
    printf("Example (SDRAM 테스트):\n");
    printf("  %s w 0x20000000 0xAA\n", argv[0]);
    printf("  %s r 0x20000000\n", argv[0]);
    return -1;
  }

  char op = argv[1][0];
  off_t target_addr = strtoul(argv[2], NULL, 16);
  uint32_t write_val = 0;

  if (op == 'w') {
    if (argc < 4) {
      printf("오류: Write 명령은 추가로 값을 입력해야 합니다.\n");
      return -1;
    }
    write_val = strtoul(argv[3], NULL, 16);
  }

  // 관리자 권한(root)으로 실행하지 않으면 실패합니다.
  int fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd == -1) {
    perror("오류: /dev/mem 파일을 열 수 없습니다 (루트 권한인지 확인하세요!)");
    return -1;
  }

  // 주소를 페이지 크기에 맞춰 정렬
  off_t page_base = target_addr & PAGE_MASK;
  off_t page_offset = target_addr - page_base;

  // 물리 메모리를 가상 주소 공간으로 매핑 (혹시 모를 경계 접근을 대비해 2개
  // 페이지 매핑)
  void *map_base = mmap(NULL, PAGE_SIZE * 2, PROT_READ | PROT_WRITE, MAP_SHARED,
                        fd, page_base);
  if (map_base == MAP_FAILED) {
    perror("오류: mmap 매핑에 실패했습니다");
    close(fd);
    return -1;
  }

  // 우리가 접근할 가상 주소 포인터 계산 (32비트 포인터로 접근)
  volatile uint32_t *virt_addr = (uint32_t *)((char *)map_base + page_offset);

  if (op == 'r') {
    // Read: 메모리에서 값 읽기
    uint32_t read_val = *virt_addr;
    printf("읽기 완료! [물리 주소: 0x%08lX] 값: 0x%08X\n",
           (unsigned long)target_addr, read_val);
  } else if (op == 'w') {
    // Write: 메모리에 값 쓰기
    *virt_addr = write_val;
    printf("쓰기 완료! [물리 주소: 0x%08lX] 값: 0x%08X 를 썼습니다.\n",
           (unsigned long)target_addr, write_val);

    // 제대로 써졌는지 바로 읽어서 검증
    uint32_t read_val = *virt_addr;
    printf("검증 읽기: 0x%08X\n", read_val);
  } else {
    printf("오류: 알 수 없는 명령 '%c' 입니다. 'r' 또는 'w'를 사용하세요.\n",
           op);
  }

  // 자원 해제
  if (munmap(map_base, PAGE_SIZE * 2) == -1) {
    perror("오류: munmap 해제에 실패했습니다");
  }
  close(fd);

  return 0;
}
