// 把间接光照写进一张视锥体对齐的体积纹理里, 半透明材质(Translucent.slang)只认这张纹理,
// 不需要知道背后到底是天光、局部探针还是别的什么东西——这一步只负责"注入", 消费端完全解耦。
//
// 每个体素存 1 阶球谐(常数项 + 线性项, 3 个通道各一个 float4, 布局跟 SkyLightPackedSHCoeff 的
// packedSH[0..2] 完全一致: xyz 是线性系数、w 是常数项), 不存 2 阶的二次项——1 阶已经足够表达
// "哪个方向亮哪个方向暗"这种漫射光源的主要特征, 而且天光注入前本身就是经过余弦加权卷积的平滑
// 辐照度, 2 阶带来的收益很有限, 3 个 float4 比 7 个 float4 省下的存储/带宽更实际。真要用到更高阶
// 的方向性细节, 到时候再加体素也不迟。
//
// 第一版只有 SkyLight 一个注入源, 而且天光对整个场景是空间不变的(只有一份 SH, 跟位置无关),
// 所以每个体素写进去的值目前都完全一样——这看起来像是"绕远路", 但目的是先把体积纹理的格式、
// 读写方式、跟 TranslucentPass 的接口先跑通, 以后接入局部光源时只需要改这里的注入逻辑, 消费端
// (Translucent.slang)完全不用动。存的是漫反射辐照度(跟 SkyLightPackedSHCoeff 本身的约定一致:
// 没有除以 PI, 没有乘 albedo)。

[[vk::binding(0, 2)]]
StructuredBuffer<float4> SkyLightPackedSHCoeff;

[[vk::binding(1, 2)]]
[format("rgba16f")]
RWTexture3D<float4> LightVolumeR;

[[vk::binding(2, 2)]]
[format("rgba16f")]
RWTexture3D<float4> LightVolumeG;

[[vk::binding(3, 2)]]
[format("rgba16f")]
RWTexture3D<float4> LightVolumeB;

struct LightVolumeImmediateData
{
    uint3 dimensions;
};
[[vk::push_constant]]
LightVolumeImmediateData ImData;

// 这个 pass 只在场景有 active SkyLight 时才会被加进 render graph(见 TranslucentPass.cpp),
// 没有天光的场景直接跳过整个注入 pass, 不需要在 shader 里再判断一次"有没有光源"
[shader("compute")]
[numthreads(4, 4, 4)]
void ComputeMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= ImData.dimensions))
        return;

    // 1 阶系数就是 packedSH[0..2] 本身, 天光空间不变, 直接原样复制进每个体素, 不需要按某个
    // 固定法线求值——法线相关的重建挪到 Translucent.slang 采样时再做, 这样才是真正的方向性数据
    LightVolumeR[id] = SkyLightPackedSHCoeff[0];
    LightVolumeG[id] = SkyLightPackedSHCoeff[1];
    LightVolumeB[id] = SkyLightPackedSHCoeff[2];
}
