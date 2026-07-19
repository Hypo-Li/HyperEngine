#!/usr/bin/env python3
# 离线生成 DFG LUT(split-sum 近似里预积分的环境 BRDF), 输出到 Resource/DfgLut.dds。
#
# DFG LUT 只是 (NoV, roughness) 的函数, 不依赖任何场景/材质/光照参数, 是纯数学常量,
# 因此不需要挂进引擎的常规构建流程里每帧/每次构建重新计算——离线跑一次这个脚本,
# 把生成的 Resource/DfgLut.dds 提交进版本库即可。改动分辨率/采样数后重新运行一遍、
# 重新提交新文件即可更新。
#
# 算法: Karis 的 split-sum 近似(2013, "Real Shading in Unreal Engine 4")对镜面 IBL
# 积分做的拆分——
#     specular_IBL ≈ prefilteredEnv(roughness) * (F0 * A + B)
# 其中 A、B 只依赖 (NoV, roughness), 与光照环境无关, 可以脱离场景单独预计算成一张
# 2D LUT。本脚本用 GGX 重要性采样对这个积分做蒙特卡洛估计, 几何项用 Smith 高度相关
# (height-correlated)联合可见性公式, 输出通道采用 Filament/Fdez-Agüera(2019) 的
# 多重散射编码(而不是原始 Karis 的 (A, B) scale-bias 编码), 具体原理见下面
# compute_dfg() 的注释。
#
# 输出格式: 128x128, RG16F, 无 mip, 用 DX10 扩展头(DXGI_FORMAT_R16G16_FLOAT = 34)——
# 传统 DDS_PIXELFORMAT 的 FourCC 表里没有直接对应 R16G16_FLOAT 的项, 必须走 DX10 头。
#
# 依赖: numpy(pip install numpy)

import struct
import numpy as np

LUT_SIZE = 128
SAMPLE_COUNT = 1024
OUTPUT_PATH = "../Resource/DfgLut.dds"

DXGI_FORMAT_R16G16_FLOAT = 34
DDS_DIMENSION_TEXTURE2D = 3


