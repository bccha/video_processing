#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>


#define PORT 5000
#define BUFFER_SIZE 2048
#define UDP_PAYLOAD_SIZE 1460

int main() {
  int sockfd;
  struct sockaddr_in server_addr;
  char buffer[BUFFER_SIZE];
  int packet_count = 0;

  // Create socket
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);

  // Bind
  if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("Bind failed");
    exit(EXIT_FAILURE);
  }

  printf("--- ST2110 Step 1: UDP Receiver ---\n");
  printf("Listening on UDP port %d for video packets (1460 bytes)...\n", PORT);

  while (1) {
    ssize_t data_len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, NULL, NULL);
    if (data_len < 0) {
      perror("Recvfrom failed");
      break;
    }

    if (data_len == UDP_PAYLOAD_SIZE) {
      packet_count++;
      if (packet_count % 1080 == 0) {
        printf("[OK] Received 1 full frame (1080 packets). Total: %d\n",
               packet_count);
        fflush(stdout);
      }
    } else {
      // Print small packets or unexpected sizes for debug
      printf("[DEBUG] Unexpected packet size: %zd\n", data_len);
    }
  }

  close(sockfd);
  return 0;
}
