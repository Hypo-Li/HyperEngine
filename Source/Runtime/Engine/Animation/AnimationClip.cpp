#include "AnimationClip.h"

#include <algorithm>

namespace hyp
{
    REGISTER_CLASS(AnimationClip)

    namespace
    {
        // 二分定位 time 落在的关键帧区间并插值; 越界钳制到首/尾关键帧, 不外推。调用方
        // 必须保证 keys 非空(由 BoneTrack::sample 的 empty() 检查保证)且已按 time 升序
        // 排列(密集烘焙来源天然有序; 手工曲线还没有 consumer, 暂不处理乱序输入)。
        template <typename Key, typename Value, typename Interpolate>
        Value sampleKeys(const std::vector<Key>& keys, float time, Interpolate interpolate)
        {
            assert(!keys.empty());
            if (time <= keys.front().time)
                return keys.front().value;
            if (time >= keys.back().time)
                return keys.back().value;

            auto next = std::upper_bound(keys.begin(), keys.end(), time,
                [](float t, const Key& key) { return t < key.time; });
            auto prev = next - 1;

            float span = next->time - prev->time;
            float t = span > 0.0f ? (time - prev->time) / span : 0.0f;
            return interpolate(prev->value, next->value, t);
        }
    }

    void BoneTrack::sample(float time, float3& outPosition, Quaternion& outRotation, float3& outScale) const
    {
        if (!positionKeys.empty())
        {
            outPosition = sampleKeys<PositionKey, float3>(positionKeys, time,
                [](const float3& a, const float3& b, float t) { return glm::mix(a, b, t); });
        }
        if (!rotationKeys.empty())
        {
            outRotation = sampleKeys<RotationKey, Quaternion>(rotationKeys, time,
                [](const Quaternion& a, const Quaternion& b, float t) { return glm::slerp(a, b, t); });
        }
        if (!scaleKeys.empty())
        {
            outScale = sampleKeys<ScaleKey, float3>(scaleKeys, time,
                [](const float3& a, const float3& b, float t) { return glm::mix(a, b, t); });
        }
    }

    bool AnimationClip::sampleBone(int32_t boneTrackIndex, float time, float3& outPosition, Quaternion& outRotation, float3& outScale) const
    {
        if (boneTrackIndex < 0)
            return false;

        assert(static_cast<size_t>(boneTrackIndex) < mTracks.size());
        mTracks[boneTrackIndex].sample(time, outPosition, outRotation, outScale);
        return true;
    }

    uint32_t AnimationClip::addBoneTrack(HString boneName)
    {
        assert(resolveBoneTrackIndex(boneName) == -1);
        uint32_t index = static_cast<uint32_t>(mTracks.size());
        BoneTrack track;
        track.boneName = boneName;
        mTracks.push_back(std::move(track));
        return index;
    }

    // 只在骨架/动画变化时(SkeletalMeshRenderer::rebuildBoneTrackIndexCache())按骨骼数调用
    // 一次, 不是每帧每骨骼都调——线性扫 mTracks 足够, 不需要额外维护一份哈希表
    int32_t AnimationClip::resolveBoneTrackIndex(HString boneName) const noexcept
    {
        for (size_t i = 0; i < mTracks.size(); ++i)
        {
            if (mTracks[i].boneName == boneName)
                return static_cast<int32_t>(i);
        }
        return -1;
    }

    template <> Type* Reflection::createType<PositionKey>()
    {
        auto s = new TStructure<PositionKey>("PositionKey");
        s->addProperty("Time", &PositionKey::time);
        s->addProperty("Value", &PositionKey::value);
        return s;
    }

    template <> Type* Reflection::createType<RotationKey>()
    {
        auto s = new TStructure<RotationKey>("RotationKey");
        s->addProperty("Time", &RotationKey::time);
        s->addProperty("Value", &RotationKey::value);
        return s;
    }

    template <> Type* Reflection::createType<ScaleKey>()
    {
        auto s = new TStructure<ScaleKey>("ScaleKey");
        s->addProperty("Time", &ScaleKey::time);
        s->addProperty("Value", &ScaleKey::value);
        return s;
    }

    template <> Type* Reflection::createType<BoneTrack>()
    {
        auto s = new TStructure<BoneTrack>("BoneTrack");
        s->addProperty("BoneName", &BoneTrack::boneName);
        s->addProperty("PositionKeys", &BoneTrack::positionKeys);
        s->addProperty("RotationKeys", &BoneTrack::rotationKeys);
        s->addProperty("ScaleKeys", &BoneTrack::scaleKeys);
        return s;
    }

    template <> Type* Reflection::createType<AnimationClip>()
    {
        auto c = new TClass<AnimationClip, Animation>("AnimationClip");
        c->setDisplayName("Animation Clip");
        c->addProperty("Duration", &AnimationClip::mDuration);
        c->addProperty("Tracks", &AnimationClip::mTracks);
        return c;
    }
}
