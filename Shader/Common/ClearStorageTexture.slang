#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)

[[vk::binding(0, 0)]]
[format(STRINGIFY(STORAGE_FORMAT))]
WTexture2D<STORAGE_TEXEL> DstTexture;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    DstTexture.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;
    DstTexture.Store(id.xy, STORAGE_TEXEL(0));
}
