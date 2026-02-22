# Display Calibration: 3x3 Gamut Matrix & Color Science
[⬅️ Back to README](../README.md)

이 문서는 디스플레이의 색역(Color Gamut) 조정과 화질 캘리브레이션을 위한 **3x3 Transformation Matrix**의 이론적 배경, 수학적 도출 방법, 그리고 FPGA 하드웨어 구현 시의 핵심 설계 포인트를 정리합니다.

---

## 1. 🧮 3x3 Matrix 연산의 이해

디스플레이 파이프라인에서 3x3 행렬은 입력 픽셀 $(R_{in}, G_{in}, B_{in})$을 패널의 물리적 특성이나 목표 색역에 맞는 출력 픽셀 $(R_{out}, G_{out}, B_{out})$으로 변환하는 역할을 합니다.

### 수식
$$
\begin{bmatrix} R_{out} \\ G_{out} \\ B_{out} \end{bmatrix} = 
\begin{bmatrix} c_{00} & c_{01} & c_{02} \\ c_{10} & c_{11} & c_{12} \\ c_{20} & c_{21} & c_{22} \end{bmatrix}
\begin{bmatrix} R_{in} \\ G_{in} \\ B_{in} \end{bmatrix}
$$

* **하드웨어 자원:** 채널당 3개씩, 총 **9개의 곱셈기(Multiplier)**와 **6개의 덧셈기(Adder)**가 필요합니다. Cyclone V의 DSP 블록을 활용하여 효율적으로 구현 가능합니다.

---

## 2. 🏗️ 하드웨어(RTL) 설계 가이드

단순한 수식과 달리, 실시간 비디오 스트리밍 환경에서 안정적인 동작을 위해서는 다음의 설계 포인트를 준수해야 합니다.

### ① 음수 및 고정소수점 처리 (Signed Fixed-Point)
* 색역 변환 계수($c_{ij}$)에는 반드시 **음수**가 포함됩니다.
* 하드웨어는 부동소수점(Float) 연산이 무겁기 때문에 **고정소수점(Fixed-point)** 형식을 사용합니다.
* **추천 사양:** 12-bit 계수 (Sign 1-bit + Integer 2-bit + Fractional 9-bit).
* Verilog 구현 시 `$signed()` 시스템 함수를 사용하여 부호 있는 연산을 정확히 수행해야 합니다.

### ② 3단계 파이프라이닝 (Pipeline)
타이밍 마진(Fmax)을 확보하기 위해 연산 과정을 최소 3단계로 분리합니다.
1. **Stage 1 (Mul):** 입력 데이터와 계수의 곱셈 수행.
2. **Stage 2 (Adder Tree):** 곱셈 결과들을 합산하여 R, G, B 각각의 결과 도출.
3. **Stage 3 (Saturation):** 최종 결과의 비트 폭 조정 및 클리핑 처리.

### ③ 클리핑 및 포화 (Saturation / Clamping)
* 연산 결과가 `0`보다 작아지는 언더플로우나 `255`를 초과하는 오버플로우가 발생할 수 있습니다.
* **Clipping 로직:** 결과가 음수면 `0`, 255를 넘으면 `255`로 강제로 눌러주어 화면에 노이즈(Overflow noise)가 튀는 것을 방지합니다.

---

## 3. 🧪 컬러 사이언스: 계수 도출 방법

"계측기로 측정한 패널 값과 타겟을 가지고 어떻게 3x3 계수를 뽑아내는가?"에 대한 해답은 선형 대수학의 **역행렬(Inverse Matrix)**에 있습니다.

### Step 1. xyY to XYZ 변환
계측기(예: CA-410)로 측정한 **Red, Green, Blue, White**의 색좌표($x, y$)와 밝기($Y$)를 절대 색공간인 **CIE XYZ**로 변환합니다.
* $X = \frac{x}{y} \cdot Y$
* $Y = Y$
* $Z = \frac{1-x-y}{y} \cdot Y$

### Step 2. RGB to XYZ 변환 행렬 구성
Target(목표 색역)과 Native(실제 패널) 각각에 대해 XYZ 좌표를 열(Column)로 하는 3x3 행렬을 만듭니다.
1. **$M_{Target}$:** sRGB 등 표준 스펙의 XYZ 행렬.
2. **$M_{Native}$:** 실제 패널의 계측 데이터를 기반으로 만든 XYZ 행렬.

### Step 3. 각각의 'RGB to XYZ' 변환 행렬 도출
Target(목표 색역)과 Native(실제 패널) 각각에 대해 RGB 입력값(0~1 스케일)을 절대 색공간 XYZ로 변환해 주는 $3 \times 3$ 행렬($M_{Target}$과 $M_{Native}$)을 생성합니다.

#### 🔍 행렬 내부의 진짜 의미 (세로줄로 읽기)
이 3x3 행렬의 안을 들여다보면 9개의 숫자가 있는데, 이 숫자들은 사실 아주 직관적입니다. 각 스펙의 순색(Primary Color)이 가진 절대 색상(XYZ) 좌표들을 세로줄(Column) 방향으로 모아놓은 것입니다.

`M_Target` 행렬(예: sRGB)을 예로 들어 쪼개서 보겠습니다.
* **1열 (Column 1):** Target 스펙의 **순색 Red**(R=1, G=0, B=0)를 켰을 때 나와야 하는 절대 X, Y, Z 값입니다.
* **2열 (Column 2):** Target 스펙의 **순색 Green**(R=0, G=1, B=0)을 켰을 때의 X, Y, Z 값입니다.
* **3열 (Column 3):** Target 스펙의 **순색 Blue**(R=0, G=0, B=1)를 켰을 때의 X, Y, Z 값입니다.

