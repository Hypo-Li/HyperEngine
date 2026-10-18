// 把源深度纹理原样写进目标深度附件——纯粹的深度拷贝, 没有颜色输出。给需要"跟场景深度做硬件
// 深度测试, 但又要在同一个 render pass 里采样另一份深度做手动重建"这种场景用: 拷贝出来的这份是
// 完全独立的 VkImage, 跟源纹理不存在任何子视图/别名关系, 可以放心当深度附件测试, 不会和源纹理
// 自己的采样用途在同一个 pass 里产生 layout 冲突。见 DirectLightingPass.cpp 里光源体积球那段的
// 用法和注释。

[[vk::binding(0, 0)]]
DepthTexture2D SourceDepthImage;

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

struct FSOutput
{
    float depth : SV_Depth;
};

// 裸标量 "float FragmentMain(...) : SV_Depth" 在 Slang 的 WGSL 后端会被错误降级成
// @location(0) 颜色输出而非 @builtin(frag_depth)(已用 slangc -target wgsl 验证), 必须用结构体包一层。
[shader("fragment")]
FSOutput FragmentMain(in float4 svPosition : SV_Position)
{
    FSOutput output;
    output.depth = SourceDepthImage[uint2(svPosition.xy)].r;
    return output;
}
