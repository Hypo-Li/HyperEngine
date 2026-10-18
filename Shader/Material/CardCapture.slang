// Mesh Card 捕获: 把网格实际材质从某张 card 的正交视角画到 SurfaceCacheAtlas 共享图集的对应
// 页里, 只输出 albedo, 不做光照。
//
// 跟 Opaque.slang 最大的区别: 顶点/片元变换不经过 Common/Binding.slang 里的 uViewData/
// sInstanceData——这个 Pass 一次只画一个场景实例的一张 card, 不需要批量 draw 的 sIndices
// 间接索引, 也不需要大世界坐标的 tile+offset 重定基(那一套是为了给"可能离原点很远的相机"
// 保证精度, card 捕获完全用不上相机这个概念)。整个变换直接在网格自己的局部空间里完成:
//
//   positionOS --(减 card 中心、投影到 card 自己的三个轴)--> card 视图空间 --(除以半宽/半高/
//   深度范围)--> 裁剪空间
//
// 相机(=card)的位置/朝向/正交视锥宽高全部由 card 的局部空间数据(center/halfExtents/
// directionIndex)决定, 通过 push constant 传入, 不需要 ViewDataManager 分配 viewId、
// 不需要构造 SceneView、也不需要每次捕获前后维护 view 生命周期。
//
// 仍然要 #include Binding.slang: 材质模板生成的 evaluateMaterial() 实现(makeShaderSource()
// 拼接进来的那部分)采样贴图时引用的是这里面声明的 BindlessTexture2Ds/BindlessSamplers 等 bindless 数组符号,
// 这些是全局 set0 数据, 跟"要不要用 uViewData/sInstanceData"无关——引擎在 FrameContext 里
// 每帧开头就已经统一把 set0 绑好了(不区分是不是这个 Pass 自己需要), 这里只是补上 Slang
// 编译期需要的符号声明, 不代表又绕回去依赖每相机的 ViewData
#include <Common/Binding.slang>
#include <Common/MaterialIO.slang>
#include <Common/VertexPositionOffset.slang>
// 不直接 include MeshCardDirection.slang: Slang 的 #pragma once 按 include 路径原文去重,
// 同一个文件从两种拼写各 include 一次会被当成两份翻译单元, getMeshCardDirectionAxes 会重定义
#include <Common/MeshShapeCard.slang>

[VERTEX_LAYOUT_IMPLEMENTATION]

// 手动按标量逐个摆放, 不用 float3/int3——跟 MeshSDFScene.slang 里 push constant 同一个
//教训: HLSL/Slang 的 cbuffer/push_constant 打包规则里 float3 后面紧跟一个标量会自动拼进
// 同一个 16 字节槽位, 但 C++ 那边(CardCapturePass.cpp)如果直接抄 float3/int 逐字段声明,
// 两边的隐式打包规则不一定完全对得上, 显式摆成互相独立的标量最不容易出错。
//
// 一张 card 如果分辨率超过一页, 会拆成 pageCountX*pageCountY 次单独捕获(照抄 UE 的
// "CardPage 是渲染最小单位"), pageIndexX/Y 是这次捕获画的是第几页——用来把整张 card 的
// NDC 范围([-1,1])缩小成这一页对应的子区间, 每一页各自拿到自己的[-1,1], 互不影响。
struct CardCapturePushConstants
{
    float cardCenterX, cardCenterY, cardCenterZ; // card 中心, 网格局部空间
    float cardHalfExtentsX, cardHalfExtentsY, cardHalfExtentsZ; // 沿 card 自己三个轴(见 getMeshCardDirectionAxes)的半宽, 网格局部空间
    uint directionIndex; // 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z, 跟 hyp::getMeshCardDirectionAxes() 同一份约定
    uint pageIndexX;
    uint pageIndexY;
    uint pageCountX;
    uint pageCountY;
    // 这一页在图集里实际占用的宽高(texel)——子页分配的卡这里会小于 kSurfaceCachePageSize,
    // 必须用这个而不是编译期常量做 NDC<->texel 换算, 否则子页卡的内容会铺满整页, 画到
    // 共享同一物理页的别的卡的区域里, 跟 CardCapturePass.cpp 的 CardCaptureConstants
    // 逐字段对应
    uint pixelExtentX;
    uint pixelExtentY;
};
[[vk::push_constant]]
CardCapturePushConstants PC;

