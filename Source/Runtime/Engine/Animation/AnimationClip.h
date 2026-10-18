#pragma once
#include "Animation.h"

#include <vector>

namespace hyp
{
    struct PositionKey
    {
        float time = 0.0f;
        float3 value{ 0, 0, 0 };
    };

    struct RotationKey
    {
        float time = 0.0f;
        Quaternion value{ 1, 0, 0, 0 };
    };

    struct ScaleKey
    {
        float time = 0.0f;
        float3 value{ 1, 1, 1 };
    };

    // 按骨骼名字绑定, 不是 Skeleton 里的 index——同一份 Track 可以在骨骼命名一致的
    // 不同 Skeleton 之间直接复用, 具体对应第几根骨骼由播放时现查现存, 不固化在这里。
    struct BoneTrack
    {
        HString boneName;
        std::vector<PositionKey> positionKeys;
        std::vector<RotationKey> rotationKeys;
        std::vector<ScaleKey> scaleKeys;

        // 三个通道各自独立插值(数量、时间点可以互不相同, 比如整段位置不变就只有 1 个
        // PositionKey), Position/Scale 用线性, Rotation 用 Slerp; time 超出关键帧覆盖
        // 范围时钳制到首尾两端的值, 不外推。某个通道的关键帧数组是空的就不touch 对应的
        // out 参数, 由调用方决定 fallback 值(比如 Skeleton 的 bind pose)。
        void sample(float time, float3& outPosition, Quaternion& outRotation, float3& outScale) const;
    };

    class AnimationClip : public Animation
    {
        REFLECT_CLASS(AnimationClip)
    public:
        AnimationClip() = default;

        float getDuration() const noexcept override
        {
            return mDuration;
        }

        // 由导入源(FBX/glTF)显式给, 不从关键帧反推——动画结尾停留一段静止时间很常见,
        // 最后一个关键帧的时间不一定等于 Duration
        void setDuration(float duration) noexcept
        {
            assert(duration >= 0.0f);
            mDuration = duration;
        }

        int32_t resolveBoneTrackIndex(HString boneName) const noexcept override;

        bool sampleBone(int32_t boneTrackIndex, float time, float3& outPosition, Quaternion& outRotation, float3& outScale) const override;

        uint32_t addBoneTrack(HString boneName);

        uint32_t getBoneTrackCount() const noexcept
        {
            return static_cast<uint32_t>(mTracks.size());
        }

        const BoneTrack& getBoneTrack(uint32_t index) const noexcept
        {
            assert(index < mTracks.size());
            return mTracks[index];
        }

        BoneTrack& getBoneTrack(uint32_t index) noexcept
        {
            assert(index < mTracks.size());
            return mTracks[index];
        }

    private:
        float mDuration = 0.0f;
        std::vector<BoneTrack> mTracks;
    };

    template <> Type* Reflection::createType<PositionKey>();
    template <> Type* Reflection::createType<RotationKey>();
    template <> Type* Reflection::createType<ScaleKey>();
    template <> Type* Reflection::createType<BoneTrack>();
    template <> Type* Reflection::createType<AnimationClip>();
}
