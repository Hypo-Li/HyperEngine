// 屏幕空间 HZB 追踪的临时调试可视化, 验证 HZBTrace.slang(Screen Probe Gather M1)的正确性,
// 不是 Screen Probe Gather 正式管线的一部分——正式接入时探针会用半球余弦采样多条光线, 这里
// 每像素只发一条视线绕法线的镜面反射光线, 方便肉眼核对命中/反射内容对不对。见
// Source/Runtime/Renderer/RenderPass/ScreenTraceVisualizerPass.cpp、
// Document/Lumen_ScreenProbeGather_Plan.md §4。
#include "../Common/Binding.slang"
#include "../Common/MaterialIO.slang"
#include "../Common/HZBTrace.slang"
#include "../Common/SkyLightData.slang"
#include "../Material/ShadingParams.slang"

[[vk::binding(0, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(1, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(2, 2)]]
Texture2D<float4> GbufferCImage;

[[vk::binding(3, 2)]]
Texture2D<float4> GbufferDImage;

[[vk::binding(4, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(5, 2)]]
Texture2D<float2> SceneHZBImage;

[[vk::binding(6, 2)]]
SamplerState PointClampSampler;

// Radiance 模式命中之后采它的颜色当这一像素的间接光——这个 pass 挂在正常光照链路(含 TAA)
// 跑完之后, 采的是这一帧已经产出的真实最终画面, 不是 UE 那种"上一帧"语义(那是因为 UE 屏幕
// 追踪发生在这一帧合成之前, 不得不用上一帧; 这里纯粹是验证 pass, 没有那个先后顺序限制)。
// Hit State 模式不会走到采样它的分支, 见 ScreenTraceVisualizerPass.cpp 里绑的是黑纹理占位
[[vk::binding(7, 2)]]
Texture2D<float4> SceneColorInput;

[[vk::binding(8, 2)]]
SamplerState LinearSampler;

// mip 0 = 接近零粗糙度的 GGX 预滤波, 近似原始天空辐射度, 按射线方向直接采即可得到方向性
// L(dir)——跟 RadiosityTrace.slang 的 SkyLightPrefilter 同一份资源/同一种采法。没有活跃天光时
// 绑黑色 cube(见 ScreenTraceVisualizerPass.cpp), miss 等价于天空辐射度恒为 0
[[vk::binding(9, 2)]]
TextureCube<float4> SkyLightPrefilter;

[[vk::binding(10, 2)]]
StructuredBuffer<float4> SkyLightData;

// 跟 hyp::SceneVisualizeMode 的 ScreenTraceHitState/ScreenTraceRadiance 两个值(15/16)对应
static const uint kVisualizeModeHitState = 15;
static const uint kVisualizeModeRadiance = 16;

struct ScreenTraceVisualizerPushConstants
{
    uint visualizeMode;
    uint padding0;
    float2 hzbUvFactor;
    float2 hzbBaseTexelSize;
    float maxWorldTraceDistance;
    float padding1;
};
[[vk::push_constant]]
ScreenTraceVisualizerPushConstants PC;

static constexpr float2 kPositions[3] = {
    float2(-1, -3),
    float2( 3,  1),
    float2(-1,  1)
};

[shader("vertex")]
float4 VertexMain(in uint vertexID : SV_VertexID) : SV_Position
{
    return float4(kPositions[vertexID], 0, 1);
}

[shader("fragment")]
float4 FragmentMain(in float4 svPosition : SV_Position) : SV_Target
{
    float sceneDepth = SceneDepthImage[uint2(svPosition.xy)].r;
    if (sceneDepth == 0)
        return float4(0, 0, 0, 1);

    uint2 gbufferA = GbufferAImage[uint2(svPosition.xy)];
    float4 gbufferB = GbufferBImage[uint2(svPosition.xy)];
    float4 gbufferC = GbufferCImage[uint2(svPosition.xy)];
    float4 gbufferD = GbufferDImage[uint2(svPosition.xy)];

    float2 uv = svPosition.xy / uViewData.resolution.xy;
    float4 fragPosCS = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, sceneDepth, 1);
    float4 fragPosVS = mul(fragPosCS, uViewData.clipToView);
    fragPosVS *= 1.0 / fragPosVS.w;
    float3 viewDirVS = normalize(-fragPosVS.xyz);
    float3 viewDirWS = mul(uViewData.worldToViewRot, viewDirVS);

    ShadingParams sp = decodeShadingParams(gbufferA, gbufferB, gbufferC, gbufferD, viewDirWS);
    if (sp.shadingModel == ShadingModel::Unlit)
        return float4(0, 0, 0, 1);

    // worldToViewRot 是纯旋转矩阵(正交), mul(v, M) 相对 mul(M, v)(上面 viewDirWS 那次调用)
    // 天然是逆变换(转置=逆)——这里用来把世界空间的反射方向/法线换回 view space, 不需要另外
    // 维护一份 viewToWorldRot
    float3 reflectDirWS = reflect(-viewDirWS, sp.normalWS);
    float3 reflectDirVS = mul(reflectDirWS, uViewData.worldToViewRot);
    float3 normalVS = mul(sp.normalWS, uViewData.worldToViewRot);

    float3 rayOriginVS = applyScreenSpaceRayBias(fragPosVS.xyz, normalVS, uv, sceneDepth, uViewData);

    // 跟 ScreenProbeTrace.slang 一致: 法线偏移在掠射角度下可能把起点推出屏幕外, TraceHZB 不处理
    // 这种情况, 越界起点直接当 miss, 不进 traceHZB
    float4 rayStartClip = mul(float4(rayOriginVS, 1.0), uViewData.viewToClip);
    bool rayStartOnScreen = all(rayStartClip.xy >= -rayStartClip.w) && all(rayStartClip.xy <= rayStartClip.w);

    HZBTraceResult result;
    result.hit = false;
    result.uncertain = false;
    result.hitSceneUV = uv;
    result.hitDeviceZ = 0.0;
    result.rayDeviceZ = sceneDepth;
    result.lastVisibleSceneUV = uv;

    if (rayStartOnScreen)
    {
        result = traceHZB(
            SceneDepthImage,
            SceneHZBImage,
            PointClampSampler,
            uViewData,
            PC.hzbUvFactor,
            PC.hzbBaseTexelSize,
            rayOriginVS,
            reflectDirVS,
            PC.maxWorldTraceDistance,
            0.0
        );
    }

    if (PC.visualizeMode == kVisualizeModeHitState)
    {
        if (result.uncertain)
            return float4(1, 1, 0, 1);
        return result.hit ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
    }

    // kVisualizeModeRadiance——miss(含 uncertain, result.hit 已经把它折成 false 了)取天空
    // 辐射度兜底, 跟正式 Screen Probe Gather 管线里"屏幕/GDF 都没打中就采天光"是同一个兜底层级,
    // 不是额外发明的规则
    if (!result.hit)
        return float4(SkyLightPrefilter.SampleLevel(LinearSampler, reflectDirWS, 0.0).rgb * SkyLightData[kSkyLightRadianceScaleIndex].xyz, 1);
    return float4(SceneColorInput.SampleLevel(LinearSampler, result.hitSceneUV, 0.0).rgb, 1);
}