#### 💡 왜 이렇게 생겼을까? (행렬 곱셈의 마법)
만약 비디오 신호로 **100% 순수한 Red 픽셀 `[1, 0, 0]`**이 들어왔다고 가정해 보겠습니다. 이 신호를 `M_Target` 행렬에 곱하면 어떻게 될까요?
보시다시피 Green과 Blue 위치에는 `0`이 곱해져서 연산에서 날아가고, 정확히 **Target Red의 XYZ 값(1열의 데이터)만 쏙 튀어나오게** 됩니다! 반대로 노란색 픽셀 `[1, 1, 0]`이 들어오면, Red의 XYZ와 Green의 XYZ가 더해져서 노란색의 절대 좌표가 계산되는 방식입니다.

#### 📏 요약하자면:
1. **Target의 XYZ (벡터):** 화면의 어떤 "특정 픽셀 하나"가 내는 절대 색상 값 (예: `[X, Y, Z]`)
2. **M_Target (행렬):** 어떤 RGB 값이 들어오더라도, 그것을 그 스펙(예: sRGB)에 맞는 기댓값 XYZ로 **변환해 주는 공식(룰북)**
3. **M_Native (행렬):** 우리 패널에 빨강, 초록, 파랑 화면을 띄워놓고 계측기(CA-410 등)로 직접 찍어서 얻은 X, Y, Z 값들을 세로로 예쁘게 세워둔 **고유의 공식**

1. **Target 행렬 수식:** $M_{Target} \times [RGB_{Target}] = [XYZ]$
2. **Native 행렬 수식:** $M_{Native} \times [RGB_{Native}] = [XYZ]$

우리 캘리브레이션의 궁극적인 목표는 **"Target RGB 신호가 들어왔을 때, 패널에 어떤 Native RGB 신호를 쏴줘야 똑같은 XYZ(절대 색)가 나올까?"** 입니다. 즉, 위 두 식의 $[XYZ]$ 결과값이 같아지도록 만들어야 합니다.

### Step 4. 마법의 변환 행렬 (Hardware Matrix) 도출!
이제 두 식을 결합하여 하드웨어 파이프라인에 탑재할 최종 변환 행렬을 도출합니다.

1. **두 수식의 결합:** Target과 Native가 같은 XYZ 값을 가져야 하므로 아래와 같이 정리할 수 있습니다.
   $$M_{Native} \times [RGB_{Native}] = M_{Target} \times [RGB_{Target}]$$
   > 💡 **이 수식의 직관적 의미:**
   > "우리가 목표로 하는 이상적인 색상($M_{Target} \times [RGB_{Target}]$)을 만들어내려면, 우리 패널($M_{Native}$)에는 도대체 **어떤 조작된 RGB 값($[RGB_{Native}]$)**을 입력으로 넣어줘야 할까?"
   
2. **$RGB_{Native}$에 대한 정리:** 하드웨어 파이프라인에서 우리가 최종적으로 알고 싶은 것은 "입력된 Target RGB를 **어떤 패널 RGB($RGB_{Native}$)**로 바꾸어 출력할 것인가?" 입니다.
   따라서 식을 $[RGB_{Native}]$ 단위로 묶기 위해, 양변의 앞부분에 $M_{Native}$의 **역행렬($M_{Native}^{-1}$)**을 곱해줍니다.

   $$M_{Native}^{-1} \times M_{Native} \times [RGB_{Native}] = M_{Native}^{-1} \times M_{Target} \times [RGB_{Target}]$$

3. **최종 도출:** 행렬 역행렬의 곱($M_{Native}^{-1} \times M_{Native}$)은 단위 행렬이 되어 사라지므로, 최종 수식은 다음과 같이 완성됩니다.
   $$[RGB_{Native}] = (M_{Native}^{-1} \times M_{Target}) \times [RGB_{Target}]$$

결론적으로 저 **$(M_{Native}^{-1} \times M_{Target})$**을 계산해서 나온 $3 \times 3$ 행렬이 **Verilog RTL 코드의 곱셈기에 집어넣어야 할 9개의 최종 계수(Coefficients)**가 됩니다.

---

## 4. ⚠️ 치명적인 함정: Gamma Linearity

3x3 행렬 연산은 물리적인 빛의 양이 **선형(Linear)일 때만 성립**합니다. 하지만 일반적인 8-bit RGB 비디오 신호는 감마가 적용된 '비선형' 데이터입니다. 따라서 다음과 같은 파이프라인 아키텍처가 필수적입니다.

> **[ 입력 RGB ]** ➡️ **De-Gamma (1D LUT)** ➡️ **🌟 3x3 Matrix** ➡️ **Gamma (1D LUT)** ➡️ **[ 출력 ]**

감마를 풀지 않고 행렬 연산을 적용하면 색상의 왜곡이 발생하여 정확한 캘리브레이션이 불가능합니다.

---

## 5. 🚨 디스플레이의 배신: 왜 White-Preserving Matrix가 필요한가?

이론적인 '빛의 가산 혼합(Additive Color Mixing)'에서는 **"Max R + Max G + Max B = Full White"**가 되는 것이 맞습니다. 하지만 실제 패널(MicroLED, OLED 등)에서는 이 공식이 깨집니다. 세 가지 색상을 동시에 켤 때 다음과 같은 물리적 제약이 발생합니다:
1. **IR Drop (전압 강하):** 세 개의 LED가 동시에 켜지면 전류를 3배로 사용하여 내부 저항에 의해 전압이 떨어지고 밝기(Luminance)가 감소합니다.
2. **광학적/전기적 크로스토크 (Crosstalk):** 인접 픽셀로 빛이 새거나 미세 전류가 흘러 색 순도가 탁해집니다.
3. **ABL (Auto Brightness Limiter):** 발열과 전력 소모를 막기 위해 패널 제어 IC가 강제로 밝기를 낮춥니다.

