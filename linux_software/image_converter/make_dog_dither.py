import sys
from PIL import Image

bayer_matrix = [
    [ 0,  8,  2, 10 ],
    [12,  4, 14,  6 ],
    [ 3, 11,  1,  9 ],
    [15,  7, 13,  5 ]
]

def make_dither():
    try:
        img = Image.open('dog.png').convert('RGB')
    except Exception as e:
        print(f"Error opening dog.png: {e}")
        return
        
    img = img.resize((960, 540), Image.Resampling.LANCZOS)
    img.save('dog_original_resized.png')
    
    # 1. Make Raw Truncated (No Dither)
    raw_img = Image.new('RGB', (960, 540))
    raw_pixels = raw_img.load()
    orig_pixels = img.load()
    
    for y in range(540):
        for x in range(960):
            r, g, b = orig_pixels[x, y]
            # Simulate a display that can only show 4-bit color (lower 4 bits zeroed)
            # Anything below 16 (0x10) naturally becomes 0 when masked with 0xF0.
            r_out = r & 0xF0
            g_out = g & 0xF0
            b_out = b & 0xF0
            raw_pixels[x, y] = (r_out, g_out, b_out)
            
    raw_img.save('dog_clamped.png')
    print("Saved dog_clamped.png")
    
    # 2. Make 16 frames for Temporal Dithered GIF
    frames = []
    print("Generating 16 frames for GIF...")
    for f in range(16):
        frame_img = Image.new('RGB', (960, 540))
        frame_pixels = frame_img.load()
        
        # Scrambling logic matching Verilog
        f0 = (f >> 0) & 1
        f1 = (f >> 1) & 1
        f2 = (f >> 2) & 1
        f3 = (f >> 3) & 1
        
        x_offset = (f0 << 1) | f2
        y_offset = (f1 << 1) | f3
        
        for y in range(540):
            for x in range(960):
                r, g, b = orig_pixels[x, y]
                
                # Decorrelation offsets: R: (0,0), G: (1,2), B: (2,1)
                x_idx_r = x + x_offset
                y_idx_r = y + y_offset
                bayer_r = bayer_matrix[y_idx_r % 4][x_idx_r % 4]
                
                x_idx_g = x + x_offset + 1
                y_idx_g = y + y_offset + 2
                bayer_g = bayer_matrix[y_idx_g % 4][x_idx_g % 4]
                
                x_idx_b = x + x_offset + 2
                y_idx_b = y + y_offset + 1
                bayer_b = bayer_matrix[y_idx_b % 4][x_idx_b % 4]
                
                sum_r = r + bayer_r
                sum_g = g + bayer_g
                sum_b = b + bayer_b
                
                r_clamp = min(sum_r, 255)
                g_clamp = min(sum_g, 255)
                b_clamp = min(sum_b, 255)
                
                # Unconditionally dither and truncate. 
                # Dark values (e.g. 5) will occasionally hit 5+15=20 -> 0x14 & 0xF0 = 0x10 (Visible!)
                r_out = r_clamp & 0xF0
                g_out = g_clamp & 0xF0
                b_out = b_clamp & 0xF0
                
                frame_pixels[x, y] = (r_out, g_out, b_out)
                
        frames.append(frame_img)
        print(f"  Frame {f+1}/16 done")
        
    frames[0].save('dog_temporal_dither.gif', save_all=True, append_images=frames[1:], duration=60, loop=0)
    print("Saved dog_temporal_dither.gif")

if __name__ == '__main__':
    make_dither()
