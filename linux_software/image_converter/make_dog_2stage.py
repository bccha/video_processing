import sys
from PIL import Image

bayer_matrix = [
    [ 0,  8,  2, 10 ],
    [12,  4, 14,  6 ],
    [ 3, 11,  1,  9 ],
    [15,  7, 13,  5 ]
]

def make_2stage_dither():
    try:
        img = Image.open('dog.png').convert('RGB')
    except Exception as e:
        print(f"Error opening dog.png: {e}")
        return
        
    img = img.resize((960, 540), Image.Resampling.LANCZOS)
    orig_pixels = img.load()
    
    frames = []
    print("Generating 16 frames for 2-stage GIF...")
    
    for f in range(16):
        # -----------------------------
        # PASS 1: Temporal Dither (2-bit)
        # -----------------------------
        pass1_img = Image.new('RGB', (960, 540))
        pass1_pixels = pass1_img.load()
        
        f0 = (f >> 0) & 1
        f1 = (f >> 1) & 1
        f2 = (f >> 2) & 1
        f3 = (f >> 3) & 1
        
        x_offset = (f0 << 1) | f2
        y_offset = (f1 << 1) | f3
        
        for y in range(540):
            for x in range(960):
                r, g, b = orig_pixels[x, y]
                
                # Decorrelation
                x_idx_r = x + x_offset
                y_idx_r = y + y_offset
                bayer_r = bayer_matrix[y_idx_r % 4][x_idx_r % 4]
                
                x_idx_g = x + x_offset + 1
                y_idx_g = y + y_offset + 2
                bayer_g = bayer_matrix[y_idx_g % 4][x_idx_g % 4]
                
                x_idx_b = x + x_offset + 2
                y_idx_b = y + y_offset + 1
                bayer_b = bayer_matrix[y_idx_b % 4][x_idx_b % 4]
                
                # 2-bit Dither Noise is 0~3
                noise_r = bayer_r >> 2
                noise_g = bayer_g >> 2
                noise_b = bayer_b >> 2
                
                # R channel pass 1
                if r >= 4:
                    out1_r = r
                else:
                    sum_r = r + noise_r
                    out1_r = sum_r & 0xFC
                    
                # G channel pass 1
                if g >= 4:
                    out1_g = g
                else:
                    sum_g = g + noise_g
                    out1_g = sum_g & 0xFC
                    
                # B channel pass 1
                if b >= 4:
                    out1_b = b
                else:
                    sum_b = b + noise_b
                    out1_b = sum_b & 0xFC
                    
                pass1_pixels[x, y] = (out1_r, out1_g, out1_b)
                
        # -----------------------------
        # PASS 2: Error Diffusion (4-bit threshold bypass)
        # -----------------------------
        pass2_img = Image.new('RGB', (960, 540))
        pass2_pixels = pass2_img.load()
        
        err_r = [[0.0 for _ in range(960)] for _ in range(540)]
        err_g = [[0.0 for _ in range(960)] for _ in range(540)]
        err_b = [[0.0 for _ in range(960)] for _ in range(540)]
        
        for y in range(540):
            for x in range(960):
                r1, g1, b1 = pass1_pixels[x, y]
                
                val_r = r1 + err_r[y][x]
                val_g = g1 + err_g[y][x]
                val_b = b1 + err_b[y][x]
                
                # R channel pass 2
                if r1 >= 16:  
                    out2_r = r1
                else:
                    if val_r < 0:
                        out2_r = 0
                    elif val_r > 255:
                        out2_r = 240
                    else:
                        out2_r = int(val_r) & 0xF0
                        
                # G channel
                if g1 >= 16:
                    out2_g = g1
                else:
                    if val_g < 0:
                        out2_g = 0
                    elif val_g > 255:
                        out2_g = 240
                    else:
                        out2_g = int(val_g) & 0xF0
                        
                # B channel
                if b1 >= 16:
                    out2_b = b1
                else:
                    if val_b < 0:
                        out2_b = 0
                    elif val_b > 255:
                        out2_b = 240
                    else:
                        out2_b = int(val_b) & 0xF0
                    
                pass2_pixels[x, y] = (int(max(0, min(255, out2_r))), 
                                      int(max(0, min(255, out2_g))), 
                                      int(max(0, min(255, out2_b))))
                
                # Calculate Error: diffuse difference between what we WANTED to output (val) and what we actually OUTPUT.
                e_r = val_r - out2_r
                e_g = val_g - out2_g
                e_b = val_b - out2_b
                
                if x + 1 < 960:
                    err_r[y][x+1] += (e_r * 7) / 16.0
                    err_g[y][x+1] += (e_g * 7) / 16.0
                    err_b[y][x+1] += (e_b * 7) / 16.0
                if y + 1 < 540:
                    if x - 1 >= 0:
                        err_r[y+1][x-1] += (e_r * 3) / 16.0
                        err_g[y+1][x-1] += (e_g * 3) / 16.0
                        err_b[y+1][x-1] += (e_b * 3) / 16.0
                    err_r[y+1][x] += (e_r * 5) / 16.0
                    err_g[y+1][x] += (e_g * 5) / 16.0
                    err_b[y+1][x] += (e_b * 5) / 16.0
                    if x + 1 < 960:
                        err_r[y+1][x+1] += (e_r * 1) / 16.0
                        err_g[y+1][x+1] += (e_g * 1) / 16.0
                        err_b[y+1][x+1] += (e_b * 1) / 16.0
                        
        frames.append(pass2_img)
        print(f"  Frame {f+1}/16 done")
        
    frames[0].save('dog_2stage_dither.gif', save_all=True, append_images=frames[1:], duration=60, loop=0)
    print("Saved dog_2stage_dither.gif")

if __name__ == '__main__':
    make_2stage_dither()
