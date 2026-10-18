#pragma once
#include <Common/Binding.slang>

// 各种光源类型的共用着色逻辑——跟 Binding.slang(只放资源绑定/数据结构声明)分开, 这里放实际的
// shading 计算函数。以后聚光灯的锥形衰减/IES profile 之类的东西也加在这个文件里。

// 点光源的方向(世界空间, 指向光源)和衰减系数——调用方负责保证只在 lightData.type == Point 时
// 调用。DirectLighting.slang(延迟/不透明)、LightVolume.slang、Translucent.slang(前向/半透明)
// 共用这份逻辑, 避免衰减公式改动时要同步改好几处而漏改。
//
// 位置现算, 不走 push constant: 跟 mesh 顶点着色器算 objectToView 平移分量用的是同一套
// calculateRelativeWorldPosition + cameraOrigin, 全程 view space, 不引入 tile 坐标之外的绝对
// 世界坐标, 半透明前向着色的 dynamic instance 场景下也一样适用。
//
// 衰减公式对应 UE DeferredLightingCommon.ush 的 GetLocalLightAttenuation() mobile/简化分支:
// windowed = saturate(1-(d/R)^4)² 是半径截断窗函数, 乘 1/(d²+ε) 补上真正的反平方衰减——完整版
// UE 是在面光源(sphere/rect)积分里隐式带 1/d², 我们这版没有面光源积分, 所以显式乘这一项。
// ε 是 UE mobile 分支的 1 个 Unreal world unit(1cm)的平方, 换算成本引擎的米制单位:
// (0.01m)² = 0.0001, 跟 C++ 侧 DirectionalLightProxy.cpp 的 kUnrealWorldUnitInMeters 是同一个换算
// 核心计算——只关心光源和着色点在世界空间的相对位置差(lightPositionRel/fragPositionRel
// 是同一个参考系下的两个位置, 参考系本身在哪不影响结果, 减出来的向量就是真实位移), 不绑定
// "当前相机的 view space"这个概念, 给没有相机的消费方(CardDirectLighting.slang, Card 没有
// view space)复用。evaluatePointLightVector()/evaluateSpotLightVector() 保留原来面向
// DirectLighting.slang/LightVolume.slang/Translucent.slang 的 view-space 签名, 转换出两个
// 相对位置之后转调这个函数, 衰减公式改动只需要改一处。
//
// 聚光灯的锥形遮罩(对应 UE DynamicLightingCommon.ush 的 SpotAttenuationMask()/
// SpotAttenuation(): mask 是 saturate((cosAngle-cosOuterCone)*invCosConeDifference), 平方
// 之后就是最终锥形衰减)按 lightData.type 直接判断是否叠加, 不用像原来那样让
// evaluateSpotLightVector() 单独包一层再调 evaluatePointLightVector()。锥轴用本地 -Z, 是
// 聚光灯专用的独立约定, 故意不跟 Transform::ForwardVector(本地 +Y, 摄像机/网格用的那个
// "前"轴)绑在一起——这样一个刚创建、旋转还是单位矩阵的聚光灯默认朝下(本引擎 +Z 朝上),
// 不需要额外套一层默认旋转
void evaluateLightVectorWSCore(
    in LightData lightData,
    in InstanceData lightInstanceData,
    in float3 lightPositionRel,
    in float3 fragPositionRel,
    out float3 lightDirWS,
    out float3 attenuation
)
{
    float3 toLight = lightPositionRel - fragPositionRel;
    float distSqr = dot(toLight, toLight);
    lightDirWS = normalize(toLight);

    // 衰减公式对应 UE DeferredLightingCommon.ush 的 GetLocalLightAttenuation() mobile/简化
    // 分支: windowed = saturate(1-(d/R)^4)² 是半径截断窗函数, 乘 1/(d²+ε) 补上真正的反平方
    // 衰减——完整版 UE 是在面光源(sphere/rect)积分里隐式带 1/d², 我们这版没有面光源积分,
    // 所以显式乘这一项。ε 是 UE mobile 分支的 1 个 Unreal world unit(1cm)的平方, 换算成
    // 本引擎的米制单位: (0.01m)² = 0.0001, 跟 C++ 侧 DirectionalLightProxy.cpp 的
    // kUnrealWorldUnitInMeters 是同一个换算
    float invRadiusSqr = 1.0 / max(lightData.attenuationRadius * lightData.attenuationRadius, 1e-8);
    float normalizedDistSqr = distSqr * invRadiusSqr;
    float falloff = saturate(1.0 - normalizedDistSqr * normalizedDistSqr);
    float windowed = falloff * falloff;
    static const float kPointLightDistanceEpsilonSq = 0.0001;
    attenuation = float3(windowed / (distSqr + kPointLightDistanceEpsilonSq));

    if (lightData.type == LightType::Spot)
    {
        float3 spotAxisWS = mul(float3(0, 0, -1), lightInstanceData.objectToWorldRot);
        float cosAngle = dot(lightDirWS, -spotAxisWS);
        float spotMask = saturate((cosAngle - lightData.cosOuterCone) * lightData.invCosConeDifference);
        attenuation *= spotMask * spotMask;
    }
}

// 位置现算, 不走 push constant: 跟 mesh 顶点着色器算 objectToView 平移分量用的是同一套
// calculateRelativeWorldPosition + cameraOrigin, 全程 view space, 不引入 tile 坐标之外的
// 绝对世界坐标, 半透明前向着色的 dynamic instance 场景下也一样适用。fragPosVS 转回世界
// 朝向的相对位置用 transpose(worldToViewRot)(view->world 的逆旋转, 旋转矩阵转置即逆)
void evaluatePointLightVector(
    in LightData lightData,
    in InstanceData lightInstanceData,
    in float3 fragPosVS,
    in ViewData viewData,
    out float3 lightDirWS,
    out float3 attenuation
)
{
    float3 lightPositionRel = calculateRelativeWorldPosition(lightInstanceData.objectToWorld.getTranslation(), viewData.cameraOrigin);
    float3 fragPositionRel = mul(fragPosVS, transpose(viewData.worldToViewRot));
    evaluateLightVectorWSCore(lightData, lightInstanceData, lightPositionRel, fragPositionRel, lightDirWS, attenuation);
}

// 聚光灯 = 点光源的距离衰减再乘一个锥形遮罩, 现在两者都在 evaluateLightVectorWSCore() 里
// 一次算完(按 lightData.type 判断), 这里只是保留跟点光源一致的 view-space 签名给调用方用
void evaluateSpotLightVector(
    in LightData lightData,
    in InstanceData lightInstanceData,
    in float3 fragPosVS,
    in ViewData viewData,
    out float3 lightDirWS,
    out float3 attenuation
)
{
    evaluatePointLightVector(lightData, lightInstanceData, fragPosVS, viewData, lightDirWS, attenuation);
}
