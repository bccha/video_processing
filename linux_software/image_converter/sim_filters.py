import numpy as np
from PIL import Image
import os

WIDTH = 960
HEIGHT = 540

# Load raw image (32-bit BGRA -> shape: 540, 960, 4)
raw_data = np.fromfile('image.raw', dtype=np.uint8).reshape((HEIGHT, WIDTH, 4))
# Extract BGR -> RGB (for Pillow to save correctly)
bgr = raw_data[:, :, :3]
rgb = np.stack([bgr[:,:,2], bgr[:,:,1], bgr[:,:,0]], axis=-1)

# Custom RTL Grayscale: (R >> 2) + (G >> 1) + (B >> 2)
gray = (rgb[:,:,0] >> 2) + (rgb[:,:,1] >> 1) + (rgb[:,:,2] >> 2)
gray = np.clip(gray, 0, 255).astype(np.uint8)

Image.fromarray(gray, mode='L').save('out_1_grayscale.jpg')

# Pad images for 3x3 window processing
rgb_pad = np.pad(rgb, ((1,1), (1,1), (0,0)), mode='edge').astype(np.int32)
gray_pad = np.pad(gray, ((1,1), (1,1)), mode='edge').astype(np.int32)

blur = np.zeros_like(rgb, dtype=np.int32)
edge_mag = np.zeros_like(gray, dtype=np.int32)
emboss = np.zeros_like(gray, dtype=np.int32)
sharp = np.zeros_like(rgb, dtype=np.int32)

print("Processing filters...")

# Using numpy stride tricks or simple slicing for 3x3 windows
for i in range(3):
    for j in range(3):
        # Blur: sum all 9 pixels
        blur += rgb_pad[i:i+HEIGHT, j:j+WIDTH, :]

# Blur divisor: (sum * 28) >> 8
blur = (blur * 28) >> 8
blur = np.clip(blur, 0, 255).astype(np.uint8)
Image.fromarray(blur, mode='RGB').save('out_2_blur.jpg')

# Edge (Sobel) on Grayscale
# Gx = [-1 0 1; -2 0 2; -1 0 1]
gx =  -1 * gray_pad[0:HEIGHT, 0:WIDTH]   + 1 * gray_pad[0:HEIGHT, 2:WIDTH+2] \
      -2 * gray_pad[1:HEIGHT+1, 0:WIDTH] + 2 * gray_pad[1:HEIGHT+1, 2:WIDTH+2] \
      -1 * gray_pad[2:HEIGHT+2, 0:WIDTH] + 1 * gray_pad[2:HEIGHT+2, 2:WIDTH+2]

# Gy = [1 2 1; 0 0 0; -1 -2 -1]
gy =   1 * gray_pad[0:HEIGHT, 0:WIDTH]   + 2 * gray_pad[0:HEIGHT, 1:WIDTH+1]   + 1 * gray_pad[0:HEIGHT, 2:WIDTH+2] \
      -1 * gray_pad[2:HEIGHT+2, 0:WIDTH] - 2 * gray_pad[2:HEIGHT+2, 1:WIDTH+1] - 1 * gray_pad[2:HEIGHT+2, 2:WIDTH+2]

edge_mag = np.abs(gx) + np.abs(gy)
edge_mag_clipped = np.clip(edge_mag, 0, 255).astype(np.uint8)
Image.fromarray(edge_mag_clipped, mode='L').save('out_3_edge_gray.jpg')

# Color Edge (Threshold > 40)
mask = edge_mag > 40
color_edge = np.zeros_like(rgb)
color_edge[mask] = rgb[mask]
Image.fromarray(color_edge, mode='RGB').save('out_4_edge_color.jpg')

# Emboss on Grayscale
# [-2 -1  0]
# [-1  1  1]
# [ 0  1  2]
emboss_sum = -2 * gray_pad[0:HEIGHT, 0:WIDTH]   - 1 * gray_pad[0:HEIGHT, 1:WIDTH+1] \
             -1 * gray_pad[1:HEIGHT+1, 0:WIDTH] + 1 * gray_pad[1:HEIGHT+1, 1:WIDTH+1] + 1 * gray_pad[1:HEIGHT+1, 2:WIDTH+2] \
                                                + 1 * gray_pad[2:HEIGHT+2, 1:WIDTH+1] + 2 * gray_pad[2:HEIGHT+2, 2:WIDTH+2]

emboss = np.clip(emboss_sum + 128, 0, 255).astype(np.uint8)
Image.fromarray(emboss, mode='L').save('out_5_emboss.jpg')

# Sharpen on RGB
# [ 0 -1  0]
# [-1  5 -1]
# [ 0 -1  0]
for c in range(3):
    channel_pad = rgb_pad[:,:,c]
    sharp_c = 5 * channel_pad[1:HEIGHT+1, 1:WIDTH+1] \
              - channel_pad[0:HEIGHT, 1:WIDTH+1] \
              - channel_pad[2:HEIGHT+2, 1:WIDTH+1] \
              - channel_pad[1:HEIGHT+1, 0:WIDTH] \
              - channel_pad[1:HEIGHT+1, 2:WIDTH+2]
    sharp[:,:,c] = np.clip(sharp_c, 0, 255)

sharp = sharp.astype(np.uint8)
Image.fromarray(sharp, mode='RGB').save('out_6_sharpen.jpg')

print("Done! Check out_*.jpg files.")
