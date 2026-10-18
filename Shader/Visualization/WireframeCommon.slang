#pragma once
#include <Common/Binding.slang>

// box/sphere/capsule/cylinder/lightCone 这 5 种线框图元共用: 都是从 DefaultResources 烘焙的
// 单位几何(Line_List 拓扑)读一个 POSITION0 顶点, 按各自的 halfExtents/radius/halfHeight/
// range+halfAngle 做非均匀缩放得到局部坐标, 再统一走 calculateObjectToView 变到 view space
struct VaryingsWireframe
{
    float4 svPosition : SV_Position;
    float4 color : COLOR0;
};

float4 computeWireframeClipPosition(float3 localPos, WorldMatrix transform)
{
    float4x4 objectToView;
    calculateObjectToView(transform, uViewData.worldToViewRot, uViewData.cameraOrigin, objectToView);
    float4 positionVS = mul(float4(localPos, 1), objectToView);
    // 用不带 TAA 抖动的矩阵——Visualization 这个 pass 排在 TAA resolve 之后画, 没有后续步骤把
    // 抖动平均掉, 用带抖动的 viewToClip 会导致每帧跟着亚像素偏移抖动
    return mul(positionVS, uViewData.viewToClipUnjittered);
}

// ignoreDepth: 见 Visualization/Icon.slang 里同名字段的注释——排序发生在 CPU 侧
// VisualizationRenderData::build() 的 orderByDepthGroup, 这里只负责钉死深度值
float4 applyIgnoreDepth(float4 svPosition, uint ignoreDepth)
{
    if (ignoreDepth != 0)
        svPosition.z = svPosition.w;
    return svPosition;
}

// 注意: FragmentMain 本身不放在这里——PassShaderCodegen 只按"顶层 .slang 文件里直接
// 写没写 [shader("fragment")]"来判断这个 pass 有没有 fragment stage, 不会跟着 #include
// 展开去找, 挂在共享头文件里的入口点会被判定成"不存在", 每个形状各自的 .slang 文件末尾
// 都要自己重复这四行