struct VSOuput
{
    float4 svPosition : SV_Position;
    float3 normalOS : TEXCOORD0;
    float4 tangentOS : TEXCOORD1;
    // card 自己的正交相机(=cardCenter)是这条 Pass 唯一有意义的"相机", 直接照抄 VertexMain
    // 算好的 positionOS - cardCenter, 不接 mi.positionRWS 惯常的"绝对世界坐标定向、相机相对
    // 原点"那套语义(见下面 makeMaterialInput 的注释)
    float4 cardRelativePositionOS : TEXCOORD2;
#if VERTEX_HAS_COLOR
    float4 color : TEXCOORD3;
#endif
#if VERTEX_HAS_TEXCOORD0
    float2 texCoord0 : TEXCOORD4;
#endif
#if VERTEX_HAS_TEXCOORD1
    float2 texCoord1 : TEXCOORD5;
#endif
#if VERTEX_HAS_TEXCOORD2
    float2 texCoord2 : TEXCOORD6;
#endif
#if VERTEX_HAS_TEXCOORD3
    float2 texCoord3 : TEXCOORD7;
#endif
};

// 材质求值不需要真的处在"世界空间"——这条 Pass 里从头到尾都在网格自己的局部空间/card
// 视图空间打转, 从没经过世界空间。normalWS/tangentWS/viewDirWS/positionRWS 这几个字段名字是
// 跟 Opaque.slang 共用的通用结构体(MaterialIO.slang), 这里塞进去的实际是局部空间向量——材质
// 函数只做向量代数(法线贴图混合、菲涅尔之类), 不关心具体是哪个坐标系, 只要求调用方自己内部
// 一致, 这里全程用局部空间就是自洽的。positionRWS 同理: 这条 Pass 每次 draw call 的"相机"就是
// push constant 传入的 card 中心, 所以直接用 positionOS - cardCenter, 不是真正的世界坐标。
MaterialInput makeMaterialInput(in VSOuput vsout, in float3 viewDirOS)
{
    MaterialInput mi;
    mi.twoSidedSign = 1.0;
    mi.normalWS = normalize(vsout.normalOS);
    mi.tangentWS = float4(normalize(vsout.tangentOS.xyz), vsout.tangentOS.w);
#if VERTEX_HAS_COLOR
    mi.color = vsout.color;
#else
    mi.color = float4(1);
#endif

#if VERTEX_HAS_TEXCOORD0
    mi.texCoords[0] = vsout.texCoord0;
#else
    mi.texCoords[0] = float2(0, 0);
#endif
#define LAST_TEXCOORD_INDEX 0

#if VERTEX_HAS_TEXCOORD1
    mi.texCoords[1] = vsout.texCoord1;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 1
#else
    mi.texCoords[1] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if VERTEX_HAS_TEXCOORD2
    mi.texCoords[2] = vsout.texCoord2;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 2
#else
    mi.texCoords[2] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if VERTEX_HAS_TEXCOORD3
    mi.texCoords[3] = vsout.texCoord3;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 3
#else
    mi.texCoords[3] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

    mi.viewDirWS = viewDirOS;
    mi.positionRWS = vsout.cardRelativePositionOS;
    // 世界空间就是局部空间、相机是 card 中心, 平移取 cardCenter 才能把 positionRWS 还原成局部位置
    mi.relativeWorldToObject = float3x4(
        float4(1, 0, 0, PC.cardCenterX),
        float4(0, 1, 0, PC.cardCenterY),
        float4(0, 0, 1, PC.cardCenterZ)
    );
    mi.time = uFrameData.wrappedTime;
    return mi;
}

[MATERIAL_IMPLEMENTATION]

