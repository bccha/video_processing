import sys
import numpy as np
from PIL import Image
import math

# Try importing skimage for professional PSNR/SSIM
try:
    from skimage.metrics import structural_similarity as ssim_func
    from skimage.metrics import peak_signal_noise_ratio as psnr_func
    HAS_SKIMAGE = True
except ImportError:
    HAS_SKIMAGE = False

def calculate_psnr_manual(img1, img2):
    mse = np.mean((img1.astype(float) - img2.astype(float)) ** 2)
    if mse == 0: return 100.0
    return 20 * math.log10(255.0 / math.sqrt(mse))

bayer_matrix = [
    [ 0,  8,  2, 10 ],
    [12,  4, 14,  6 ],
    [ 3, 11,  1,  9 ],
    [15,  7, 13,  5 ]
]

def simulate_pipeline(orig_img):
    w, h = orig_img.size
    pixels = np.array(orig_img)
    
    # 1. Truncation (Baseline)
    trunc_img = (pixels & 0xF0)
    
    # 2. Hybrid 2-Stage (Simulation)
    # Pass 1: Temporal
    pass1 = np.zeros_like(pixels)
    for y in range(h):
        for x in range(w):
            for c in range(3):
                p = pixels[y, x, c]
                noise = bayer_matrix[y % 4][x % 4] >> 2 # 2-bit
                if p >= 4: pass1[y, x, c] = p
                else:      pass1[y, x, c] = (p + noise) & 0xFC
    
    # Pass 2: Error Diffusion
    pass2 = np.zeros_like(pixels, dtype=float)
    err = np.zeros((h + 1, w + 1, 3), dtype=float)
    
    for y in range(h):
        for x in range(w):
            for c in range(3):
                p_in = pass1[y, x, c]
                val = p_in + err[y, x, c]
                
                if p_in >= 16: # Bypass
                    out = p_in
                else:
                    out = np.clip(val, 0, 255)
                    out = int(out) & 0xF0
                
                pass2[y, x, c] = out
                e = val - out
                
                # FS Diffusion
                if x + 1 < w: err[y, x+1, c] += e * 7/16
                if y + 1 < h:
                    if x > 0: err[y+1, x-1, c] += e * 3/16
                    err[y+1, x, c] += e * 5/16
                    if x + 1 < w: err[y+1, x+1, c] += e * 1/16
                    
    return trunc_img, pass2.astype(np.uint8)

def main():
    try:
        orig = Image.open('dog.png').convert('RGB')
        orig = orig.resize((960, 540), Image.Resampling.LANCZOS)
    except:
        print("dog.png not found")
        return

    orig_np = np.array(orig)
    trunc_np, dither_np = simulate_pipeline(orig)
    
    print("--- Metric Comparison (Lower Grayscale Focus) ---")
    
    # Calculate for whole image
    if HAS_SKIMAGE:
        psnr_trunc = psnr_func(orig_np, trunc_np)
        psnr_dither = psnr_func(orig_np, dither_np)
        # SSIM calculation (multichannel=True is deprecated in some versions, use channel_axis)
        try:
            ssim_trunc = ssim_func(orig_np, trunc_np, channel_axis=2)
            ssim_dither = ssim_func(orig_np, dither_np, channel_axis=2)
        except TypeError:
            ssim_trunc = ssim_func(orig_np, trunc_np, multichannel=True)
            ssim_dither = ssim_func(orig_np, dither_np, multichannel=True)
    else:
        psnr_trunc = calculate_psnr_manual(orig_np, trunc_np)
        psnr_dither = calculate_psnr_manual(orig_np, dither_np)
        ssim_trunc = 0.0 # Placeholder
        ssim_dither = 0.0 # Placeholder

    print(f"Whole Image PSNR (Truncation): {psnr_trunc:.2f} dB")
    print(f"Whole Image PSNR (2-Stage Dither): {psnr_dither:.2f} dB")
    if HAS_SKIMAGE:
        print(f"Whole Image SSIM (Truncation): {ssim_trunc:.4f}")
        print(f"Whole Image SSIM (2-Stage Dither): {ssim_dither:.4f}")

    # Calculate for Ultra-Low Grayscale pixels ( < 32 )
    mask = np.max(orig_np, axis=2) < 32
    if np.any(mask):
        low_orig = orig_np[mask]
        low_trunc = trunc_np[mask]
        low_dither = dither_np[mask]
        
        mse_low_trunc = np.mean((low_orig.astype(float) - low_trunc.astype(float))**2)
        mse_low_dither = np.mean((low_orig.astype(float) - low_dither.astype(float))**2)
        
        psnr_low_trunc = 20 * math.log10(255.0 / math.sqrt(mse_low_trunc)) if mse_low_trunc > 0 else 100
        psnr_low_dither = 20 * math.log10(255.0 / math.sqrt(mse_low_dither)) if mse_low_dither > 0 else 100
        
        print(f"\nNear-Black Region PSNR (Truncation): {psnr_low_trunc:.2f} dB")
        print(f"Near-Black Region PSNR (2-Stage Dither): {psnr_low_dither:.2f} dB")

if __name__ == "__main__":
    main()
