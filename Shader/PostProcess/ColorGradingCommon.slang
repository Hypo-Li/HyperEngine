#pragma once
#include "../Common/Math.slang"

// ARRI LogC3(EI 800)编解码: 用来把 LUT 的三个坐标轴映射到线性场景颜色, 相比直接线性映射,
// 能把更多分辨率分配给中间调, 是电影调色 LUT 里最常用的坐标编码
static constexpr float kLogC3Cut = 0.010591f;
static constexpr float kLogC3A = 5.555556f;
static constexpr float kLogC3B = 0.052272f;
static constexpr float kLogC3C = 0.247190f;
static constexpr float kLogC3D = 0.385537f;
static constexpr float kLogC3E = 5.367655f;
static constexpr float kLogC3F = 0.092809f;

float linearToLogC(float x)
{
    return x > kLogC3Cut
        ? kLogC3C * log10(kLogC3A * x + kLogC3B) + kLogC3D
        : kLogC3E * x + kLogC3F;
}

float3 linearToLogC(float3 x)
{
    return float3(linearToLogC(x.r), linearToLogC(x.g), linearToLogC(x.b));
}

float logCToLinear(float x)
{
    static constexpr float kBreak = kLogC3E * kLogC3Cut + kLogC3F;
    return x > kBreak
        ? (pow(10.0, (x - kLogC3D) / kLogC3C) - kLogC3B) / kLogC3A
        : (x - kLogC3F) / kLogC3E;
}

float3 logCToLinear(float3 x)
{
    return float3(logCToLinear(x.r), logCToLinear(x.g), logCToLinear(x.b));
}

// ACES(RRT+ODT)用到的色彩空间转换矩阵, 从 Filament 的 ACES.cpp 搬过来(数值上是发布很广的标准
// 矩阵, 不是 Filament 自己发明的), 全部按 mul(vector, matrix) 的行向量约定存储——下面所有用到
// 这些矩阵的地方都要用 mul(v, M), 不能反过来, 否则相当于用了矩阵的转置
static constexpr float3x3 sRGB_to_AP0 = float3x3(
     0.4397010, 0.0897923, 0.0175440,
     0.3829780, 0.8134230, 0.1115440,
     0.1773350, 0.0967616, 0.8707040
);

static constexpr float3x3 AP0_to_AP1 = float3x3(
     1.4514393161, -0.0765537734,  0.0083161484,
    -0.2365107469,  1.1762296998, -0.0060324498,
    -0.2149285693, -0.0996759264,  0.9977163014
);

static constexpr float3x3 AP1_to_sRGB = float3x3(
     1.70505, -0.13026, -0.02400,
    -0.62179,  1.14080, -0.12897,
    -0.08326, -0.01055,  1.15297
);

static constexpr float3x3 AP1_to_XYZ = float3x3(
     0.6624541811,  0.2722287168, -0.0055746495,
     0.1340042065,  0.6740817658,  0.0040607335,
     0.1561876870,  0.0536895174,  1.0103391003
);

static constexpr float3x3 XYZ_to_AP1 = float3x3(
     1.6410233797, -0.6636628587,  0.0117218943,
    -0.3248032942,  1.6153315917, -0.0082844420,
    -0.2364246952,  0.0167563477,  0.9883948585
);

// RGB to luminance coefficients for ACEScg(AP1), 是 AP1_to_XYZ 里对应 Y 的那一行
static constexpr float3 LUMINANCE_AP1 = float3(0.272229, 0.674082, 0.0536895);

static constexpr float kFloatMax = 3.402823466e+38;

float3 xyY_to_XYZ(float3 v)
{
    const float a = v.z / max(v.y, 1e-5);
    return float3(v.x * a, v.z, (1.0 - v.x - v.y) * a);
}

float3 XYZ_to_xyY(float3 v)
{
    return float3(v.xy / max(v.x + v.y + v.z, 1e-5), v.y);
}

float rgb_2_saturation(float3 rgb)
{
    const float kTiny = 1e-5;
    float mi = min(min(rgb.r, rgb.g), rgb.b);
    float ma = max(max(rgb.r, rgb.g), rgb.b);
    return (max(ma, kTiny) - max(mi, kTiny)) / max(ma, 1e-2);
}

// 把 RGB 转成一个亮度代理 YC(约等于 Y + K * Chroma), 是一个以中性轴为顶点、朝白色方向展开的
// 锥形等值面; ycRadiusWeight = 1.75 是 ACES 参考实现里的默认值
float rgb_2_yc(float3 rgb)
{
    const float kYcRadiusWeight = 1.75;
    float r = rgb.r;
    float g = rgb.g;
    float b = rgb.b;
    float chroma = sqrt(b * (b - g) + g * (g - r) + r * (r - b));
    return (b + g + r + kYcRadiusWeight * chroma) / 3.0;
}

// [-2, 2] 区间的 sigmoid, 输出范围 [0, 1]
float sigmoid_shaper(float x)
{
    float t = max(1.0 - abs(x / 2.0), 0.0);
    float y = 1.0 + sign(x) * (1.0 - t * t);
    return y / 2.0;
}