### 🎯 실무의 핵심: "White 밸런스가 모든 색을 지배한다"
위와 같은 이유로 단순 역행렬을 적용하면 빨강, 초록, 파랑은 정확할지라도 **가장 중요한 하얀색 화면(D65)의 색 온도가 누렇게 뜨거나 퍼렇게 질리게 됩니다.** 사람의 눈은 다른 색상보다 무채색(White, Gray)의 색상 틀어짐(Color Shift)에 수십 배 훨씬 예민합니다. 
이를 해결하기 위해 3x3 매트릭스를 도출할 때 '단순 역행렬'을 쓰지 않고 **White를 완벽한 타겟(D65)에 강제로 맞추는 스케일링(Scaling)** 작업을 수학적으로 한 번 더 거쳐야 합니다.

### 🎛️ 3x3 매트릭스 계수의 숨겨진 2가지 역할
화이트 밸런스까지 고려하여 도출된 9개의 계수들은 입력된 데이터에 단순히 색을 섞는 것을 넘어, **각 채널의 '마스터 볼륨(밝기 출력 비율)'을 강제로 억눌러 밸런스를 조절하는 역할**을 수행합니다.
1. **대각선 성분 (C00, C11, C22): "자기 자신의 밝기 볼륨 조절"**
   * 예: `C22 (Blue 계수) = 0.706` ➡️ "이 패널은 파란색이 너무 밝으니까 무조건 원래 밝기의 70%까지만 켜지게 깎아라!"
2. **비대각선 성분 (C01, C02 등): "다른 색상 스며들기 (Color Steering)"**
   * 예: `C01 (Red 출력에 Green을 곱하는 값) = 0.192` ➡️ "초록색을 켤 때 패널 고유의 초록색이 누렇다면, 빨간색 LED를 **19% 정도 몰래 같이 켜서** 예쁜 초록색으로 틀어주어라!"

이렇게 뽑아낸 매트릭스가 바로 하드웨어로 무너진 패널의 밸런스를 멱살 잡고 끌어올리는 만능 화질 치트키, **White-Preserving Matrix**입니다.

---

## 6. 🐍 실무용 White-Preserving 계수 추출기 (Python 스크립트)

아래 스크립트는 RGB의 고유 색좌표(x, y)와 Target White Point 스펙을 입력받아, 타겟 White 밸런스에 정확히 일치하도록 각 채널의 가중치($S$)를 계산하여 곱해주는 **하드웨어용 12비트 계수 추출기**입니다.

```python
import numpy as np

def get_rgb_to_xyz_matrix(xr, yr, xg, yg, xb, yb, xw, yw):
    # 1. 색좌표(x, y)를 XYZ 비율로 변환
    Xr, Yr, Zr = xr/yr, 1.0, (1 - xr - yr)/yr
    Xg, Yg, Zg = xg/yg, 1.0, (1 - xg - yg)/yg
    Xb, Yb, Zb = xb/yb, 1.0, (1 - xb - yb)/yb
    
    # 2. 타겟 White Point의 XYZ 변환
    Xw, Yw, Zw = xw/yw, 1.0, (1 - xw - yw)/yw
    
    # 3. Primary Matrix (P) 생성
    P = np.array([
        [Xr, Xg, Xb],
        [Yr, Yg, Yb],
        [Zr, Zg, Zb]
    ])
    W = np.array([Xw, Yw, Zw])
    
    # 4. 마법의 가중치 S 계산 (White를 맞추기 위한 각 RGB의 밝기 스케일)
    S = np.linalg.inv(P).dot(W)
    
    # 5. 최종 White-Preserving 행렬 완성! (P의 각 열에 S를 곱함)
    M = P * S 
    return M

# --- 1. 스펙 타겟 (sRGB, D65 White) ---
M_target = get_rgb_to_xyz_matrix(
    0.640, 0.330,  # sRGB Red
    0.300, 0.600,  # sRGB Green
    0.150, 0.060,  # sRGB Blue
    0.3127, 0.3290 # D65 White (따뜻한 흰색)
)

# --- 2. 우리 패널 계측값 (가상: 광색역이지만 파란빛이 도는 패널) ---
M_native = get_rgb_to_xyz_matrix(
    0.680, 0.310,  # Native Red (sRGB보다 넓음)
    0.260, 0.650,  # Native Green (sRGB보다 넓음)
    0.140, 0.050,  # Native Blue (더 깊음)
    0.280, 0.290   # Native White (파란빛이 돔, 예: 9300K)
)

# --- 3. 하드웨어 변환 행렬 도출 (Native 역행렬 * Target) ---
M_hw_float = np.linalg.inv(M_native).dot(M_target)

# --- 4. Verilog용 12-bit 고정 소수점 변환 (x1024) ---
# 12-bit Signed: 부호 1비트 + 정수 1비트 + 소수 10비트
M_hw_fixed = np.round(M_hw_float * 1024).astype(int)

print("--- 하드웨어 파라미터 9개 (12-bit) ---")
print(M_hw_fixed)

# --- 5. White 테스트: 입력으로 sRGB White [1, 1, 1]을 넣는다면? ---
white_output = M_hw_float.dot(np.array([1.0, 1.0, 1.0]))
print("\n--- sRGB White 입력 시 패널 출력 밝기 ---")
print(np.round(white_output, 4))
```

