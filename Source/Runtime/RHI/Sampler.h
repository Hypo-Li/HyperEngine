#pragma once
#include <Core/Common/Refable.h>

namespace hyp::rhi
{
    enum class Filter : uint8_t
    {
        Nearest,
        Linear,
    };

    enum class MipmapMode : uint8_t
    {
        Nearest,
        Linear,
    };

    enum class AddressMode : uint8_t
    {
        Repeat,
        Mirrored_Repeat,
        Clamp_To_Edge,
    };

    enum class CompareOp : uint8_t
    {
        Never,
        Less,
        Equal,
        Less_Equal,
        Greater,
        Not_Equal,
        Greater_Equal,
        Always
    };

    struct SamplerDesc
    {
        Filter minFilter = Filter::Linear;
        Filter magFilter = Filter::Linear;
        MipmapMode mipmapMode = MipmapMode::Linear;
        AddressMode addressModeU = AddressMode::Repeat;
        AddressMode addressModeV = AddressMode::Repeat;
        AddressMode addressModeW = AddressMode::Repeat;
        float mipLodBias = 0.0f;
        bool anisotropyEnable = false;
        float maxAnisotropy = 0.0f;
        bool compareEnable = false;
        CompareOp compareOp = CompareOp::Greater;
        float minLod = 0.0f;
        float maxLod = 100.0f;
    };

    class Device;
    class Sampler : public Refable
    {
    public:
        Sampler(const Device& device) :
            mDevice(device)
        {
        }

        virtual ~Sampler() = default;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

    private:
        const Device& mDevice;
    };
}
