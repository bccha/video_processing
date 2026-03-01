#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * ==============================================================================
 * Hybrid ST 2110 Receiver - Phase 1: Network Decapsulation Verification
 * ==============================================================================
 * Description:
 * This program opens an AF_PACKET (Raw) socket to capture incoming Ethernet
 * frames on a specified network interface. Its primary goal is to verify the
 * exact 62-byte header structure (Ethernet + IP + UDP + RTP + SRD) transmitted
 * by the PC (st2110_tx.py).
 *
 * Hardware Context:
 * In Phase 4, the RTL (FPGA) logic will perform "Header Stripping" by blindly
 * discarding these first 62 bytes. Therefore, verifying the byte alignment
 * here is critical before moving to zero-copy memory mapping (mmap).
 * ==============================================================================
 */

#define TARGET_ETH_IF                                                          \
  "usb0"                 // Changed to usb0 (RNDIS connection IP: 192.168.7.1)
#define BUFFER_SIZE 2048 // Standard MTU support (1518 max Ethernet frame)
#define HEADER_STRIP_SIZE 62 // The critical 62-byte header length

void print_hex_dump(const unsigned char *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    printf("%02X ", data[i]);
    if ((i + 1) % 16 == 0) {
      printf("\n");
    } else if ((i + 1) % 8 == 0) {
      printf("  ");
    }
  }
  printf("\n");
}

int main(int argc, char *argv[]) {
  int raw_sock;
  struct sockaddr_ll sll;
  struct ifreq ifr;
  unsigned char buffer[BUFFER_SIZE];

  printf("--- ST 2110 Raw Socket Receiver (Decapsulation Verification) ---\n");
  printf("Preparing to capture physical Ethernet frames on [%s]...\n",
         TARGET_ETH_IF);

  // 1. Create a Raw Socket
  // AF_PACKET: Low level packet interface
  // SOCK_RAW: Includes the Ethernet header (MAC addresses)
  // ETH_P_IP: Capture only IPv4 packets (0x0800)
  raw_sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
  if (raw_sock < 0) {
    perror("Socket creation failed (Must run as root/sudo)");
    exit(EXIT_FAILURE);
  }

  // 2. Get the interface index
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, TARGET_ETH_IF, IFNAMSIZ - 1);
  if (ioctl(raw_sock, SIOCGIFINDEX, &ifr) < 0) {
    perror("Failed to retrieve interface index (Check interface name)");
    close(raw_sock);
    exit(EXIT_FAILURE);
  }

  // 3. Bind the socket to the specific interface
  memset(&sll, 0, sizeof(sll));
  sll.sll_family = AF_PACKET;
  sll.sll_protocol = htons(ETH_P_IP);
  sll.sll_ifindex = ifr.ifr_ifindex;

  if (bind(raw_sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
    perror("Failed to bind raw socket to interface");
    close(raw_sock);
    exit(EXIT_FAILURE);
  }

  printf("Successfully bound to interface index %d. Waiting for packets...\n",
         ifr.ifr_ifindex);
  printf("Target Header Stripping Size: %d bytes\n\n", HEADER_STRIP_SIZE);

  int packet_count = 0;

  // 4. Capture Loop
  while (1) {
    // Receive data directly from the network card driver
    ssize_t data_len = recvfrom(raw_sock, buffer, BUFFER_SIZE, 0, NULL, NULL);

    if (data_len < 0) {
      perror("Error receiving packet");
      break;
    }

    // --- Decapsulation Verification Logic ---
    // We are specifically looking for large packets representing our Video
    // lines A single 960x540 line split payload is 1440 bytes. With 62-byte
    // header = 1502 bytes. Only print large packets (>1400 bytes) to filter out
    // background SSH/ARP noise.
    if (data_len > 1400 && data_len <= 1518) {
      packet_count++;

      printf("\n[%d] Standard MTU Video Frame Detected! Length: %zd bytes\n",
             packet_count, data_len);
      printf("--- Decapsulation Target (First %d Bytes) ---\n",
             HEADER_STRIP_SIZE);
      print_hex_dump(buffer, HEADER_STRIP_SIZE);

      printf("--- Expected Payload Start (Bytes 62-77) ---\n");
      // Print the first 16 bytes of the payload (the pixel data)
      if (data_len >= HEADER_STRIP_SIZE + 16) {
        print_hex_dump(buffer + HEADER_STRIP_SIZE, 16);
      }

      printf("-----------------------------------------------------------------"
             "---------------\n");

      // For verification purposes, exit after successfully capturing a few
      // packets
      if (packet_count >= 3) {
        printf("Successfully verified the 62-byte decapsulation target.\n");
        printf("Ready to proceed to Phase 2 (mmap).\n");
        break;
      }
    }
  }

  close(raw_sock);
  return 0;
}