**[ 하드웨어 파라미터 (12-bit Fixed) 출력 결과 ]**
```text
[[ 923,  192,   31]
 [  44,  970,   11]
 [   7,   18,  706]]
```

*(💡 파란빛이 강한 패널 특성에 맞춰 대각선 성분인 Blue 출력을 `706/1024 (약 70%)` 수준으로 깎고, Red 출력을 `923` 및 인접 크로스토크 게인으로 끌어올려 완벽한 D65 White를 달성하는 원리입니다!)*

---

## 7. 🛠️ 하드웨어 파이프라인 설계 (Verilog RTL)

위에서 도출한 9개의 12-bit 계수(`C00` ~ `C22`)를 파라미터로 적용한 3단계 파이프라인 연산기입니다. 높은 동작 주파수(Fmax)를 달성하기 위해 **곱셈 ➡️ 덧셈 ➡️ 클리핑** 과정을 클럭별로 분리하여 설계합니다. 

소프트웨어(GPU/OS) 단에서 이 처리를 하면 8비트 계조가 손실되어 밴딩(Banding) 현상이 발생하지만, 이 코드는 DSP 블록을 병렬로 활용하여 **원본 데이터 손실 없이 실시간으로 12-bit 정밀도의 색역(Gamut) 튜닝과 백색점(White Balance) 튜닝을 동시 수행**합니다.

```verilog
module color_matrix_3x3 (
    input  wire        clk,
    input  wire        rst_n,
    // Input Video (8-bit)
    input  wire [7:0]  r_in,
    input  wire [7:0]  g_in,
    input  wire [7:0]  b_in,
    input  wire        valid_in,
    // Output Video (8-bit)
    output reg  [7:0]  r_out,
    output reg  [7:0]  g_out,
    output reg  [7:0]  b_out,
    output reg         valid_out
);

    // Python에서 뽑은 12-bit Signed 계수 하드코딩
    // (위 파이썬 스크립트에서 추출된 화이트 밸런싱 계수 적용)
    localparam signed [11:0] C00 = 12'sd923;  localparam signed [11:0] C01 = 12'sd192; localparam signed [11:0] C02 = 12'sd31;
    localparam signed [11:0] C10 =  12'sd44;  localparam signed [11:0] C11 = 12'sd970; localparam signed [11:0] C12 = 12'sd11;
    localparam signed [11:0] C20 =   12'sd7;  localparam signed [11:0] C21 =  12'sd18; localparam signed [11:0] C22 = 12'sd706;

    // --- [Stage 1] Multipliers (8-bit Unsigned * 12-bit Signed = 21-bit Signed) ---
    reg signed [20:0] mult_r0, mult_r1, mult_r2;
    reg signed [20:0] mult_g0, mult_g1, mult_g2;
    // ... b 채널 및 valid 신호용 파이프라인 레지스터 생략 ...

    always @(posedge clk) begin
        // r_in에 0을 붙여 부호 없는 양수임을 명시적으로 알린 후 signed 연산 수행
        mult_r0 <= $signed({1'b0, r_in}) * C00;
        mult_r1 <= $signed({1'b0, g_in}) * C01;
        mult_r2 <= $signed({1'b0, b_in}) * C02;
        // G, B 채널 곱셈도 동일하게 수행...
    end

    // --- [Stage 2] Adder Tree (21-bit + 21-bit + 21-bit = 23-bit Signed) ---
    reg signed [22:0] sum_r, sum_g, sum_b;

    always @(posedge clk) begin
        sum_r <= mult_r0 + mult_r1 + mult_r2;
        // G, B 채널 덧셈도 동일하게 수행...
    end

    // --- [Stage 3] Shift & Clipping / Saturation ---
    // 소수점 10자리를 곱했으므로(x1024), 덧셈 결과를 다시 10비트 우측 산술 시프트(>>> 10) 해줍니다.
    wire signed [12:0] final_r = sum_r >>> 10; 

    always @(posedge clk) begin
        // Underflow (결과가 음수면 0으로 클램핑)
        if (final_r < 0) 
            r_out <= 8'd0;
        // Overflow (결과가 255 이상이면 255로 클램핑)
        else if (final_r > 255) 
            r_out <= 8'd255;
        // 정상 범위
        else 
            r_out <= final_r[7:0]; 
            
        // G, B 채널 클리핑 동일하게 수행...
    end

endmodule
```

---
> [!TIP]
> 이제 시뮬레이션 툴(ModelSim 등)을 활용해 위 3x3 매트릭스 모듈에 파형을 인가하여 검증해 볼 수 있습니다. 계조 손실이 발생하지 않는 가장 기초적이고 완벽한 색공간 변환 파이프라인입니다.

---

## 8. 🗺️ 궁극의 하드웨어 파이프라인과 역순 캘리브레이션

디스플레이 화질 처리(Color/Gamma Pipeline)의 대원칙은 다음과 같습니다.
**"모든 복잡한 색상 연산(3x3 Matrix, Demura)은 반드시 물리적인 빛의 양과 비례하는 '선형(Linear) 공간'에서 이루어져야 한다."**

이 설계 사상에 따라, 완벽한 디스플레이 하드웨어 데이터 파이프라인은 다음과 같은 순서로 칩 내부를 흘러갑니다.

### 🌊 하드웨어 데이터 파이프라인 (정방향: Input ➡️ Output)

