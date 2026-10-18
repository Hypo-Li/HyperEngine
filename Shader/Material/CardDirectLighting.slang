// Card Surface Cache 直接光照——按 CardCapturePass 已经捕获好的每一页, 反解出每个 texel 的
// 世界坐标+法线(公式跟 CardCapture.slang 正向投影对称, 见下面 decodeCardTexel 的注释),
// 遍历场景全部光源(不做视锥/包围球剔除——Card 没有屏幕可见性这个概念, 剔除是纯优化, 见
// Lumen_GI_Roadmap.md 3.3 节排除项), 方向光阴影追 per-mesh MDF(用
// hyp::LumenScene::DirectLightingShadowTileGrid 筛出的候选), 点光源/聚光灯阴影追主相机
// 合成好的 GDF(固定用主相机, 见 Lumen_GI_Roadmap.md 对 UE PrimaryView 用法的调研结论),
// 写进 DirectLightingAtlas 的是纯入射光照(NdotL × 阴影 × 衰减 × 灯光强度), 不含 albedo/
// 自发光/1/PI 归一化——这几步统一挪到实际采样这张图集的地方做(目前是
// resolveGlobalDistanceFieldHit()), 跟 UE CombineFinalLighting() 的分工一致, 好处是以后
// Indirect Lighting 反弹可以直接复用这份纯光照数值, 不需要先把 albedo 除回去。
//
// DirectLightingShadowTileGrid 共享 buffer 布局(每盏方向光一段连续区间, 起始下标是
// LightData::shadowTileGridOffset): [originX(float), originY(float), cellSize(float),
// tileCountX(uint), tileCountY(uint), tileCandidateOffsets[tileCount+1](CSR, 末尾多存一个
// 哨兵 = 总候选数), tileCandidatePayload[总候选数](MeshInstanceDataBuffer 下标)]——跟
// hyp::LumenScene::updateDirectLightingShadowCandidates() 手动保持一致。
//
// SceneConstants 布局(见 CardDirectLightingPass.cpp): [0..2] referenceOrigin.tile(int3),
// [3..5] referenceOrigin.offset(float, asuint), [6] lightCount, [7] 保留
#include "../Common/Binding.slang"
// MeshShapeCard.slang 已经带 MeshCardDirection.slang(getMeshCardDirectionAxes), 不再单独
// include 一遍——Slang 的 #pragma once 按 include 路径原始文本去重, 同一个物理文件从两条
// 不同的相对路径各 include 一次会被当成两份不同的翻译单元, 导致 getMeshCardDirectionAxes
// 重复定义报错
#include "../Common/MeshShapeCard.slang"
#include "../Common/CardTexelDecode.slang"
#include "../Common/FloatQuantization.slang"
#include "../Common/SparseDistanceField.slang"
#include "../GlobalDistanceField/GlobalDistanceFieldQuery.slang"
#include "../Atmosphere/AtmosphereCommon.slang"
#include "Light.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

// 跟 hyp::kInvalidSurfaceCacheAtlasPageIndex(SurfaceCacheAtlas.h)必须保持一致
static const uint kInvalidSurfaceCacheAtlasPageIndex = 0xFFFFFFFFu;

[[vk::binding(0, 2)]]
StructuredBuffer<uint> MeshShapeData;

[[vk::binding(1, 2)]]
StructuredBuffer<MeshInstanceData> MeshInstanceDataBuffer;

[[vk::binding(2, 2)]]
Texture3D<float> SourceBrickAtlas;

[[vk::binding(3, 2)]]
StructuredBuffer<uint> GdfIndirection;

[[vk::binding(4, 2)]]
Texture3D<float> GdfAtlas;

[[vk::binding(5, 2)]]
SamplerState LinearSampler;

[[vk::binding(6, 2)]]
Texture2D<float4> CardNormalAtlas;

[[vk::binding(7, 2)]]
Texture2D<float> CardDepthAtlas;

