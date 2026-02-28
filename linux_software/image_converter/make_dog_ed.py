import sys
from PIL import Image

def make_error_diffusion_demo():
    try:
        img = Image.open('dog.png').convert('RGB')
    except Exception as e:
        print(f"Error opening dog.png: {e}")
        return
        
    img = img.resize((960, 540), Image.Resampling.LANCZOS)
    
    threshold = 0x10
    
    diff_img = Image.new('RGB', (960, 540))
    diff_pixels = diff_img.load()
    orig_pixels = img.load()
    
    # We need error buffers. For simplicity in python, we can just use a 2D array of floats or ints.
    # To match Verilog, we'll use integers.
    err_r = [[0 for _ in range(960)] for _ in range(540)]
    err_g = [[0 for _ in range(960)] for _ in range(540)]
    err_b = [[0 for _ in range(960)] for _ in range(540)]
    
    for y in range(540):
        for x in range(960):
            r, g, b = orig_pixels[x, y]
            
            # 1. Add diffused error to current pixel
            val_r = r + err_r[y][x]
            val_g = g + err_g[y][x]
            val_b = b + err_b[y][x]
            
            # 2. Output determination based on threshold bypass logic:
            # If original pixel (or val? Let's use val to be safe and match standard ED) is >= threshold, BYPASS.
            # Wait, user said: "threshold 이상은 그냥 bypass시킨다. threshold 미만은 dither를 해서 출력시킨다."
            # "error는 threshold 와 상관없이 흘려보낸다."
            
            # If val < threshold, we quantize (to 0, or just truncate) and calculate error.
            # But the user implies if it's >= threshold, we output exactly the original pixel (bypassed, no 4-bit truncation maybe? or just standard 8-bit bypass without thresholding?)
            # Let's assume bypass means outputting the full 8-bit value (or 4-bit truncated, but "bypass" usually means leave it alone).
            # "truncate를 막 시키지 말고. 전체 계조에 대해서 dither 적용하지 말고"
            # This implies if it's >= 0x10, output original `r` directly (bypassing 4-bit truncation entirely?!).
            # Let's test outputting `val` directly if >= threshold. 
            # Or output `r` directly? If we output `r` directly, we still calculate error as `val_r - r`.
            
            if val_r >= threshold:
                out_r = r # True bypass of original pixel
            else:
                out_r = 0 # Dithered to black if it falls below threshold
                
            if val_g >= threshold:
                out_g = g
            else:
                out_g = 0
                
            if val_b >= threshold:
                out_b = b
            else:
                out_b = 0
                
            out_r_clamp = max(0, min(255, out_r))
            out_g_clamp = max(0, min(255, out_g))
            out_b_clamp = max(0, min(255, out_b))
            
            diff_pixels[x, y] = (out_r_clamp, out_g_clamp, out_b_clamp)
            
            # Error is ALWAYS passed down, regardless of threshold!
            e_r = val_r - out_r_clamp
            e_g = val_g - out_g_clamp
            e_b = val_b - out_b_clamp
            
            # Diffuse Error (Floyd-Steinberg: 7/16, 3/16, 5/16, 1/16)
            if x + 1 < 960:
                err_r[y][x+1] += (e_r * 7) // 16
                err_g[y][x+1] += (e_g * 7) // 16
                err_b[y][x+1] += (e_b * 7) // 16
                
            if y + 1 < 540:
                if x - 1 >= 0:
                    err_r[y+1][x-1] += (e_r * 3) // 16
                    err_g[y+1][x-1] += (e_g * 3) // 16
                    err_b[y+1][x-1] += (e_b * 3) // 16
                    
                err_r[y+1][x] += (e_r * 5) // 16
                err_g[y+1][x] += (e_g * 5) // 16
                err_b[y+1][x] += (e_b * 5) // 16
                
                if x + 1 < 960:
                    err_r[y+1][x+1] += (e_r * 1) // 16
                    err_g[y+1][x+1] += (e_g * 1) // 16
                    err_b[y+1][x+1] += (e_b * 1) // 16

    diff_img.save('dog_error_diffusion_bypass.png')
    print("Saved dog_error_diffusion_bypass.png")

if __name__ == '__main__':
    make_error_diffusion_demo()