1. **[ De-Gamma (1D LUT) ]:** 외부에서 들어오는 비선형(Gamma 2.2) 영상 신호를 하드웨어 연산용 **16-bit 선형 데이터(Linear Light)**로 쫙 펴줍니다.
2. **[ 🌟 3x3 Matrix (Color Gamut) ]:** 선형 공간에서 완벽하게 타겟 색역과 White Balance를 비틀어줍니다. (우리가 앞서 도출한 계수 적용)
3. **[ Pixel Cal / Demura ]:** 화면의 공간적인 얼룩(Mura)을 보상합니다.
4. **[ Panel Gamma (1D LUT) ]:** 연산이 다 끝난 선형 데이터를 **'우리 패널의 실제 물리적 S자 커브 특성에 맞게'** 다시 구부려줍니다 (매핑).
5. **[ Bit-Split Dithering ]:** 고정밀 16-bit 데이터를 패널 드라이버 IC가 먹을 수 있는 8-bit나 10-bit로 시각적 형열 없이 깎아냅니다.
6. **[ 디스플레이 패널 출력 ]**

### ⏪ 양산 캘리브레이션 순서 (역방향: Output ➡️ Input)

데이터는 칩 안에서 앞에서 뒤로 흐르지만, **공장에서 계측기를 대고 패널의 영점을 잡는 캘리브레이션 시퀀스는 반드시 '뒤에서 앞으로' 가야만 합니다.** 뒷단의 물리적 특성이 평탄화되지 않으면, 앞단의 수학적 계산 결과가 실제 빛으로 어떻게 나올지 짐작할 수 없기 때문입니다.

1. **Step 1. 물리적 계조 펴기 (Panel Gamma 1D LUT):** 파이프라인 맨 뒷단. 패널에 16-bit 값을 밀어 넣으며 밝기를 찍어 제멋대로인 물리적 곡선을 '선형(Linear) 16-bit'로 펴주는 맵핑 테이블을 만듭니다.
2. **Step 2. 공간 얼룩 지우기 (Demura):** 파이프라인 중간. 패널이 정직하게 빛을 내기 시작하면, 고해상도 카메라로 화면 전체를 찍어 픽셀별 밝기 편차(얼룩)를 지우는 보상 맵을 만듭니다.
3. **Step 3. 글로벌 색상 튜닝 (3x3 Matrix):** 파이프라인 앞단. 얼룩 하나 없이 완벽하게 평평해진 도화지 상태에서 중앙의 RGBW를 한 번만 찍고, 앞서 파이썬으로 짰던 **White-Preserving Matrix 9개 계수**를 단번에 뽑아냅니다.

### 💡 디스플레이 양산의 현실: "Golden Gamma"와 "PWL 하드웨어"

공장 라인에서 1초가 아쉬운 판에, 쏟아져 나오는 모든 패널에 계측기를 대고 16-bit 감마 커브(65,536개의 포인트)를 일일이 찍고 있는 것은 물리적으로 불가능합니다. 하지만 아키텍트는 이를 '골든 샘플 전략'과 '하드웨어 보간(Interpolation)'으로 극복합니다.

#### 🏆 1. 골든 샘플(Golden Sample) 전략
실무에서는 수백, 수천 대의 패널 중 가장 특성이 평균에 가까운 A급 패널 몇 개를 골라 연구소에서 정밀하게 계측합니다. 여기서 뽑아낸 완벽한 1D Gamma LUT 데이터를 **'골든 감마 테이블(Golden Gamma Table)'**이라고 부릅니다.
* **양산 적용:** 이 골든 테이블을 하나 구워서 같은 모델로 생산되는 모든 칩의 플래시 메모리에 똑같이 복사(Burn-in)해 버립니다.
* **오차 해결:** 패널마다 약간씩 달라지는 물리적 편차는, 앞서 설명한 **Step 2. Demura (얼룩 보상)** 단계가 픽셀 단위로 미세하게 더하고 빼면서 평균 감마 테이블의 빈틈을 완벽하게 하드캐리하여 메워버립니다.

#### 🧠 2. 아키텍트의 설계 포인트: PWL (구간 선형 보간법)
만약 16-bit 해상도의 감마 테이블 데이터를 전부 칩 내부 SRAM(BRAM)에 올리려 한다면, `65,536개 × 16-bit × RGB 3채널 = 약 3 Megabit`의 막대한 메모리가 필요합니다. FPGA나 디스플레이 드라이버 IC(DDI)에서 이는 치명적인 낭비입니다.

이를 해결하기 위해 실무 하드웨어는 **'구간 선형 보간법 (Piecewise Linear Interpolation, PWL)'**을 사용합니다.
1. **포인트 샘플링 (Knot Points):** 16-bit 전체 계조 중에서 커브가 심하게 휘는 어두운 곳은 촘촘하게, 밝고 완만한 곳은 듬성듬성하게 핵심 포인트(예: 33개, 65개, 257개)만 뽑아서 BRAM에 저장합니다. 메모리 사용량이 $\frac{1}{1000}$ 수준으로 급감합니다!
2. **실시간 하드웨어 보간 (Interpolation):** 비디오 픽셀 데이터가 들어오면, 이 값이 BRAM에 저장된 어느 두 포인트(Knot) 사이에 있는지 빠르게 찾습니다. 그리고 하드웨어 곱셈기(DSP)를 써서 현재 픽셀 값의 위치만큼 **두 점 사이를 직선(기울기)으로 이어버리는 계산**을 매 클럭마다 실시간으로 수행합니다.

> 이 PWL 방식 덕분에 디스플레이 칩은 극한으로 메모리를 아끼면서도, 수만 개의 계조(16-bit)를 꽉 채워 넣은 것과 99.9% 동일한 무손실의 부드러운 감마 화질을 뿜어낼 수 있습니다.

---

