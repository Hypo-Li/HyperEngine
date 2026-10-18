#include "MeshShapeTypes.h"

#include <cassert>

namespace hyp
{
    template <> Type* Reflection::createType<MeshCard>()
    {
        auto s = new TStructure<MeshCard>("MeshCard");
        s->addProperty("Center", &MeshCard::center);
        s->addProperty("HalfExtents", &MeshCard::halfExtents);
        s->addProperty("DirectionIndex", &MeshCard::directionIndex);
        return s;
    }

    void getMeshCardDirectionAxes(uint8_t directionIndex, float3& outAxisX, float3& outAxisY, float3& outAxisZ) noexcept
    {
        assert(directionIndex < 6);

        // 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z——AxisZ 是朝外法线(定义决定必然随方向翻号, 不是
        // 选择), AxisX 是 card 图像的水平轴, AxisY 是垂直轴。
        //
        // 手性(AxisX/AxisY/AxisZ 是否构成右手基, 即 det[axisX;axisY;axisZ] 是否为 +1)
        // 六个方向必须一致, 不能有的方向是旋转(det=+1)有的方向是镜像(det=-1)——旧版本
        // ±Y 这一对用的是 (axisX=世界X, axisY=世界Z), det=-1, 跟其余四个方向(det=+1)手性
        // 不一致。这不是"无所谓的观感选择": CardCapture.slang 直接拿这三个基向量当行向量
        // 把局部坐标变到 card 视图空间, 手性为 -1 的方向会把三角形环绕顺序整体翻转, 背面
        // 剔除(FrontFace::Counter_Clockwise)因此会把本该保留的正面三角形全部剔除掉。对
        // 闭合网格(比如 Suzanne)这个 bug 不容易被发现, 因为剔除结果换成了内壁三角形、页面
        // 依然有内容; 但对薄壳、开放式的网格(比如 Cornell box 只有一面墙的方向), 翻转方向
        // 没有"备用的另一层三角形"顶上, 直接整页空白——这正是这条注释存在的原因, 由一次
        // Cornell box 测试(单面 0.1m 厚薄壁盒子)实测暴露, 修复前 ±Y 两个方向完全画不出来。
        //
        // 在手性一致的前提下, 才轮到"翻哪个轴做镜像"这个纯观感问题: 不管翻哪个轴, 只要
        // 恰好翻到"网格自身的镜像对称轴"上, 对一个左右对称的网格(比如 Suzanne, 关于 X=0
        // 对称)来说镜像前后长得一模一样, 等于白翻。+X/-X 翻 AxisX(映射世界 Y 轴, 不会踩中
        // X 轴对称性); +Z/-Z 翻 AxisY(映射世界 Y 轴, 同理)——+Z/-Z 的水平轴映射的正好是
        // X 轴, 翻它会白翻, 所以改翻 AxisY。
        //
        // +Y/-Y 曾经用 AxisX=世界Z、AxisY=世界X 这组映射(翻 AxisY), 实测捕获出来的内容
        // 整体转了 90 度("倒下")——水平轴/垂直轴映射反了, 改成 AxisX=世界X(在 ±Y 间翻转)、
        // AxisY=世界Z(不翻)才是对的方向。代价: 这一对现在翻转的是映射世界 X 轴的分量, 在
        // Suzanne 这类关于 X=0 对称的测试网格上会是"白翻"(肉眼看不出镜像效果, 跟上面
        // +Z/-Z 要绕开的是同一类问题)——不影响正确性, 只是拿这类对称网格测试时验证不出
        // ±Y 的镜像有没有生效, 换一个非对称网格测试才能看出来。
        static const float3 kAxisX[6] = {
            float3(0.0f, 1.0f, 0.0f), float3(0.0f, -1.0f, 0.0f),  // +X, -X (翻)
            float3(-1.0f, 0.0f, 0.0f), float3(1.0f, 0.0f, 0.0f),  // +Y, -Y (翻)
            float3(1.0f, 0.0f, 0.0f), float3(1.0f, 0.0f, 0.0f),   // +Z, -Z (不翻)
        };
        static const float3 kAxisY[6] = {
            float3(0.0f, 0.0f, 1.0f), float3(0.0f, 0.0f, 1.0f),  // +X, -X (不翻)
            float3(0.0f, 0.0f, 1.0f), float3(0.0f, 0.0f, 1.0f),  // +Y, -Y (不翻)
            float3(0.0f, 1.0f, 0.0f), float3(0.0f, -1.0f, 0.0f), // +Z, -Z (翻)
        };
        static const float3 kAxisZ[6] = {
            float3(1.0f, 0.0f, 0.0f), float3(-1.0f, 0.0f, 0.0f),
            float3(0.0f, 1.0f, 0.0f), float3(0.0f, -1.0f, 0.0f),
            float3(0.0f, 0.0f, 1.0f), float3(0.0f, 0.0f, -1.0f),
        };

        outAxisX = kAxisX[directionIndex];
        outAxisY = kAxisY[directionIndex];
        outAxisZ = kAxisZ[directionIndex];
    }
}
