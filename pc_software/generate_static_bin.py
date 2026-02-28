import struct
import os
from PIL import Image

# ==============================================================================
# Static Image ST2110 Payload Generator
# ==============================================================================
# This script generates a single frame of a static test pattern or an image
# and wraps it in exactly the same 62-byte headers (Ethernet+IP+UDP+RTP+SRD) 
# that the ST2110 Header Stripper RTL expects.
# 
# Outputs a binary file (`st2110_static_frame.bin`) that can be loaded directly
# into the FPGA's DDR memory via HPS/JTAG.
# ==============================================================================

WIDTH = 960
HEIGHT = 540
PACKET_PAYLOAD_SIZE = 1440

def create_ethernet_ip_udp_dummy_header():
    """
    Returns 42 bytes of dummy data to simulate Ethernet (14) + IP (20) + UDP (8).
    The RTL stripper only drops these, so the contents don't matter, just the length.
    """
    return b'\x00' * 42

def create_rtp_header(marker=False):
    """
    12-byte RTP header. The RTL stripper snoops byte 43[7] (Marker Bit).
    Note: Dummy headers = 42 bytes (0~41). 
    RTP starts at byte 42. Byte 43 is the second byte of RTP.
    """
    v_p_x_cc = 0x80
    m_pt = (1 << 7) | 96 if marker else 96
    seq_num = 0
    timestamp = 0
    ssrc = 0x12345678
    return struct.pack('!BBHII', v_p_x_cc, m_pt, seq_num, timestamp, ssrc)

def create_srd_header(line_number, offset):
    """
    8-byte SRD header. The RTL stripper snoops Line Number and Offset
    to determine the SOP (Start of Packet) for the frame.
    """
    ext_seq_num = 0
    length = PACKET_PAYLOAD_SIZE
    return struct.pack('!HHHH', ext_seq_num, length, line_number, offset)

def generate_static_image_chunk(img_data, y, offset):
    """
    Reads a 1440-byte chunk of RGB pixels from the loaded image array.
    img_data is a flat bytearray of size 960 * 540 * 3.
    """
    # Calculate the linear start index in the flat array
    pixel_byte_start = (y * WIDTH * 3) + offset
    
    # Extract the chunk
    payload = img_data[pixel_byte_start : pixel_byte_start + PACKET_PAYLOAD_SIZE]
    
    # Just in case the image was short (shouldn't happen with exact resize)
    if len(payload) < PACKET_PAYLOAD_SIZE:
        payload = payload + bytearray(PACKET_PAYLOAD_SIZE - len(payload))
        
    return payload

def build_binary_frame(image_path, filename):
    if image_path == "COLOR_BARS":
        print("Generating pure RGB color bars...")
        img_data = bytearray(WIDTH * HEIGHT * 3)
        bar_width = WIDTH // 5
        
        for y in range(HEIGHT):
            for x in range(WIDTH):
                idx = (y * WIDTH + x) * 3
                if x < bar_width:         # Red
                    img_data[idx:idx+3] = b'\xFF\x00\x00'
                elif x < bar_width * 2:   # Green
                    img_data[idx:idx+3] = b'\x00\xFF\x00'
                elif x < bar_width * 3:   # Blue
                    img_data[idx:idx+3] = b'\x00\x00\xFF'
                elif x < bar_width * 4:   # White
                    img_data[idx:idx+3] = b'\xFF\xFF\xFF'
                else:                     # Black
                    img_data[idx:idx+3] = b'\x00\x00\x00'
    else:
        print(f"Loading image: {image_path}")
        try:
            img = Image.open(image_path).convert('RGB')
            img = img.resize((WIDTH, HEIGHT))
            img_data = bytearray(img.tobytes())
            print(f"Image loaded and resized to {WIDTH}x{HEIGHT}.")
        except Exception as e:
            print(f"Error loading image: {e}")
            return

    print(f"Generating static 1-frame ST2110 binary: {filename}...")
    
    with open(filename, 'wb') as f:
        # 1 Frame = 540 lines
        for line_number in range(HEIGHT):
            # 1 Line = 2 packets (Offset 0 and Offset 1440)
            for offset in (0, 1440):
                # Is this the absolute last packet of the frame? (Marker bit for EOP)
                marker = (line_number == HEIGHT - 1) and (offset == 1440)
               
                # Build components
                dummy_eth_ip_udp = create_ethernet_ip_udp_dummy_header() # 42 bytes
                rtp_hdr = create_rtp_header(marker)                      # 12 bytes
                srd_hdr = create_srd_header(line_number, offset)         # 8 bytes
                payload = generate_static_image_chunk(img_data, line_number, offset) # 1440 bytes
               
                # Total packet size = 42 + 12 + 8 + 1440 = 1502 bytes
                packet = dummy_eth_ip_udp + rtp_hdr + srd_hdr + payload
                f.write(packet)
                
    total_bytes = HEIGHT * 2 * 1502
    print(f"Done! File size: {total_bytes} bytes.")
    print(f"-> Expected 32-bit words for FPGA: {total_bytes // 4}")

if __name__ == "__main__":
    img_path = "COLOR_BARS"
    out_path = "st2110_static_frame.bin"
    build_binary_frame(img_path, out_path)