// Binding.slang 里的 LightData/LightType 只声明结构体, 实际的 buffer 绑定由各自需要的 pass
// 自己在 set2 里声明——跟 DirectLighting.slang 的 sLightData 是同一个理由, 不占公共 set1
[[vk::binding(9, 2)]]
StructuredBuffer<LightData> sLightData;

[[vk::binding(10, 2)]]
StructuredBuffer<uint> SceneConstants;

[[vk::binding(11, 2)]]
StructuredBuffer<uint> DirectLightingShadowTileGridBuffer;

// 跟 hyp::SurfaceCacheAtlas::mDirectLightingAtlas 的 B10G11R11_UFLOAT_PACK32 对应, 见
// CMake/CompilePassShader.cmake 里 "r11f_g11f_b10f" -> B10G11R11_UFLOAT_PACK32 的映射表
[[vk::binding(12, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> DirectLightingAtlas;

// 太阳光穿过大气到达 Card texel 的透过率, 跟 DirectLighting.slang 同一张 TransmittanceLut、
// 同一套用法——见下面 ComputeMain 里方向光分支的注释。没有大气的场景绑
// DefaultResources 的占位资源(全零 buffer + 全白纹理), 靠 topRadiusKm<=bottomRadiusKm
// 退化跳过, 跟 DirectLighting.slang 同一个理由
[[vk::binding(13, 2)]]
ConstantBuffer<AtmosphereParameters> Atmosphere;

[[vk::binding(14, 2)]]
Texture2D<float4> TransmittanceLutTexture;

// 跟 CardDirectLightingPass.cpp 里的 CardDirectLightingConstants 逐字段对应, 总大小压在
// push constant 64 字节硬上限(rhi::kImmediateDataSize)——手动按标量摆放, 不用 float3/int3,
// 理由跟 CardCapture.slang/MeshSDFScene.slang 的 push constant 一致
struct CardDirectLightingConstants
{
    float cardCenterX, cardCenterY, cardCenterZ;
    float cardHalfExtentsX, cardHalfExtentsY, cardHalfExtentsZ;
    uint directionIndex;
    // mCardPageTable 里存的打包值本身(不是预先解码好的像素原点)——shader 端用
    // decodeCardPageAddress() 自己解出像素原点+实际尺寸, 省下单独传 pixelExtentX/Y 需要的
    // 4 字节, push constant 总大小压在 64 字节硬上限(rhi::kImmediateDataSize), 挤不出
    // 额外空间再加两个字段
    uint packedAddress;
    uint pageIndexX, pageIndexY;        // 这一页在这张 card 自己的页网格里的位置
    uint pageCountX, pageCountY;        // 这张 card 总共有几页
    uint instanceIndex;
    int gdfAtlasBrickGridExtent;
    int sourceAtlasBrickGridExtent;
};
[[vk::push_constant]]
CardDirectLightingConstants PC;

WorldCoord loadReferenceOrigin()
{
    WorldCoord origin;
    origin.tile = int3(asint(SceneConstants[0]), asint(SceneConstants[1]), asint(SceneConstants[2]));
    origin.offset = asfloat(uint3(SceneConstants[3], SceneConstants[4], SceneConstants[5]));
    return origin;
}

// 跟 hyp::DirectionalLightProxy::computeLightSpaceBasis() 手动保持一致——tile grid 的
// origin/cellSize 是 CPU 端用这套基向量投影出来的, GPU 这边重新投影世界位置时必须用完全
// 同一套基向量, 否则算出来的 tile 坐标系跟共享 buffer 里存的对不上
void computeLightSpaceBasis(float3 direction, out float3 forward, out float3 right, out float3 up)
{
    float3 upHint = abs(direction.z) == 1.0 ? float3(0, 1, 0) : float3(0, 0, 1);
    forward = normalize(direction);
    right = normalize(cross(forward, upHint));
    up = cross(right, forward);
}

// 跟 UE r.LumenScene.DirectLighting.MeshSDF.ShadowRayBias 默认值(2 cm)一致
static const float kMeshSDFShadowRayBias = 0.02;

// 跟 UE r.LumenScene.DirectLighting.GlobalSDF.ShadowRayBias 默认值一致
static const float kGlobalSDFShadowRayBias = 1.0;

// 跟 UE FPostProcessSettings::LumenMaxTraceDistance 默认值(20000 cm)一致
static const float kLumenMaxTraceDistance = 200.0;

// 跟 UE TraceOffscreenShadows() 传给 ShadowRayTraceThroughCulledObjects() 的 MaxSphereRadius(100 cm)一致
static const float kMeshSDFShadowMaxSphereRadius = 1.0;

// 照抄 UE GetCardBiasForShadowing()
float getCardBiasForShadowing(float3 lightDirWS, float3 normalWS, float biasValue)
{
    return biasValue + 2.0 * biasValue * saturate(1.0 - dot(lightDirWS, normalWS));
}

// 方向光 MDF 阴影: 在这盏光的 tile grid 里查这个世界位置落在哪个 tile, 取候选实例列表,
// 对每个候选的 SparseDistanceFieldObject 追一条阴影射线, 任意命中就算被遮挡。tile grid 的
// origin/cellSize 是相对 referenceOrigin 投影出来的光源空间坐标(见 hyp::LumenScene::
// updateDirectLightingShadowCandidates() 的注释), 这里必须用同一个 referenceOrigin 重新
// 投影一次世界位置才能对上同一套坐标系。追踪参数照抄 UE TraceOffscreenShadows() 的 OFFSCREEN_SHADOWING_TRACE_MESH_SDF 分支
float traceDirectionalShadow(WorldCoord fragWorldPos, float3 normalWS, float3 lightDirWS, uint shadowTileGridOffset, WorldCoord referenceOrigin, float3 lightRight, float3 lightUp)
{
    if (shadowTileGridOffset == kInvalidSurfaceCacheAtlasPageIndex)
        return 1.0; // 没有候选数据(没有活跃相机, 场景没有 SDF 数据, 或者共享 buffer 容量不够), 视为不遮挡

    float3 fragPositionRel = calculateRelativeWorldPosition(fragWorldPos, referenceOrigin);
    float coordRight = dot(fragPositionRel, lightRight);
    float coordUp = dot(fragPositionRel, lightUp);

    float originX = asfloat(DirectLightingShadowTileGridBuffer[shadowTileGridOffset + 0]);
    float originY = asfloat(DirectLightingShadowTileGridBuffer[shadowTileGridOffset + 1]);
    float cellSize = asfloat(DirectLightingShadowTileGridBuffer[shadowTileGridOffset + 2]);
    uint tileCountX = DirectLightingShadowTileGridBuffer[shadowTileGridOffset + 3];
    uint tileCountY = DirectLightingShadowTileGridBuffer[shadowTileGridOffset + 4];

    int tileX = clamp(int(floor((coordRight - originX) / cellSize)), 0, int(tileCountX) - 1);
    int tileY = clamp(int(floor((coordUp - originY) / cellSize)), 0, int(tileCountY) - 1);
    uint tileIndex = uint(tileY) * tileCountX + uint(tileX);

    const uint headerWordCount = 5;
    uint offsetsBase = shadowTileGridOffset + headerWordCount;
    uint candidateStart = DirectLightingShadowTileGridBuffer[offsetsBase + tileIndex];
    uint candidateEnd = DirectLightingShadowTileGridBuffer[offsetsBase + tileIndex + 1]; // CSR 哨兵, 见共享 buffer 布局注释
    uint payloadBase = offsetsBase + (tileCountX * tileCountY + 1);

    WorldCoord rayStartWS = fragWorldPos;
    rayStartWS.offset += lightDirWS * getCardBiasForShadowing(lightDirWS, normalWS, kMeshSDFShadowRayBias);

    float minConeVisibility = 1.0;
    for (uint i = candidateStart; i < candidateEnd; ++i)
    {
        uint meshInstanceIndex = DirectLightingShadowTileGridBuffer[payloadBase + i];
        MeshInstanceData candidate = MeshInstanceDataBuffer[meshInstanceIndex];
        InstanceData candidateInstanceData = sInstanceData[candidate.instanceIndex];

        SparseDistanceFieldObject obj = loadSparseDistanceFieldObject(MeshShapeData, candidate.meshShapeDataOffset, PC.sourceAtlasBrickGridExtent);

        WorldCoord candidateTranslation = candidateInstanceData.objectToWorld.getTranslation();
        float3 rayStartRel = calculateRelativeWorldPosition(rayStartWS, candidateTranslation);
        float3 rayStartOS = mul(rayStartRel, candidateInstanceData.worldToObjectRS);
        float3 rayEndOS = mul(rayStartRel + lightDirWS * kLumenMaxTraceDistance, candidateInstanceData.worldToObjectRS);

        float3x3 rs = candidateInstanceData.objectToWorldRS;
        float worldToLocalScale = 1.0 / max(length(rs[0]), max(length(rs[1]), length(rs[2])));

        minConeVisibility = min(minConeVisibility, shadowRayTraceSparseDistanceFieldObject(
            obj, MeshShapeData, SourceBrickAtlas, LinearSampler,
            rayStartOS, rayEndOS, worldToLocalScale,
            /*tanLightAngle*/ 0.0, /*minSphereRadius*/ 0.0, kMeshSDFShadowMaxSphereRadius,
            /*selfShadowBias: UE UStaticMesh::DistanceFieldSelfShadowBias 默认值*/ 0.0,
            /*expandSurface*/ true));

        if (minConeVisibility < 0.01)
            return 0.0;
    }
    return minConeVisibility;
}

// 参数照抄 UE TraceOffscreenShadows() 的 OFFSCREEN_SHADOWING_TRACE_GLOBAL_SDF 分支
bool tracePointOrSpotShadow(WorldCoord fragWorldPos, float3 normalWS, float3 lightDirWS, float distanceToLight, WorldCoord referenceOrigin)
{
    GlobalDistanceFieldCascadeInfo cascades[kGlobalDistanceFieldCascadeCount];
    for (uint i = 0; i < uint(kGlobalDistanceFieldCascadeCount); ++i)
        cascades[i] = globalDistanceFieldCascadeInfoAtIndex(referenceOrigin, i);

    GlobalDistanceFieldTraceInput input = setupGlobalDistanceFieldTraceInput(fragWorldPos, lightDirWS, 0.0, distanceToLight, 1.0, 1.0);
    input.voxelSizeRelativeBias = getCardBiasForShadowing(lightDirWS, normalWS, kGlobalSDFShadowRayBias);
    input.voxelSizeRelativeRayEndBias = kGlobalSDFShadowRayBias;

    GlobalDistanceFieldTraceResult result = traceGlobalDistanceField(
        cascades, GdfIndirection, GdfAtlas, LinearSampler, PC.gdfAtlasBrickGridExtent, input);
    return result.hit;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    CardPageRegion pageRegion = decodeCardPageAddress(PC.packedAddress);

    uint2 pixelInPage = groupId.xy * 8 + localId.xy;
    if (pixelInPage.x >= uint(pageRegion.pixelExtent.x) || pixelInPage.y >= uint(pageRegion.pixelExtent.y))
        return;

    CardTexelInfo texel = decodeCardTexel(
        pixelInPage,
        uint2(PC.pageIndexX, PC.pageIndexY),
        uint2(PC.pageCountX, PC.pageCountY),
        uint2(pageRegion.pixelOrigin),
        uint2(pageRegion.pixelExtent),
        PC.directionIndex,
        float3(PC.cardCenterX, PC.cardCenterY, PC.cardCenterZ),
        float3(PC.cardHalfExtentsX, PC.cardHalfExtentsY, PC.cardHalfExtentsZ),
        PC.instanceIndex,
        CardDepthAtlas,
        CardNormalAtlas);
    int2 atlasTexelCoord = pageRegion.pixelOrigin + int2(pixelInPage);
    if (!texel.valid)
    {
        DirectLightingAtlas[atlasTexelCoord] = float4(0, 0, 0, 1);
        return;
    }

    WorldCoord referenceOrigin = loadReferenceOrigin();
    uint lightCount = SceneConstants[6];

    float3 litColor = float3(0.0);
    for (uint lightIndex = 0; lightIndex < lightCount; ++lightIndex)
    {
        LightData lightData = sLightData[lightIndex];
        InstanceData lightInstanceData = sInstanceData[lightData.instanceIndex];
        if ((lightInstanceData.flags & InstanceFlags::Visible) == InstanceFlags::None)
            continue;

        float3 lightDirWS;
        float3 attenuation;
        float shadowFactor;

        if (lightData.type == LightType::Directional)
        {
            lightDirWS = mul(float3(0, 0, 1), lightInstanceData.objectToWorldRot);
            attenuation = float3(1.0);

            // 太阳光穿过大气到达这个 texel 的透过率——跟 DirectLighting.slang 同一张
            // TransmittanceLut、同一套 (高度, 光源天顶角) 参数化, 只是这里直接用
            // texel.worldPos(已经是 decodeCardTexel() 反解出来的 WorldCoord)算高度, 不需要
            // 像 DirectLighting.slang 那样从相机+视线方向+距离现推。topRadiusKm<=bottomRadiusKm
            // (没有大气/占位资源)时退化跳过, 理由见 Atmosphere/TransmittanceLutTexture 绑定处的注释
            if (Atmosphere.topRadiusKm > Atmosphere.bottomRadiusKm)
            {
                float3 texelWorldPosKm = getWorldPosUnclamped(Atmosphere, texel.worldPos);
                float texelHeightKm = length(texelWorldPosKm);
                float3 upVector = texelWorldPosKm / texelHeightKm;
                float lightZenithCos = dot(lightDirWS, upVector);
                attenuation *= getTransmittance(Atmosphere, TransmittanceLutTexture, LinearSampler, lightZenithCos, texelHeightKm)
                    * getEarthShadow(lightDirWS, upVector);
            }

            // tile grid 的 lightRight/lightUp 是 CPU 端用 hyp::DirectionalLightProxy::
            // getDirection()(= -worldUp, "光线传播方向", 即 DirectionalLight::onTransformUpdate()
            // 里的 -transform.getWorldUpVector())投出来的, 不是 lightDirWS(= +worldUp, "指向
            // 光源方向", 给 NdotL/阴影射线方向用的)——两者刚好反号。computeLightSpaceBasis()
            // 的 right 轴符号跟着输入方向的符号翻转(forward 反号会让 cross(forward,upHint)
            // 也反号), up 轴则不受影响, 直接传 lightDirWS 进去算出来的 lightRight 跟 CPU 建
            // grid 时用的 lightRight 差了一个负号, 表现为 tile 坐标系的 right 轴左右颠倒——
            // 必须传 -lightDirWS(还原成 CPU 那套"光线传播方向"的约定)才能对上
            float3 lightForward, lightRight, lightUp;
            computeLightSpaceBasis(-lightDirWS, lightForward, lightRight, lightUp);
            shadowFactor = traceDirectionalShadow(texel.worldPos, texel.normalWS, lightDirWS, lightData.shadowTileGridOffset, referenceOrigin, lightRight, lightUp);
        }
        else
        {
            WorldCoord lightTranslation = lightInstanceData.objectToWorld.getTranslation();
            float3 lightPositionRel = calculateRelativeWorldPosition(lightTranslation, texel.worldPos);
            evaluateLightVectorWSCore(lightData, lightInstanceData, lightPositionRel, float3(0.0), lightDirWS, attenuation);

            float distanceToLight = length(lightPositionRel);
            shadowFactor = tracePointOrSpotShadow(texel.worldPos, texel.normalWS, lightDirWS, distanceToLight, referenceOrigin) ? 0.0 : 1.0;
        }

        if (shadowFactor <= 0.0)
            continue;

        float ndotl = saturate(dot(texel.normalWS, lightDirWS));
        litColor += ndotl * attenuation * lightData.intensity * shadowFactor;
    }

    DirectLightingAtlas[atlasTexelCoord] = float4(
        quantizeForFloatRenderTarget(litColor, int3(atlasTexelCoord, int(uFrameData.frameIndex % 8u))), 1.0);
}
