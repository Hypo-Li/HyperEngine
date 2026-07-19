#pragma once
#include "WorldCoordinate.slang"

// 材质顶点位置偏移(vertex position offset, 对应 UE 的 World Position Offset)求值时可用的
// 输入。evaluateVertexPositionOffset() 的返回值语义是世界空间的位移量(纯方向, 不含平移),
// 不是绝对坐标——引擎的大世界坐标用 tile+offset 相对相机重定基(见 WorldCoordinate.slang 的
// WorldCoord/calculateLocalToView), 从不物化出一个绝对的世界坐标, 所以偏移量只能表达成方向,
// 由调用方(VertexMain)按 ViewData.worldToViewRot 转换到 view 空间后叠加到 positionVS。
// 用世界空间而不是局部空间表达偏移, 是为了让同一种效果(比如风吹摆动)在不同旋转/缩放的实例上
// 摆动方向一致, 不会被每个实例各自的局部坐标系带偏。
struct VertexPositionOffsetInput
{
    float3 positionLS;      // 顶点原始局部空间坐标(偏移前), 供"离物体中心越远偏移越大"这类效果使用
    float3 normalLS;        // 顶点局部空间法线, 供"沿法线方向偏移"这类效果使用
    float time;              // uFrameData.wrappedTime, 和 MaterialInput.time 同一个来源
    WorldMatrix localToWorld; // instanceData.localToWorld, 需要按实例差异化摆动时用
};