[shader("vertex")]
VSOuput VertexMain(in VSInput input)
{
    float3 axisX, axisY, axisZ;
    getMeshCardDirectionAxes(PC.directionIndex, axisX, axisY, axisZ);

    const float3 cardCenter = float3(PC.cardCenterX, PC.cardCenterY, PC.cardCenterZ);
    const float3 cardHalfExtents = float3(PC.cardHalfExtentsX, PC.cardHalfExtentsY, PC.cardHalfExtentsZ);

    // 必须取 abs(): halfWidth/halfHeight 表示"宽度", 跟 axisX/axisY 具体朝哪边无关——
    // AxisY 在负方向侧取反是为了让镜像生效(见 getMeshCardDirectionAxes 的注释), 如果
    // halfHeight 不强制取正, 它会跟着 axisY 一起变号, cardNdcY = cardViewPos.y/halfHeight
    // 这个比值里分子分母的负号就抵消了, 镜像效果无论如何都出不来
    const float halfWidth = abs(dot(axisX, cardHalfExtents));
    const float halfHeight = abs(dot(axisY, cardHalfExtents));
    // cardHalfExtents 分量总是非负, 但 axisZ 对负方向(-X/-Y/-Z)是带负号的 one-hot 向量,
    // dot(axisZ, cardHalfExtents) 算出来是负数——之前直接 max(那个负数, 1e-5) 恒等于
    // 1e-5, depthExtent 被错误地钳到接近 0, 导致 ndcZ 对任何非零深度都爆出裁剪空间,
    // 三角形全部被裁掉、什么都画不出来。这也是唯一的根因: +X/+Y/+Z 三个方向能正常画出来,
    // -X/-Y/-Z 三个方向全部空白, 跟"正负方向"精确对应, 不是背面剔除/环绕方向的问题
    //
    // 深度方向的裁剪余量已经在烘焙阶段(MeshCard.cpp 的 computeCardOBB)加进
    // cardHalfExtents 里了, 这里不再重复加——两处都加会让余量翻倍、且分散在两个文件里
    // 不好维护
    const float depthExtent = max(abs(dot(axisZ, cardHalfExtents)), 1e-5);

    const VertexGeometry geo = getVertexGeometry(input);
    const float3 positionOS = geo.position;
    const float3 rel = positionOS - cardCenter;

    // card 视图空间: x/y 是面内坐标, z 是沿朝外法线(axisZ)的深度——card 自己的相机放在
    // z=+depthExtent 处朝 -axisZ 看, 所以 z 越大越靠近相机(近平面), 越小越远(远平面)
    const float3 cardViewPos = float3(dot(rel, axisX), dot(rel, axisY), dot(rel, axisZ));

    // 整张 card 的 NDC 范围先按老办法算(不管有几页, 这两个值范围始终是 card 整体的
    // [-1,1]), 再按这一页在 card 里的子区间映射到这一页的 texel 坐标区间——一张 card 拆成
    // 多页时, 每一页各自独立捕获、独立当成一次完整的正交渲染, 互不干扰。
    const float cardNdcX = cardViewPos.x / halfWidth;
    const float cardNdcY = cardViewPos.y / halfHeight;

    const float segmentWidth = 2.0 / float(PC.pageCountX);
    const float segmentHeight = 2.0 / float(PC.pageCountY);
    const float segmentStartX = -1.0 + float(PC.pageIndexX) * segmentWidth;
    const float segmentStartY = -1.0 + float(PC.pageIndexY) * segmentHeight;

    float2 minBorder, maxBorder;
    cardPageTexelBorders(uint2(PC.pageIndexX, PC.pageIndexY), uint2(PC.pageCountX, PC.pageCountY), minBorder, maxBorder);

    // 子页分配的卡 pageCountX/Y 恒为 1, cardPageTexelBorders 算出来的 minBorder/maxBorder
    // 恒为 0, pixelExtent 就是这张卡实际分到的宽高; 跨页的卡 pixelExtent 恒为
    // kSurfaceCachePageSize(跨页不进子页 bin, 见 SurfaceCachePageAllocator::allocate()),
    // 两种情况这里都不需要分支, 直接用运行时的实际尺寸算
    const float2 pageExtent = float2(float(PC.pixelExtentX), float(PC.pixelExtentY));

    const float2 segmentT = float2(
        (cardNdcX - segmentStartX) / segmentWidth,
        (cardNdcY - segmentStartY) / segmentHeight);
    const float2 pageTexelCoord =
        float2(segmentT.x, 1.0 - segmentT.y) * (pageExtent - minBorder - maxBorder) + minBorder;

    const float ndcX = 2.0 * pageTexelCoord.x / pageExtent.x - 1.0;
    const float ndcY = 1.0 - 2.0 * pageTexelCoord.y / pageExtent.y;
    // [-depthExtent, +depthExtent] -> [0, 1](近=1 远=0, 配合 depthCompareOp=Greater_Equal 的
    // reversed-Z 写法: cardViewPos.z 越大越靠近朝外的一侧(离观察者越近), 必须映射到越大的
    // ndcZ, Greater_Equal 深度测试才会在自遮挡(同一像素被多层三角形覆盖)时正确保留朝外的
    // 那一层——写成 0.5 - z/(2*depthExtent) 是反的, 会让"更远"的内容算出更大的 ndcZ,
    // 深度测试选错赢家, 表现为近处本该可见的内容被更远的内容错误覆盖掉。深度范围不按页
    // 拆分, 每一页看到的深度跟整张 card 一致
    // 只对落在深度范围内的值躲开 0 这个码值(留给捕获图集的深度清除值表示"没画过")——负值必须
    // 原样下传让光栅化裁掉, 钳回正区间会把远平面外的几何变成有效 texel
    const float rawNdcZ = 0.5 + cardViewPos.z / (2.0 * depthExtent);
    const float ndcZ = rawNdcZ < 0.0 ? rawNdcZ : max(rawNdcZ, kCardMinValidDepth);

    VSOuput output;
    output.svPosition = float4(ndcX, ndcY, ndcZ, 1.0);
    output.normalOS = geo.normal;
    output.tangentOS = geo.tangent;
    // .w 要跟 Opaque/Translucent 保持同一个"沿视线方向, 越远越大"的约定——但 cardViewPos.z
    // 是反过来的(见上面的注释: z 越大越靠近相机), 所以这里要取负, 不能直接抄 cardViewPos.z
    output.cardRelativePositionOS = float4(rel, -cardViewPos.z);
#if VERTEX_HAS_COLOR
    output.color = getColor(input);
#endif
#if VERTEX_HAS_TEXCOORD0
    output.texCoord0 = getTexCoord0(input);
#endif
#if VERTEX_HAS_TEXCOORD1
    output.texCoord1 = getTexCoord1(input);
#endif
#if VERTEX_HAS_TEXCOORD2
    output.texCoord2 = getTexCoord2(input);
#endif
#if VERTEX_HAS_TEXCOORD3
    output.texCoord3 = getTexCoord3(input);
#endif
    return output;
}

