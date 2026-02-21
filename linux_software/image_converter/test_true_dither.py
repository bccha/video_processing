from PIL import Image
import os

bayer_matrix = [
    [ 0,  8,  2, 10 ],
    [12,  4, 14,  6 ],
    [ 3, 11,  1,  9 ],
    [15,  7, 13,  5 ]
]

def process_image(input_path, output_path):
    img = Image.open(input_path).convert('RGB')
    
    # Force resize to 960x540 for the demonstration
    img = img.resize((960, 540), Image.Resampling.LANCZOS)
    
    # Save the resized original for comparison
    original_resized_path = output_path.replace('.png', '_original.png')
    img.save(original_resized_path)
    
    width, height = img.size
    pixels = img.load()
    
    TRUNC_MASK = 0xF0
    
    for y in range(height):
        for x in range(width):
            r, g, b = pixels[x, y]
            
            # Decorrelate RGB by using different spatial offsets
            # R: no offset, G: (x+1, y+2), B: (x+2, y+1)
            bayer_val_r = bayer_matrix[y % 4][x % 4]
            bayer_val_g = bayer_matrix[(y + 2) % 4][(x + 1) % 4]
            bayer_val_b = bayer_matrix[(y + 1) % 4][(x + 2) % 4]
            
            # Step 1: Add noise
            sum_r = r + bayer_val_r
            sum_g = g + bayer_val_g
            sum_b = b + bayer_val_b
            
            # Step 2: Clamp
            r_clamp = min(sum_r, 255)
            g_clamp = min(sum_g, 255)
            b_clamp = min(sum_b, 255)
            
            if x < 480:
                # Left: Simply truncate (simulate 4-bit color depth)
                r_out = r & TRUNC_MASK
                if (r >= 0x10):
                    r_out = r
                g_out = g & TRUNC_MASK
                if (g >= 0x10):
                    g_out = g
                b_out = b & TRUNC_MASK
                if (b >= 0x10):
                    b_out = b
            else:
                # Right: Dithered AND Truncated
                # Mathematically, ordered dithering IS (Value + Noise) followed by Quantization!
                r_out = r_clamp & TRUNC_MASK
                if (r >= 0x10):
                    r_out = r
                g_out = g_clamp & TRUNC_MASK
                if (g >= 0x10):
                    g_out = g
                b_out = b_clamp & TRUNC_MASK
                if (b >= 0x10):
                    b_out = b
                
            pixels[x, y] = (r_out, g_out, b_out)
            
    img.save(output_path)
    print(f"Saved: {output_path}")

import sys

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python test_true_dither.py <input.png> <output.png>")
        sys.exit(1)
        
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    if os.path.exists(input_file):
        process_image(input_file, output_file)
    else:
        print(f"File not found: {input_file}")
