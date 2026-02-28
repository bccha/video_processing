import sys
import os
from PIL import Image

def convert_image_to_raw(input_path, output_path):
    # Target Resolution 540p
    WIDTH = 960
    HEIGHT = 540

    try:
        # Open and Resize Image
        img = Image.open(input_path)
        img = img.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
        
        # Ensure RGB format
        img = img.convert('RGB')
        
        print(f"Converting {input_path} ({img.size}) to {output_path}...")
        
        with open(output_path, 'wb') as f:
            for y in range(HEIGHT):
                for x in range(WIDTH):
                    r, g, b = img.getpixel((x, y))
                    # Format: 32-bit XRGB (0x00RRGGBB)
                    # Lower 24 bits are used in our hardware (hdmi_sync_gen.v)
                    pixel_data = bytearray([b, g, r, 0x00]) # Little Endian for ARM/Nios
                    f.write(pixel_data)
                    
        print(f"Successfully created {output_path} ({os.path.getsize(output_path)} bytes)")

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python img2raw.py <input_image> <output_raw>")
        sys.exit(1)
        
    convert_image_to_raw(sys.argv[1], sys.argv[2])