float glow_fwd(float ycIn, float glowGainIn, float glowMid)
{
    if (ycIn <= 2.0 / 3.0 * glowMid)
        return glowGainIn;
    if (ycIn >= 2.0 * glowMid)
        return 0.0;
    return glowGainIn * (glowMid / ycIn - 0.5);
}

// 返回 0-360 度的几何色相角; RGB 三分量相等时色相无定义, 返回 0
float rgb_2_hue(float3 rgb)
{
    float hue = 0.0;
    if (!(rgb.x == rgb.y && rgb.y == rgb.z))
    {
        hue = RAD_TO_DEG * atan2(sqrt(3.0) * (rgb.y - rgb.z), 2.0 * rgb.x - rgb.y - rgb.z);
    }
    return hue < 0.0 ? hue + 360.0 : hue;
}

float center_hue(float hue, float centerH)
{
    float hueCentered = hue - centerH;
    if (hueCentered < -180.0)
        hueCentered += 360.0;
    else if (hueCentered > 180.0)
        hueCentered -= 360.0;
    return hueCentered;
}

// 官方 ACES ODT 假设"暗室"观看环境, 这里补偿到"暗环境"(dim surround), 通过 xyY 空间对
// 亮度分量做一次 gamma 校正实现
float3 darkSurround_to_dimSurround(float3 linearCV)
{
    const float kDimSurroundGamma = 0.9811;

    float3 xyz = mul(linearCV, AP1_to_XYZ);
    float3 xyY = XYZ_to_xyY(xyz);

    xyY.z = clamp(xyY.z, 0.0, kFloatMax);
    xyY.z = pow(xyY.z, kDimSurroundGamma);

    xyz = xyY_to_XYZ(xyY);
    return mul(xyz, XYZ_to_AP1);
}

// 实时引擎里最常用的"完整版"ACES(RRT+ODT, RGB monitor 100 nits dim)实时近似, 从 Filament 的
// ACES.cpp 搬过来, 只去掉了 Filament 自己加的 brightness 系数(那是他们为了兼容自家旧版
// tonemapper 亮度加的历史包袱, 不是官方 ACES 的一部分)。依次包含: Glow 模块、红色色相修正、
// AP0->AP1 + 一次全局去饱和、RRT+ODT 有理函数拟合、暗室->普通室内的 gamma 补偿、二次去饱和。
float3 ACES(float3 color)
{
    // Calibrate the fitted RRT + ODT curve so neutral 18% scene grey maps back to
    // approximately 18% display-linear grey. The uncalibrated fit returns about 0.108.
    static constexpr float kMiddleGreyInputScale = 1.439;
    color *= kMiddleGreyInputScale;

    // "Glow" 模块常数
    const float kRRTGlowGain = 0.05;
    const float kRRTGlowMid = 0.08;

    // 红色修正常数
    const float kRRTRedScale = 0.82;
    const float kRRTRedPivot = 0.03;
    const float kRRTRedHue = 0.0;
    const float kRRTRedWidth = 135.0;

    // 去饱和常数
    const float kRRTSatFactor = 0.96;
    const float kODTSatFactor = 0.93;

    float3 ap0 = mul(color, sRGB_to_AP0);

    // Glow 模块
    float saturation = rgb_2_saturation(ap0);
    float ycIn = rgb_2_yc(ap0);
    float s = sigmoid_shaper((saturation - 0.4) / 0.2);
    float addedGlow = 1.0 + glow_fwd(ycIn, kRRTGlowGain * s, kRRTGlowMid);
    ap0 *= addedGlow;

    // 红色修正
    float hue = rgb_2_hue(ap0);
    float centeredHue = center_hue(hue, kRRTRedHue);
    float hueWeight = smoothstep(0.0, 1.0, 1.0 - abs(2.0 * centeredHue / kRRTRedWidth));
    hueWeight *= hueWeight;
    ap0.r += hueWeight * saturation * (kRRTRedPivot - ap0.r) * (1.0 - kRRTRedScale);

    // ACES to RGB rendering space
    float3 ap1 = clamp(mul(ap0, AP0_to_AP1), 0.0, kFloatMax);

    // 全局去饱和
    ap1 = lerp(float3(dot(ap1, LUMINANCE_AP1)), ap1, kRRTSatFactor);

    // RRT + ODT(RGB monitor 100 nits dim)拟合, 来自:
    // https://github.com/colour-science/colour-unity/blob/master/Assets/Colour/Notebooks/CIECAM02_Unity.ipynb
    const float a = 2.785085;
    const float b = 0.107772;
    const float c = 2.936045;
    const float d = 0.887122;
    const float e = 0.806889;
    float3 rgbPost = (ap1 * (a * ap1 + b)) / (ap1 * (c * ap1 + d) + e);

    // 补偿暗室观看环境的 gamma 调整
    float3 linearCV = darkSurround_to_dimSurround(rgbPost);

    // 补偿亮度差异的二次去饱和
    linearCV = lerp(float3(dot(linearCV, LUMINANCE_AP1)), linearCV, kODTSatFactor);

    return mul(linearCV, AP1_to_sRGB);
}