struct FSOutput
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 emissive : SV_Target2;
};

[shader("fragment")]
FSOutput FragmentMain(in VSOuput vsout)
{
    // 正交视角下"看向表面"的方向对整张 card 恒定, 就是 card 自己的朝外法线取反——不需要
    // 逐像素重新算
    float3 axisX, axisY, axisZ;
    getMeshCardDirectionAxes(PC.directionIndex, axisX, axisY, axisZ);
    float3 viewDirOS = -axisZ;

    MaterialInput mi = makeMaterialInput(vsout, viewDirOS);
    MaterialOutput mo;
    evaluateMaterial(mi, mo);

#if BLEND_MODE == BLEND_MODE_MASKED
    if (mo.opacity < ALPHA_CUTOFF)
        discard;
#endif

    // 法线跟 Opaque.slang 同一套 TBN 构建方式, 只是"世界空间"这里换成网格局部空间(这个
    // Pass 全程没有真正的世界空间概念, 见文件顶部注释)。存进 UNORM8 前要 *0.5+0.5 映射到
    // [0,1], 跟 UE CardCaptureAtlas 的 NormalAtlas(UNORM float4)是同一个编码
    const float3 bitangentOS = normalize(cross(mi.normalWS, mi.tangentWS.xyz)) * mi.tangentWS.w;
    const float3x3 tangentToObject = float3x3(mi.tangentWS.xyz, bitangentOS, mi.normalWS);
#if TANGENT_SPACE_NORMAL
    const float3 normalOS = mul(mo.normal, tangentToObject);
#else
    const float3 normalOS = mo.normal;
#endif

    // UE EnvBRDFApproxFullyRough(BRDF.ush:648-654): 镜面反照率按完全粗糙近似折算成等效漫反射项(×0.4524)叠加进 albedo
    float3 diffuseColor = mo.baseColor - mo.baseColor * mo.metallic;
    float3 specularColor = lerp(float3(0.08 * mo.specular), mo.baseColor, mo.metallic);
    diffuseColor += specularColor * 0.45;

    FSOutput output;
    output.albedo = float4(diffuseColor, 1.0);
    output.normal = float4(normalOS * 0.5 + 0.5, 1.0);
    output.emissive = float4(mo.emissive, 1.0);
    return output;
}
