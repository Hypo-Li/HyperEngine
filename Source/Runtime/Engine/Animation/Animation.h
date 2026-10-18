#pragma once
#include <Core/Object/Object.h>
#include <Core/Common/HString.h>
#include <Core/Math/Vector.h>
#include <Core/Math/Quaternion.h>

namespace hyp
{
    // 可播放动画数据的公共基类——AnimationClip 是第一个具体实现, 以后 BlendSpace/AnimMontage
    // 这类同样"给定时间就能确定性求出一份姿势"的数据资产也从这里派生。跟真正有状态、每个
    // 角色各自维护运行状态的动画蓝图/状态机不是一回事, 不共享这个基类。
    class Animation : public Object
    {
        REFLECT_CLASS(Animation)
    public:
        virtual float getDuration() const noexcept = 0;

        bool isLooping() const noexcept
        {
            return mLooping;
        }

        void setLooping(bool looping) noexcept
        {
            mLooping = looping;
        }

        // 按骨骼名字查出一个可以反复传给 sampleBone() 的下标, 没有对应数据时返回 -1。拆成
        // "按名字查一次 + 按下标反复采样"两步, 是因为调用方(SkeletalMeshRenderer)每帧对
        // 同一根骨骼都要采样一次, 名字查找的结果在动画/骨架都没换的情况下每帧不变, 只需要
        // 查一次、缓存住, 不需要每帧都付一次按名字查找的开销
        virtual int32_t resolveBoneTrackIndex(HString boneName) const = 0;

        // 用 resolveBoneTrackIndex() 查出来的下标采样 time 时刻的局部 TRS, boneTrackIndex 为 -1
        // (resolveBoneTrackIndex() 没查到)时返回 false——调用方(比如 SkeletalMeshRenderer)
        // fallback 到 Skeleton 的 bind pose local TRS
        virtual bool sampleBone(int32_t boneTrackIndex, float time, float3& outPosition, Quaternion& outRotation, float3& outScale) const = 0;

    protected:
        bool mLooping = false;
    };

    template <> Type* Reflection::createType<Animation>();
}