float ueFilmicCurve(
    float linearColor,
    float filmSlope,
    float filmToe,
    float filmShoulder,
    float filmBlackClip,
    float filmWhiteClip)
{
    if (linearColor <= 0.0)
        return 0.0;

    float toeScale = 1.0 + filmBlackClip - filmToe;
    float shoulderScale = 1.0 + filmWhiteClip - filmShoulder;

    // UE constructs the curve around this explicit middle-grey anchor.
    static constexpr float kMiddleGrey = 0.18;
    float toeMatch;
    if (filmToe > 0.8)
    {
        toeMatch = (1.0 - filmToe - kMiddleGrey) / filmSlope + log10(kMiddleGrey);
    }
    else
    {
        float bt = (kMiddleGrey + filmBlackClip) / toeScale - 1.0;
        toeMatch = log10(kMiddleGrey)
            - 0.5 * log((1.0 + bt) / (1.0 - bt)) * (toeScale / filmSlope);
    }

    float straightMatch = (1.0 - filmToe) / filmSlope - toeMatch;
    float shoulderMatch = filmShoulder / filmSlope - straightMatch;
    float logColor = log10(linearColor);
    float straightColor = filmSlope * (logColor + straightMatch);
    float toeColor = -filmBlackClip
        + (2.0 * toeScale)
            / (1.0 + exp((-2.0 * filmSlope / toeScale) * (logColor - toeMatch)));
    float shoulderColor = 1.0 + filmWhiteClip
        - (2.0 * shoulderScale)
            / (1.0 + exp((2.0 * filmSlope / shoulderScale) * (logColor - shoulderMatch)));

    toeColor = logColor < toeMatch ? toeColor : straightColor;
    shoulderColor = logColor > shoulderMatch ? shoulderColor : straightColor;

    float t = saturate((logColor - toeMatch) / (shoulderMatch - toeMatch));
    t = shoulderMatch < toeMatch ? 1.0 - t : t;
    t = (3.0 - 2.0 * t) * t * t;
    return lerp(toeColor, shoulderColor, t);
}

// UE-style filmic tonemapper with the default FPostProcessSettings values.
float3 UEFilmic(float3 color)
{
    static constexpr float kFilmSlope = 0.88;
    static constexpr float kFilmToe = 0.55;
    static constexpr float kFilmShoulder = 0.26;
    static constexpr float kFilmBlackClip = 0.0;
    static constexpr float kFilmWhiteClip = 0.04;

    static constexpr float kRRTGlowGain = 0.05;
    static constexpr float kRRTGlowMid = 0.08;
    static constexpr float kRRTRedScale = 0.82;
    static constexpr float kRRTRedPivot = 0.03;
    static constexpr float kRRTRedHue = 0.0;
    static constexpr float kRRTRedWidth = 135.0;
    static constexpr float kRRTSatFactor = 0.96;
    static constexpr float kODTSatFactor = 0.93;

    float3 ap0 = mul(color, sRGB_to_AP0);

    float saturation = rgb_2_saturation(ap0);
    float ycIn = rgb_2_yc(ap0);
    float s = sigmoid_shaper((saturation - 0.4) / 0.2);
    float addedGlow = 1.0 + glow_fwd(ycIn, kRRTGlowGain * s, kRRTGlowMid);
    ap0 *= addedGlow;

    float hue = rgb_2_hue(ap0);
    float centeredHue = center_hue(hue, kRRTRedHue);
    float hueWeight = smoothstep(0.0, 1.0, 1.0 - abs(2.0 * centeredHue / kRRTRedWidth));
    hueWeight *= hueWeight;
    ap0.r += hueWeight * saturation * (kRRTRedPivot - ap0.r) * (1.0 - kRRTRedScale);

    float3 ap1 = clamp(mul(ap0, AP0_to_AP1), 0.0, kFloatMax);
    ap1 = lerp(float3(dot(ap1, LUMINANCE_AP1)), ap1, kRRTSatFactor);

    float3 toneColor = float3(
        ueFilmicCurve(ap1.r, kFilmSlope, kFilmToe, kFilmShoulder, kFilmBlackClip, kFilmWhiteClip),
        ueFilmicCurve(ap1.g, kFilmSlope, kFilmToe, kFilmShoulder, kFilmBlackClip, kFilmWhiteClip),
        ueFilmicCurve(ap1.b, kFilmSlope, kFilmToe, kFilmShoulder, kFilmBlackClip, kFilmWhiteClip));
    toneColor = lerp(float3(dot(toneColor, LUMINANCE_AP1)), toneColor, kODTSatFactor);

    return max(mul(toneColor, AP1_to_sRGB), 0.0);
}

float linearToSRGB(float color)
{
    color = max(color, 0.0);
    return color <= 0.0031308
        ? 12.92 * color
        : 1.055 * pow(color, 1.0 / 2.4) - 0.055;
}

float3 linearToSRGB(float3 color)
{
    return float3(
        linearToSRGB(color.r),
        linearToSRGB(color.g),
        linearToSRGB(color.b));
}