def radical_inverse_vdc(bits: int) -> float:
    # Van der Corput 基2基数反演: 把 bits 的二进制表示按位镜像翻转, 结果解读成
    # [0,1) 区间的小数。用 & 0xFFFFFFFF 在每一步位运算后手动截断, 模拟 32 位无符号
    # 整数的回绕行为。
    bits &= 0xFFFFFFFF
    bits = ((bits << 16) | (bits >> 16)) & 0xFFFFFFFF
    bits = (((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1)) & 0xFFFFFFFF
    bits = (((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2)) & 0xFFFFFFFF
    bits = (((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4)) & 0xFFFFFFFF
    bits = (((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8)) & 0xFFFFFFFF
    return bits * 2.3283064365386963e-10  # bits / 2^32


def hammersley(i: int, n: int) -> tuple[float, float]:
    # Hammersley 低差异序列的第 i 个点: 第一维均匀取 i/n, 第二维用 Van der Corput
    # 序列, 两维组合起来在 [0,1)^2 上比纯随机采样更均匀, 减少蒙特卡洛积分的方差。
    return (i / n, radical_inverse_vdc(i))


# 所有 texel 共用同一组采样点: Xi 序列本身只取决于 i/SAMPLE_COUNT, 和具体某个 texel
# 的 (NoV, roughness) 无关, 提前算好避免每个 texel 重复计算。
_XI = np.array([hammersley(i, SAMPLE_COUNT) for i in range(SAMPLE_COUNT)])  # (N, 2)


def importance_sample_ggx(xi: np.ndarray, alpha: float) -> np.ndarray:
    # 按 GGX 法线分布函数重要性采样出一个切线空间半程向量 H(法线固定为 +Z)。
    # 采样密度正比于 D(H), 这样蒙特卡洛估计里 D 项和采样 pdf 大部分抵消, 不需要在
    # 积分里显式再乘一次 D。
    a2 = alpha * alpha
    cos_theta = np.sqrt((1.0 - xi[:, 1]) / (1.0 + (a2 - 1.0) * xi[:, 1]))
    sin_theta = np.sqrt(np.clip(1.0 - cos_theta * cos_theta, 0.0, None))
    phi = 2.0 * np.pi * xi[:, 0]
    return np.stack([sin_theta * np.cos(phi), sin_theta * np.sin(phi), cos_theta], axis=-1)


def smith_height_correlated_gdfg(nov: float, nol: np.ndarray, alpha: float) -> np.ndarray:
    # Smith 高度相关(height-correlated)联合遮蔽-阴影可见性项, 用 Heitz(2014,
    # "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs")给出
    # 的数值稳定形式。这个函数在数学上等价于经典 Smith 几何项 G 除以 NoV
    # (G_smith(NoV,NoL,alpha) / NoV), 也就是说 Cook-Torrance 分母里 4*NoV*NoL 中的
    # NoV 已经被这个公式自己的定义吸收掉了一份——调用方(compute_dfg)不需要、也不应该
    # 再额外除一次 NoV, 否则相当于对 NoV 做了两次归一化, 会在 NoV 趋近 0 的掠射角区域
    # 造成数值发散。
    a2 = alpha * alpha
    ggx_l = nov * np.sqrt((-nol * a2 + nol) * nol + a2)
    ggx_v = nol * np.sqrt((-nov * a2 + nov) * nov + a2)
    return (2.0 * nol) / (ggx_v + ggx_l)


def compute_dfg(nov: float, perceptual_roughness: float) -> tuple[float, float]:
    # 感知粗糙度(perceptualRoughness, 材质参数暴露给美术的线性 [0,1] 粗糙度)在这里
    # 换算成 GGX 分布实际使用的 alpha = roughness^2(Disney/UE 的标准约定, 让粗糙度
    # 在感知上更线性)。alpha 下限钳制到一个很小的正数, 避免 alpha=0 时 GGX 分布退化
    # 成 delta 函数导致除零。
    alpha = max(perceptual_roughness * perceptual_roughness, 1e-6)

    v = np.array([np.sqrt(max(1.0 - nov * nov, 0.0)), 0.0, nov])
    h = importance_sample_ggx(_XI, alpha)  # (N, 3)
    voh_raw = h @ v  # dot(V, H), (N,)
    l = 2.0 * voh_raw[:, None] * h - v  # 反射公式求出光线方向 L, (N, 3)

    voh = np.clip(voh_raw, 0.0, 1.0)
    nol = np.clip(l[:, 2], 0.0, 1.0)
    noh = np.clip(h[:, 2], 0.0, 1.0)

    valid = nol > 0.0
    g = np.zeros_like(nol)
    g[valid] = smith_height_correlated_gdfg(nov, nol[valid], alpha)

    # 把几何项和重要性采样的 pdf 相除后剩下的部分记作 Gv——理论上 noh 应该和 nol>0
    # 同时成立(H.z<=0 时反射出的 L.z 也不会大于 0), 这里仍然显式排掉 noh 趋近 0 的
    # 极端采样, 避免浮点数值上偶发的除以极小值。
    gv = np.zeros_like(nol)
    stable = valid & (noh > 1e-6)
    gv[stable] = g[stable] * voh[stable] / noh[stable]

    # 通道编码: 采用 Filament / Fdez-Agüera(2019, "A Multiple-Scattering Microfacet
    # Model for Real-Time Image-based Lighting")的多重散射友好编码, 而不是 Karis
    # 原始的 (scale, bias) 编码。
    #
    # 令 Fc = (1-VoH)^5(Schlick 菲涅尔近似里的高次项), 单次散射的镜面 IBL 结果本应是
    #     specular = F0 * sum(Gv*(1-Fc)) + sum(Gv*Fc)
    # 这里改成存:
    #     r.x = sum(Gv*Fc)
    #     r.y = sum(Gv)          (不带 Fresnel 加权)
    # 使用方对应写成 specular = mix(r.x, r.y, F0) = (1-F0)*r.x + F0*r.y。展开验证
    # 这和上面的 specular 公式是同一个表达式(只是把 r.x/r.y 换了一个基底), 数学上
    # 完全等价, 单次散射意义上不会因为换了编码而多出或少算能量。
    #
    # 这样编码的价值在于 r.y = sum(Gv)(不带 Fresnel 加权)恰好等于 Kulla & Conty
    # (2017, "Revisiting Physically Based Shading at Imageworks")意义上的单次散射
    # 方向反照率 Ess(NoV, roughness)——即假设 F=1(完美镜面菲涅尔)时这个 BRDF 在
    # 半球上积分出的总反射率。GGX 微表面模型的单次散射假设丢失了微表面间多次弹射的
    # 能量, 粗糙度越高丢失越明显(Ess 越远离 1), 而 r.y 直接暴露了这个量, 运行时可以
    # 额外乘一个能量补偿系数
    #     energyCompensation = 1 + F0 * (1/r.y - 1)
    # 到镜面反射结果上, 近似地把这部分被单次散射假设丢掉的能量补偿回来。
    fc = np.power(1.0 - voh, 5.0)
    r_x = float(np.sum(gv * fc) / SAMPLE_COUNT)
    r_y = float(np.sum(gv) / SAMPLE_COUNT)
    return r_x, r_y


def build_lut() -> np.ndarray:
    # texel (row, col): row 对应粗糙度轴, col 对应 NoV 轴, 读取端按同样的 (NoV,
    # roughness) 顺序采样这张贴图。用像素中心取值(+0.5), 避免 NoV=0/roughness=0
    # 这类边界值正好落在网格顶点上。
    lut = np.zeros((LUT_SIZE, LUT_SIZE, 2), dtype=np.float32)
    for row in range(LUT_SIZE):
        perceptual_roughness = (row + 0.5) / LUT_SIZE
        for col in range(LUT_SIZE):
            nov = (col + 0.5) / LUT_SIZE
            lut[row, col, 0], lut[row, col, 1] = compute_dfg(nov, perceptual_roughness)
    return lut


def write_dds(path: str, lut: np.ndarray) -> None:
    half_pixels = lut.astype(np.float16)  # IEEE754 binary16, 和 DXGI R16G16_FLOAT 的内存布局完全一致
    pixel_bytes = half_pixels.tobytes()

    bytes_per_pixel = 4  # 2 channel * 2 byte (half)
    pitch = LUT_SIZE * bytes_per_pixel

    DDSD_CAPS = 0x1
    DDSD_HEIGHT = 0x2
    DDSD_WIDTH = 0x4
    DDSD_PITCH = 0x8
    DDSD_PIXELFORMAT = 0x1000
    DDPF_FOURCC = 0x4
    DDSCAPS_TEXTURE = 0x1000
    FOURCC_DX10 = struct.unpack("<I", b"DX10")[0]

    header = struct.pack(
        "<31I",
        124,  # dwSize
        DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT,  # dwFlags
        LUT_SIZE,  # dwHeight
        LUT_SIZE,  # dwWidth
        pitch,  # dwPitchOrLinearSize
        0,  # dwDepth
        1,  # dwMipMapCount
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  # dwReserved1[11]
        32,  # ddspf.dwSize
        DDPF_FOURCC,  # ddspf.dwFlags
        FOURCC_DX10,  # ddspf.dwFourCC
        0, 0, 0, 0, 0,  # ddspf.{dwRGBBitCount,dwRBitMask,dwGBitMask,dwBBitMask,dwABitMask}
        DDSCAPS_TEXTURE,  # dwCaps
        0, 0, 0,  # dwCaps2/3/4
        0,  # dwReserved2
    )

    dx10_header = struct.pack(
        "<5I",
        DXGI_FORMAT_R16G16_FLOAT,  # dxgiFormat
        DDS_DIMENSION_TEXTURE2D,  # resourceDimension
        0,  # miscFlag
        1,  # arraySize
        0,  # miscFlags2 (DDS_ALPHA_MODE_UNKNOWN)
    )

    with open(path, "wb") as f:
        f.write(b"DDS ")
        f.write(header)
        f.write(dx10_header)
        f.write(pixel_bytes)


if __name__ == "__main__":
    print(f"Computing {LUT_SIZE}x{LUT_SIZE} DFG LUT ({SAMPLE_COUNT} samples/texel)...")
    lut = build_lut()
    write_dds(OUTPUT_PATH, lut)
    print(f"Wrote {OUTPUT_PATH} (max value = {lut.max():.4f})")