## 9. ✂️ 하드웨어 방어막: 음수(Negative) 처리와 클리핑(Clipping)

3x3 Matrix 연산을 하다 보면 십중팔구 **RGB 값이 0보다 작아지는(음수)** 상황이 발생합니다. 이때 하드웨어에서 아무 조치도 취하지 않으면 화면에 형광색 노이즈가 튀는 끔찍한 현상이 발생합니다.

### 🌑 1. 음수 RGB는 왜 발생하는가? (Out of Gamut)
3x3 Matrix의 비대각선 성분(예: Red 출력에 Green 데이터를 곱하는 $C_{01}$)에는 보통 **음수(-)** 계수가 들어갑니다. 패널 고유의 빛샘(Crosstalk)이나 불순한 색을 '빼서(Subtract)' 순도를 높이기 위함입니다. 
하지만 입력 비디오 신호로 이미 엄청나게 진한 빨간색이 들어왔을 때, 이를 타겟에 맞추기 위해 연산하다 보면 수학적으로 **"Green과 Blue를 마이너스 밝기로 켜라!"** 라는 기괴한 수식이 도출될 수 있습니다. 
물리적인 현실 세계에 '마이너스 빛(Negative Light)'을 내는 LED는 존재하지 않으므로 아키텍처적인 방어막이 필요합니다.

### ✂️ 2. 하드웨어 해결책: "무자비한 클리핑(Clipping / Saturation)"
음수가 나오거나 255를 넘어서는 오버플로우가 발생했을 때, 하드웨어는 이를 정상 범위인 `0 ~ 255` 안으로 강제로 우겨넣어야 합니다. 이를 **클리핑(Clipping) 또는 새츄레이션(Saturation)**이라고 부릅니다.
Verilog 연산기에서 덧셈이 끝난 직후, 이 '클리핑 가드 로직'을 방패처럼 세워두어야 합니다.

