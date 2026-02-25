import socket
import struct
import time
import argparse
import sys

# ==============================================================================
# PC Transmitter for Hybrid ST 2110 Receiver (Phase 1)
# ==============================================================================
# This script sends uncompressed 960x540 24-bit video data over UDP imitating
# SMPTE ST 2110-20. To support standard Gigabit Ethernet switches (MTU 1500),
# it splits the 2880 bytes of 1 video line into two 1440-byte packets.
#
# Packet Structure (62-byte header target for FPGA stripping):
# [Ethernet (14)] + [IP (20)] + [UDP (8)] + [RTP (12)] + [SRD (8)] + [Payload (1440)]
# Note: Ethernet/IP/UDP headers are handled by the OS network stack.
# This script constructs the RTP + SRD + Payload.
# ==============================================================================

# --- Configuration Constants ---
WIDTH = 960
HEIGHT = 540
BYTES_PER_PIXEL = 3 # 24-bit RGB
BYTES_PER_LINE = WIDTH * BYTES_PER_PIXEL # 2880 bytes per line
PACKET_PAYLOAD_SIZE = 1440 # Fits safely in standard 1500 MTU

# RTP/SRD Header Configuration
RTP_PAYLOAD_TYPE = 96
RTP_SSRC = 0x12345678

def create_rtp_header(seq_num, timestamp, marker=False):
    """
    Constructs a 12-byte RTP header.
    Format:
    [V(2) P(1) X(1) CC(4)] [M(1) PT(7)] [Sequence Number (16)]
    [Timestamp (32)]
    [SSRC (32)]
    """
    v_p_x_cc = 0x80 # Version 2, No Padding, No Extension, CSRC Count 0
    m_pt = (1 << 7) | RTP_PAYLOAD_TYPE if marker else RTP_PAYLOAD_TYPE
    
    # struct.pack format:
    # ! : Network byte order (Big-Endian)
    # B : unsigned char (1 byte)
    # H : unsigned short (2 bytes)
    # I : unsigned int (4 bytes)
    header = struct.pack('!BBHII', v_p_x_cc, m_pt, seq_num, timestamp, RTP_SSRC)
    return header

def create_srd_header(line_number, offset, length):
    """
    Constructs an 8-byte SRD (Sample Row Data) header (Simplified).
    Format (RFC 4175 / ST 2110-20):
    [Extended Sequence Number (16)]
    [Length (16)]
    [Row Number (16)]
    [Offset (16)]
    
    * Note: This is a simplified 8-byte SRD payload header tailored for the 
    62-byte total stripping goal in the RTL phase.
    """
    ext_seq_num = 0x0000
    header = struct.pack('!HHHH', ext_seq_num, length, line_number, offset)
    return header

def generate_color_bar_line(line_number, frame_count):
    """
    Generates a 2880-byte payload representing one line of a moving color bar.
    24-bit RGB pixels. We'll simulate a simple moving pattern.
    """
    payload = bytearray(BYTES_PER_LINE)
    
    # Simple moving vertical bars
    shift = (frame_count * 4) % WIDTH
    
    for x in range(WIDTH):
        # Determine bar color based on x position
        bar_idx = ((x + shift) // 120) % 8
        
        # 24-bit color values (RGB 888) for 8 bars
        colors = [
            (0xFF, 0xFF, 0xFF), # White
            (0xFF, 0xFF, 0x00), # Yellow
            (0x00, 0xFF, 0xFF), # Cyan
            (0x00, 0xFF, 0x00), # Green
            (0xFF, 0x00, 0xFF), # Magenta
            (0xFF, 0x00, 0x00), # Red
            (0x00, 0x00, 0xFF), # Blue
            (0x00, 0x00, 0x00)  # Black
        ]
        r, g, b = colors[bar_idx]
        
        # Write 3 bytes per pixel (RGB)
        payload[x*3]     = r
        payload[x*3 + 1] = g
        payload[x*3 + 2] = b
        
    return payload

def send_video_stream(target_ip, target_port, fps=60):
    """
    Main loop to send the simulated ST 2110 stream.
    """
    print(f"Targeting: {target_ip}:{target_port}")
    print(f"Resolution: {WIDTH}x{HEIGHT}, {fps} FPS")
    print(f"Payload per packet: {PACKET_PAYLOAD_SIZE} bytes (Standard MTU support)")
    
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    # Optional: Set socket buffer size to prevent dropping on the sender side
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1024 * 1024 * 8)
    
    seq_num = 0
    timestamp = 0
    frame_count = 0
    
    frame_duration = 1.0 / fps
    
    try:
        while True:
            start_time = time.time()
            
            # Send one complete frame (540 lines)
            for line_number in range(HEIGHT):
                # 2. Build Full Line Payload
                full_line_payload = generate_color_bar_line(line_number, frame_count)
                
                # Split the 2880-byte line into two 1440-byte packets
                for offset in (0, 1440):
                    # Is this the last packet of the frame?
                    marker = (line_number == HEIGHT - 1) and (offset == 1440)
                    
                    # 1. Build Headers
                    rtp_hdr = create_rtp_header(seq_num, timestamp, marker)
                    srd_hdr = create_srd_header(line_number, offset, PACKET_PAYLOAD_SIZE)
                    
                    # Extract the chunk for this packet
                    packet_payload = full_line_payload[offset:offset + PACKET_PAYLOAD_SIZE]
                    
                    # 3. Assemble Packet: RTP (12) + SRD (8) + Payload (1440) = 1460 bytes
                    packet = rtp_hdr + srd_hdr + packet_payload
                    
                    # 4. Send Packet via UDP
                    # Note: The OS will append Ethernet (14), IP (20), and UDP (8) headers.
                    # Total length on wire = 14 + 20 + 8 + 1460 = 1502 bytes (Fits in 1518 MTU).
                    sock.sendto(packet, (target_ip, target_port))
                    
                    seq_num = (seq_num + 1) % 65536
                
            # Advance timestamp for the next frame (e.g., 90kHz clock ticks)
            # 90000 / 60 = 1500 ticks per frame
            timestamp = (timestamp + 1500) % 0x100000000
            frame_count += 1
            
            # FPS Pacing
            elapsed_time = time.time() - start_time
            sleep_time = frame_duration - elapsed_time
            if sleep_time > 0:
                time.sleep(sleep_time)
            
            # Print status every 60 frames
            if frame_count % 60 == 0:
                print(f"Sent {frame_count} frames. Sequence number: {seq_num}")

    except KeyboardInterrupt:
        print("\nTransmission stopped by user.")
    finally:
        sock.close()
        print("Socket closed.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Simulate ST 2110-20 Video Stream Transmitter (Jumbo Frames)")
    parser.add_argument("--ip", type=str, required=True, help="Target IP address (e.g., 192.168.1.100)")
    parser.add_argument("--port", type=int, default=5000, help="Target UDP port (default: 5000)")
    parser.add_argument("--fps", type=int, default=60, help="Target Frames Per Second (default: 60)")
    
    args = parser.parse_args()
    
    send_video_stream(args.ip, args.port, args.fps)
