
import struct
import os

def write_bmp(filename, width, height, pixels):
    # BMP Header
    file_size = 54 + len(pixels) * 3
    # 54 bytes header
    # BM magic
    header = b'BM'
    header += struct.pack('<I', file_size)
    header += b'\x00\x00' # Reserved
    header += b'\x00\x00' # Reserved
    header += struct.pack('<I', 54) # Offset to pixel array
    
    # DIB Header
    header += struct.pack('<I', 40) # DIB header size
    header += struct.pack('<i', width)
    header += struct.pack('<i', -height) # Negative height for top-down
    header += struct.pack('<H', 1) # Planes
    header += struct.pack('<H', 24) # Bits per pixel
    header += struct.pack('<I', 0) # Compression (BI_RGB)
    header += struct.pack('<I', 0) # Image size (can be 0 for BI_RGB)
    header += struct.pack('<i', 0) # X pixels per meter
    header += struct.pack('<i', 0) # Y pixels per meter
    header += struct.pack('<I', 0) # Colors in palette
    header += struct.pack('<I', 0) # Important colors
    
    with open(filename, 'wb') as f:
        f.write(header)
        for p in pixels:
            # 24-bit RGB: B, G, R
            r = (p >> 16) & 0xFF
            g = (p >> 8) & 0xFF
            b = p & 0xFF
            f.write(struct.pack('BBB', b, g, r))

def main():
    input_file = "hdmi_output.bin"
    # Adjust path to reach image.raw from tests/cocotb
    ref_image_path = "../../linux_software/image_converter/image.raw"
    width = 960
    
    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found.")
        return

    print(f"Reading {input_file}...")
    with open(input_file, "rb") as f:
        data = f.read()

    total_pixels = len(data) // 4
    # Unpack all data
    all_pixels = struct.unpack(f"<{total_pixels}I", data)
    
    # Load Reference
    if os.path.exists(ref_image_path):
        print(f"Loading reference image: {ref_image_path}...")
        with open(ref_image_path, "rb") as f:
            ref_data = f.read()
        ref_len = len(ref_data) // 4
        ref_pixels = struct.unpack(f"<{ref_len}I", ref_data)
        print(f"Reference Image loaded: {len(ref_pixels)} pixels")
    else:
        print(f"Warning: Reference image {ref_image_path} not found. Skipping verification.")
        ref_pixels = None

    FRAME_SIZE = 960 * 540
    
    # Helper for verification
    def verify_chunk(frame_name, pixels, start_offset_in_ref):
        if ref_pixels is None: return
        match = True
        err_count = 0
        for i in range(len(pixels)):
            ref_idx = (i + start_offset_in_ref) % len(ref_pixels)
            if pixels[i] != ref_pixels[ref_idx]:
                match = False
                err_count += 1
                if err_count < 5:
                    print(f"  [FAIL] Pixel {i}: Sim {pixels[i]:06X} != Ref {ref_pixels[ref_idx]:06X}")
        
        if match:
            print(f"  [PASS] {frame_name} Verified against Reference Image!")
        else:
            print(f"  [FAIL] {frame_name} had {err_count} mismatches.")

    # Frame 0, 1, 2: All Full Frames
    for f_idx in range(3):
        start_px = f_idx * FRAME_SIZE
        if total_pixels >= start_px + FRAME_SIZE:
            print(f"Saving Frame {f_idx} (Full Frame)...")
            chunk = all_pixels[start_px : start_px + FRAME_SIZE]
            write_bmp(f"frame_{f_idx}_full.bmp", width, 540, chunk)
            verify_chunk(f"Frame {f_idx}", chunk, 0)
    
    print("Done!")

if __name__ == "__main__":
    main()