### 💻 3. Verilog 구현: "부호 비트(Sign Bit) 검사"
Verilog에서는 2의 보수(2's Complement)의 **최상위 비트(MSB, Sign Bit)가 `1`인지**만 검사하면 음수 판별이 끝납니다.

*(소수점을 없애기 위해 10번 우측 시프트한 직후의 13-bit 데이터를 `final_r`이라고 가정합니다.)*

```verilog
wire signed [12:0] final_r; // 3x3 연산이 끝난 13-bit 결과값

always @(posedge clk) begin
    // [클리핑 로직: 3단 콤보 MUX]
    if (final_r[12] == 1'b1) begin
        // 1. Underflow (음수 발생): MSB(12번 비트)가 1이면 가차 없이 0으로 깎아냅니다.
        r_out <= 8'd0;
    end else if (final_r > 13'sd255) begin
        // 2. Overflow (최대치 초과): 양수이지만 255보다 크면 255로 꽉 눌러줍니다.
        r_out <= 8'd255;
    end else begin
        // 3. 정상 범위 (0 ~ 255): 하위 8비트만 깔끔하게 내보냅니다.
        r_out <= final_r[7:0];
    end
end
```

---

## 10. 🪄 화질 튜닝의 예술: "음수를 어떻게 줄이는가?"

위에서 음수를 0으로 깎아버리면 어두운 계조의 디테일이 시커멓게 뭉개지는 **'블랙 크러시(Black Crush)'** 부작용이 생깁니다. 수학 엔진이 뱉어낸 '날것의 계수'를 함부로 쓰지 않고, 파이썬 단에서 음수 발생 자체를 줄이는 2가지 수학적 마사지를 거칩니다.

### ① 전체 볼륨 스케일 다운 (Global Scaling)
* **문제:** 연산 결과가 `R=1.2`, `G=-0.2`, `B=-0.1`이라면, 클리핑 후 `R=1.0`, `G=0`, `B=0`이 되어 색상(Hue) 비율이 완전히 깨집니다.
* **해결:** 계수 전체에 **0.8 (또는 비율 유지 마진)**을 곱해버립니다. `R=0.96`, `G=-0.16`, `B=-0.08`이 되면서 전체 밝기는 다소 어두워지지만 R/G/B의 비율(Ratio)이 보존되어 클리핑 데미지가 최소화됩니다.

### ② 타겟 색역 압축 (Gamut Compression)
* **문제:** 물리적으로 불가능한 스펙(예: 100% DCI-P3)을 억지로 맞추려 할 때 억지스러운 음수 계수가 도출됩니다.
* **해결:** 인간의 눈은 진한 원색의 '쨍함'이 조금 빠지는 것보다 어두운 곳의 디테일이 뭉개지는 것에 훨씬 민감합니다. 화질 엔지니어들은 타겟 색역을 패널 능력의 **90% 수준으로 살짝 압축(Desaturation)**하여 역행렬을 풉니다. 이로써 `-0.2`였던 계수가 `-0.02`로 확 끌어올려져 음수 폭발을 원천 차단합니다. 

> 이러한 처절한 타협점들이 12-bit 정수로 압축되어 3x3 Matrix 하드웨어에 탑재됩니다.

---

## 11. 🧩 초대형 시스템의 지혜: 모듈러 LED 비디오 월 교집합 튜닝 (Seamless Calibration)

단일 패널(TV나 모니터)의 튜닝 한계를 넘어서, 여러 개의 LED 캐비닛(Cabinet)을 이어 붙여 하나의 거대한 화면을 만드는 모듈러 LED 비디오 월(Modular LED Video Wall)에서는 **"캐비닛마다 타겟(Target)이 다르면 경계선이 바둑판처럼 쩍쩍 갈라져 보이는 대참사(Checkerboard Effect)"**가 일어납니다. 

이를 해결하기 위해 현업(NovaStar, Brompton 등)에서 사용하는 최상위 아키텍처 전략이 바로 **"하향 평준화(Lowest Common Denominator)"**이자 **"색역 교집합 맵핑(Gamut Intersection Mapping)"**입니다.

### 🧱 1. 바둑판 현상 (Checkerboard Effect)의 원인
같은 공장에서 같은 날 생산된 LED 소자라도 캐비닛마다 낼 수 있는 최대 밝기와 색역(Native Gamut)이 미세하게 다릅니다.
* **캐비닛 A:** DCI-P3 98%까지 표현 가능 (우등생)
* **캐비닛 B:** DCI-P3 92%까지 표현 가능 (열등생)

만약 각 캐비닛별로 "네가 낼 수 있는 최대치로 튜닝해!"라고 명령하면, 화면 전체에 새빨간 스포츠카가 지나갈 때 캐비닛 A 영역은 '깊고 진한 빨강'으로 보이고 캐비닛 B 영역은 '살짝 물 빠진 빨강'으로 보여 물리적 이음새(Seam)가 눈에 확 띄게 됩니다.

### ✂️ 2. 실무의 해결책: "가장 못난 놈에게 맞춰라 (교집합 추출)"
이 경계선을 없애는 유일한 방법은 모든 캐비닛의 타겟(Target Gamut & Target White)을 완벽하게 재설정하고 통일하는 것입니다.

1. **전수 조사 (Profiling):** 비디오 월을 구성할 전체 캐비닛이 낼 수 있는 최대 순색의 계측 데이터(Native XYZ 좌표)를 중앙 컨트롤러로 수집합니다.
2. **공통 교집합 타겟 추출 (Find the Intersection & Worst White):** 모든 캐비닛의 색역 삼각형이 겹치는 **'가장 작고 안전한 교집합 영역(Common Gamut)'**과 가장 밝기가 어두운 캐비닛의 Max White를 찾아내어 **새로운 전역 타겟 행렬($M_{Target\_New}$)**을 하나 도출해냅니다.
3. **맞춤형 매트릭스 재계산 (Recalculation):** 하향 평준화된 $M_{Target\_New}$를 기준으로 100개 캐비닛 각각의 고유한 $M_{Native}$를 대입하여 서로 다른 3x3 매트릭스 계수를 100개 독립적으로 계산하여 뿌립니다. (우등생 캐비닛은 스스로 색을 죽이는 계수가 들어가고, 열등생은 영혼까지 끌어쓰는 계수가 들어갑니다.)

### 💡 결론: 타겟은 하나지만, 하드웨어 계수는 모두 다르다
매트릭스 연산 결과에 단순 스칼라율 비율 곱셈을 하면 색좌표가 뒤틀립니다. 목표점(Target) 자체를 재설정한 뒤 각자의 상태($M_{Native}$)에 맞게 3x3 계수 9개를 도출하여 각각의 FPGA에 담아주면, 수백 개의 캐비닛이 완벽하게 하나의 화면처럼 융합되는 **이음새 없는 캘리브레이션 (Seamless Calibration)**이 완성됩니다.

---

### 🧮 4. 교집합 타겟과 매트릭스 재계산의 수학적/알고리즘적 프로세스

실제 캘리브레이션 소프트웨어(PC)에서 100개의 캐비닛 데이터를 가지고 이 과정을 어떻게 연산하는지 살펴보겠습니다.

#### Step 1. 교집합 색좌표($x, y$) 도출 (Finding the Common Gamut)
100개의 캐비닛이 가진 고유한 Red, Green, Blue의 색좌표(CIE $x, y$) 100세트를 xy 색도도(Chromaticity Diagram) 위에 펼치면 100개의 미세하게 다른 삼각형이 그려집니다. 시스템은 이 100개의 삼각형이 모두 포함되는 **내부 교집합 다각형**을 구합니다.
* **실무적 숏컷(Simplification):** 화이트 포인트(예: D65)를 중심으로, 100개의 Red 중 가장 채도가 낮은(White에 가장 가까운) Red, 가장 채도가 낮은 Green, 가장 채도가 낮은 Blue 좌표를 각각 새로운 타겟 $x, y$ 좌표로 채택합니다. 이렇게 하면 모든 캐비닛이 무리 없이 낼 수 있는 '하향 평준화된 교집합 삼각형(안전 구역)'이 도출됩니다.

#### Step 2. 최저 밝기($Y$) 도출 (Finding the Worst White)
계측된 100개의 White 밝기($Y$ 값, 단위: nits) 배열에서 **최소값(Minimum)**을 찾습니다.
* 예: 100개 중 가장 어두운 캐비닛의 밝기가 800nit라면, 전역 타겟 밝기 $Y_{Target} = 800$으로 고정됩니다.

#### Step 3. 새로운 전역 타겟 행렬($M_{Target\_New}$) 도출
Step 1에서 얻은 교집합 R, G, B 색좌표($x, y$)와 D65 타겟 화이트의 $x, y$, 그리고 Step 2의 타겟 밝기($Y=800$)를 앞서 6장에서 소개한 `get_rgb_to_xyz_matrix()` 파이썬 함수에 대입합니다.
* 이 단 한 번의 연산으로 전체 비디오 월이 공통으로 지향해야 할 유일무이한 **전역 타겟 3x3 행렬 ($M_{Target\_New}$)** 이 생성됩니다.

#### Step 4. 100개의 맞춤형 매트릭스 재계산 및 분배 (Recalculation Loop)
이제 반복문(Loop)을 돌며 각 캐비닛(FPGA) 로컬 플래시에 내려보낼 12-bit 계수를 각각 따로 계산합니다.

```python
for i in range(100):
    # 각 "i"번째 캐비닛의 고유한 Native 계측 데이터로 만든 행렬 (100개가 모두 다름)
    M_native_i = cabinet_data[i].matrix 
    
    # 공통 타겟에 도달하기 위한 개별 캐비닛의 변환 행렬 도출 (Native 역행렬 * Target)
    M_hw_float_i = np.linalg.inv(M_native_i).dot(M_Target_New)
    
    # FPGA 연산기 탑재용 12-bit Fixed-Point 변환
    M_hw_fixed_i = np.round(M_hw_float_i * 1024).astype(int)
    
    # 도출된 고유한 9개의 계수를 i번째 캐비닛으로 통신망(Ethernet)을 통해 전송!
    send_to_fpga(cabinet_id=i, matrix_data=M_hw_fixed_i)
```

이 4단계 알고리즘 프로세스를 거치면, 우등생 캐비닛은 빛을 줄이고 채도를 빼는 계수가 자동으로 들어가고, 열등생 캐비닛은 자신의 한계치까지 빛을 쥐어짜는 계수가 들어가며 **완벽한 바둑판 이음새 제거(Seamless Video Wall)**가 수학성으로 달성됩니다.

---

## 12. 🛡️ 최종 수비수: 디더링(Dithering)의 필연성과 PWM Dead Zone

디스플레이 알고리즘의 최상단(Color Science)인 3x3 Matrix와 반도체의 가장 밑바닥 물리적 한계인 LED Driver 특성을 **하나의 선으로 완벽하게 연결**해 주는 궁극의 블록이 바로 파이프라인의 맨 마지막에 위치한 **'디더링(Dithering)'**입니다. 

디더링이 왜 단순 화질 개선을 넘어 시스템의 '필연적 구원자'인지 하드웨어 아키텍처 관점에서 해부해 보겠습니다.

### 🌑 1. 3x3 Matrix의 저주: "너무 미세한 찌꺼기 값"
매트릭스로 타겟 색을 맞추려 비틀다 보면, 원래 꺼져 있어야 할 채널에 아주 미세한 값이 섞여 들어갑니다.
* 예: 순수한 빨간색을 튜닝하기 위한 연산 결과 $\rightarrow$ `Red = 40000`, `Green = 5`, `Blue = 2`
* 수학적으로는 완벽한 비율이지만 하드웨어에는 치명적인 문제가 생깁니다.

### ⚡ 2. 물리적 LED Driver의 한계: "Dead Zone (Minimum On-Time)"
수학적으로는 Green을 '5'만큼 켜라고 지시했지만, LED 드라이버 IC 입장에서는 빛을 낼 수 없는 **'데드 존(Dead Zone)'**에 빠집니다.
* **Turn-on Delay와 기생 커패시턴스:** PWM 신호가 'High'를 치고 올라가도, 전류가 차올라 실제 LED가 빛을 내기까지는 물리적인 지연 시간이 발생합니다.
* **점등 실패:** 만약 드라이버 IC가 빛을 낼 수 있는 최소 PWM 펄스 폭(Minimum On-Time)이 '10'이라면, 매트릭스가 뼈 빠지게 계산한 Green '5'와 Blue '2'는 LED를 켜지도 못하고 그냥 씹혀버립니다. 결과적으로 정교하게 맞춘 타겟 색좌표가 붕괴되고, 어두운 디테일은 시커멓게 죽어버리는 악성 **Black Crush**로 이어집니다.

### 🛡️ 3. 디스플레이의 구원자: Bit-Split Dithering
바로 이 지점에서 파이프라인의 맨 끝단, **'디더링(Dithering) IP'**가 등판합니다. 
디더링 블록은 매트릭스가 뱉어낸 저 미세한 Green '5'를, LED가 켤 수 없는 짧은 펄스로 매 프레임 무의미하게 쏘라고 명령하지 않습니다.

1. **에너지의 누적과 이월 (Noise Injection):** 드라이버가 켤 수 없는 자투리 에너지('5')를 주변 픽셀들이나 다음 프레임으로 시간적/공간적으로 이월시키고 누적합니다.
2. **가동 가능한 펄스 발사:** 에너지가 점차 누적되어 드라이버가 확실히 점등할 수 있는 물리적 크기(예: 8-bit의 1 LSB 단위인 '16')에 도달하면, 비로소 드라이버에 LED를 깜빡이라는 확실한 펄스를 발사합니다.
3. **인간 시각의 착각 (Optical Integration):** 인간의 눈은 공간적/시간적으로 깜빡이는 빛을 적분(평균)하여, **"아, 아주 옅고 미세한 Green 빛(5)이 섞여 있구나"** 하고 완벽하게 착각하게 됩니다.

### 💡 결론: 완벽한 인과관계의 완성
1. **3x3 Matrix**가 수학적으로 완벽한 고정밀(16-bit) 색상 튜닝 비율을 만들어내면,
2. 필연적으로 생겨나는 **'물리적으로 점등 불가능한 찌꺼기 값(Micro-pulses)'**들을,
3. **'디더링 IP'**가 뭉치고 흩뿌려 드라이버 IC가 소화할 수 있는 안전한 PWM 펄스로 번역해 냅니다.

디더링은 단순한 비트 깎기 노이즈 발생기가 아니라, 색역 튜닝(3x3 Matrix)이라는 거대한 이상향이 물리적인 벽(PWM Dead Zone)에 부딪혀 깨지지 않도록 막아주는 최후의 방패이자 필수 불가결한 생존 아키텍처입니다.
