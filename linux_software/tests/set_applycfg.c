#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>


#define TARGET_ADDR 0xFFC25080
#define TARGET_VAL 0x3FFF

int main() {
  int fd;
  void *map_base, *virt_addr;
  off_t target = TARGET_ADDR;
  uint32_t read_val;

  // 1. 물리 메모리 디바이스 열기
  if ((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) {
    printf("Error: /dev/mem을 열 수 없습니다. sudo 권한을 확인하세요.\n");
    return -1;
  }

  // 2. 페이지 단위(보통 4KB)로 주소 계산
  unsigned long page_size = sysconf(_SC_PAGE_SIZE);
  unsigned long page_base = target & ~(page_size - 1);
  unsigned long page_offset = target - page_base;

  // 3. 물리 주소를 가상 주소로 매핑 (mmap)
  map_base =
      mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, page_base);
  if (map_base == (void *)-1) {
    printf("Error: mmap 실패\n");
    close(fd);
    return -1;
  }

  // 정확한 타겟 레지스터의 가상 주소
  virt_addr = (void *)((unsigned char *)map_base + page_offset);

  // 4. (옵션) 쓰기 전 현재 값 읽어보기
  read_val = *((volatile uint32_t *)virt_addr);
  printf("[Before] Address 0x%lX: 0x%08X\n", (unsigned long)target, read_val);

  // 5. 타겟 값(0x3FFF) 쓰기! (F2H SDRAM 포트 설정 적용 명령)
  *((volatile uint32_t *)virt_addr) = TARGET_VAL;
  printf(">> Wrote 0x%08X to 0x%lX\n", TARGET_VAL, (unsigned long)target);

  // 6. (옵션) 쓴 후 값 다시 읽어보기
  read_val = *((volatile uint32_t *)virt_addr);
  printf("[After ] Address 0x%lX: 0x%08X\n", (unsigned long)target, read_val);

  // 7. 정리
  if (munmap(map_base, page_size) == -1) {
    printf("Error: munmap 실패\n");
  }
  close(fd);

  return 0;
}
