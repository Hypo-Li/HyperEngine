#include <Common/Binding.slang>
#include <Common/MaterialIO.slang>
#include "ShadingParams.slang"
#include "BxDF.slang"
#include "Light.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

// 点光源/聚光灯的光源体积几何 pass——VertexMain 按 SV_VertexID 现算球体(点光源)/锥体(聚光灯)
// 几何, 代替 DirectLighting.slang 的全屏三角形, 靠背面剔除 + 深度测试把着色范围天然限制在光源
// 体积的屏幕投影内, 减少无谓的像素着色调用。相机钻进光源体积内部时(近裁剪面可能把背面裁掉)
// 退化用 DirectLighting.slang 的全屏三角形路径, 那边已经有同一套点光源/聚光灯着色分支, 不用
// 重复实现。
//
// 深度只测试不写, SceneDepthImage(D32/S8 格式的真正深度附件)负责硬件深度测试, SceneDepthDImage
// 另外采样一份用来手动重建像素位置——跟 DirectLighting.slang 用 SceneDepthD 的道理一样, 硬件测试
// 附件本身不能在同一个 pass 里当 SRV 采样。

[[vk::binding(0, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(1, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(2, 2)]]
Texture2D<float4> GbufferCImage;

[[vk::binding(3, 2)]]
Texture2D<float4> GbufferDImage;

[[vk::binding(4, 2)]]
DepthTexture2D SceneDepthDImage;

[[vk::binding(5, 2)]]
StructuredBuffer<LightData> sLightData;

// 字段顺序特意把 float3 摆在 offset 0(天然对齐), lightIndex 这个标量塞进用剩的 4 字节里,
// 不会单独再占一整个 16 字节槽——教训见 LightData 那次的 padding1 bug(Binding.slang 里的
// 注释), push constant block 是 std430 对齐规则, 不能假设 HLSL cbuffer 那种更宽松的规则
struct LightVolumePushConstants
{
    float3 positionVS; // 相机相对 view space 位置, CPU 侧每帧现算——只用来摆放这个工具网格本身
                        // 的屏幕位置, 容忍一点误差, 跟 Light.slang 里像素级精确的光照位置计算是
                        // 两回事, 互不影响
    uint lightIndex;
};
[[vk::push_constant]]
LightVolumePushConstants kPushConstants;

static const float kPI = 3.14159265358979323846;

// 跟 DefaultResources::kLightVolumeConeSides/Slices 手动保持一致, 这两个值决定了 CPU 侧
// drawIndexed 的 vertexCount, 改这边必须同步改那边
static const uint kConeSides = 18;
static const uint kConeSlices = 12;
static const uint kSphereSides = 18;
static const uint kSphereRings = 12;

// 锥体(侧面+球形封口)按 UE FStencilConeIndexBuffer/DeferredLightVertexShaders.usf 的
// SHADER_RADIAL_LIGHT 分支现算, 用这盏灯真实的 attenuationRadius/outerConeAngle, 不是烘焙在
// 某个参照角上再靠非均匀缩放去近似——非均匀缩放对球形封口那部分(要维持在球面上依赖 sqrt 关系)
// 做不到精确, 偏离参照角越远, 体积跟灯光实际锥角的偏差越大, 窄锥角聚光灯会在锥角边缘出现
// 体积欠覆盖、漏光被裁掉的问题
float3 computeConeLocalPosition(uint vertexId, float attenuationRadius, float outerConeAngle)
{
    const uint kSideSliceOffset[6] = { 0, 0, 1, 1, 0, 1 };
    const uint kSideUseNextSide[6] = { 0, 1, 0, 0, 1, 1 };
    const uint kCapSliceOffset[6] = { 0, 1, 0, 0, 1, 1 };
    const uint kCapUseNextSide[6] = { 0, 0, 1, 1, 0, 1 };

    uint quadsPerSurface = (kConeSlices - 1) * kConeSides;
    uint cellIndex = vertexId / 6;
    uint corner = vertexId % 6;
    uint surfaceIndex = cellIndex / quadsPerSurface; // 0 = 侧面, 1 = 球形封口
    uint cellInSurface = cellIndex % quadsPerSurface;
    uint slice = cellInSurface / kConeSides;
    uint side = cellInSurface % kConeSides;
    uint nextSide = (side + 1) % kConeSides;

    uint sliceOffset = surfaceIndex == 0 ? kSideSliceOffset[corner] : kCapSliceOffset[corner];
    uint useNextSide = surfaceIndex == 0 ? kSideUseNextSide[corner] : kCapUseNextSide[corner];
    uint sliceIdx = slice + sliceOffset;
    uint sideIdx = useNextSide != 0 ? nextSide : side;

    // 侧面 N 边形按外接圆(跟真圆相切)而不是内接圆撑大, 只取决于边数, 跟灯光实际锥角无关,
    // 跟 UE FStencilConeIndexBuffer 同一个处理
    float invCosRadiansPerSide = 1.0 / cos(kPI / float(kConeSides));
    float angle = float(sideIdx) * (2.0 * kPI / float(kConeSides));
    float zRadius = attenuationRadius * cos(outerConeAngle);
    float tanConeAngle = tan(outerConeAngle);

    if (surfaceIndex == 0)
    {
        float distanceDownCone = zRadius * float(sliceIdx) / float(kConeSlices - 1);
        float sliceRadius = distanceDownCone * tanConeAngle * invCosRadiansPerSide;
        return float3(sliceRadius * sin(angle), sliceRadius * cos(angle), distanceDownCone);
    }
    else
    {
        float capRadius = zRadius * tanConeAngle;
        float unadjustedSliceRadius = capRadius * float(sliceIdx) / float(kConeSlices - 1);
        float sliceRadius = unadjustedSliceRadius * invCosRadiansPerSide;
        float zDistance = sqrt(max(attenuationRadius * attenuationRadius - unadjustedSliceRadius * unadjustedSliceRadius, 0.0));
        return float3(sliceRadius * sin(angle), sliceRadius * cos(angle), zDistance);
    }
}

// 单位球按经纬网格现算, 均匀缩放对任意半径都是精确变换, 不需要像锥体那样处理近似误差
float3 computeSphereLocalPosition(uint vertexId, float radius)
{
    const uint kSOffset[6] = { 0, 1, 0, 1, 1, 0 };
    const uint kROffset[6] = { 0, 0, 1, 0, 1, 1 };

    uint cellIndex = vertexId / 6;
    uint corner = vertexId % 6;
    uint s = cellIndex / kSphereRings;
    uint r = cellIndex % kSphereRings;

    uint sIdx = (s + kSOffset[corner]) % kSphereSides;
    uint rIdx = r + kROffset[corner];

    float theta = float(sIdx) * (2.0 * kPI / float(kSphereSides));
    float angle = float(rIdx) * (kPI / float(kSphereRings));
    float z = cos(angle);
    float rad = sin(angle);
    return float3(-rad * sin(theta), rad * cos(theta), z) * radius;
}

[shader("vertex")]
float4 VertexMain(uint vertexId : SV_VertexID) : SV_Position
{
    LightData lightData = sLightData[kPushConstants.lightIndex];
    InstanceData instanceData = sInstanceData[lightData.instanceIndex];

    float3 localPosition = lightData.type == LightType::Spot
        ? computeConeLocalPosition(vertexId, lightData.attenuationRadius, acos(clamp(lightData.cosOuterCone, -1.0, 1.0)))
        : computeSphereLocalPosition(vertexId, lightData.attenuationRadius);

    // 形状本地空间的锥轴/极轴是 +Z, 聚光灯锥轴约定是本地 -Z(见 Light.slang 的
    // evaluateSpotLightVector)——这里用绕 X 轴转 180 度的真旋转把 +Z 重映射到 -Z(行列式 +1,
    // 不翻转三角形环绕方向)。球体旋转对称, 套这个重映射也是无害的, 两种形状用同一条变换路径
    // 不需要分支
    float3 axisRemapped = float3(localPosition.x, -localPosition.y, -localPosition.z);
    float3x3 objectToViewRot = mul(instanceData.objectToWorldRot, uViewData.worldToViewRot);
    float3 positionVS = mul(axisRemapped, objectToViewRot) + kPushConstants.positionVS;
    return mul(float4(positionVS, 1), uViewData.viewToClip);
}

[shader("fragment")]
float4 FragmentMain(in float4 svPosition : SV_Position) : SV_Target
{
    float sceneDepth = SceneDepthDImage[uint2(svPosition.xy)].r;
    if (sceneDepth == 0)
    {
        return float4(0, 0, 0, 1);
    }

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
    {
        return float4(0, 0, 0, 1);
    }

    LightData lightData = sLightData[kPushConstants.lightIndex];
    InstanceData instanceData = sInstanceData[lightData.instanceIndex];

    float3 lightDirWS;
    float3 lightAttenuation;
    if (lightData.type == LightType::Spot)
    {
        evaluateSpotLightVector(lightData, instanceData, fragPosVS.xyz, uViewData, lightDirWS, lightAttenuation);
    }
    else // Point
    {
        evaluatePointLightVector(lightData, instanceData, fragPosVS.xyz, uViewData, lightDirWS, lightAttenuation);
    }

    BxDFContext ctx;
    initBxDFContext(sp, lightDirWS, ctx);
    float3 litColor = evaluateBxDF(sp, ctx);

    float3 attenuatedIntensity = lightData.intensity * lightAttenuation;
    float4 color = float4(attenuatedIntensity * litColor * getPreExposure(uViewData), 1);
    return color;
}
