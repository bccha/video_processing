import numpy as np
import cv2

# 해상도 설정 (540p)
WIDTH = 960
HEIGHT = 540

print("Generating Top-to-Bottom Gradient for Split-Screen Demo...")

# 1. 위(0, 검은색)에서 아래(255, 흰색)로 이어지는 1차원 세로 배열 생성
# 높이(HEIGHT)에 맞춰 540개의 밝기 단계를 만듭니다.
gradient_col = np.linspace(0, 0x20, HEIGHT, dtype=np.uint8).reshape(-1, 1)

# 2. 이 1차원 세로 배열을 가로 길이(WIDTH, 960)만큼 쫙 복사해서 2D 이미지로 확장!
img_vertical_grad = np.repeat(gradient_col, WIDTH, axis=1)

# 3. 흑백(Grayscale)을 RGB 3채널로 변환
img_vertical_grad_rgb = cv2.cvtColor(img_vertical_grad, cv2.COLOR_GRAY2BGR)

# 4. 이미지 저장
cv2.imwrite("demo_top_to_bottom.png", img_vertical_grad_rgb)
print("Saved as 'demo_top_to_bottom.png'!")
