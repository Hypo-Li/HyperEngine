#pragma once
#include <Core/Common/EnumBitwise.h>
#include <Core/Common/HString.h>
#include <Core/Common/Refable.h>

#include <cstdint>
#include <vector>
#include <span>

namespace hyp::rhi
{
    // Geometry, TessControl, TessEval is useless, so we don't support them
    enum class ShaderStage
    {
        None = 0,
        Vertex = 1 << 0,
        Fragment = 1 << 1,
        Compute = 1 << 2,

        Graphics = Vertex | Fragment,
        All = Graphics | Compute,
    };
    ENABLE_ENUM_CLASS_BITWISE(ShaderStage)

    enum class ShaderCodeFormat
    {
        Spirv,
        Dxil,
        Wgsl,
    };

    struct ShaderCompileTarget
    {
        ShaderCodeFormat codeFormat = ShaderCodeFormat::Spirv;
        uint32_t majorVersion = 1;
        uint32_t minorVersion = 0;

        bool operator==(const ShaderCompileTarget&) const noexcept = default;
    };

    class Device;
    class ShaderModule : public Refable
    {
    public:
        ShaderModule(const Device& device, ShaderStage stage, std::vector<uint8_t>&& code, ShaderCodeFormat codeFormat) :
            mDevice(device),
            mStage(stage),
            mCode(std::move(code)),
            mCodeFormat(codeFormat)
        {
        }

        virtual ~ShaderModule() = default;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        ShaderStage getStage() const noexcept
        {
            return mStage;
        }

        std::span<const uint8_t> getCode() const noexcept
        {
            return mCode;
        }

        ShaderCodeFormat getCodeFormat() const noexcept
        {
            return mCodeFormat;
        }

        virtual void setDebugName(HString name)
        {
            mDebugName = std::move(name);
        }

        HString getDebugName() const noexcept
        {
            return mDebugName;
        }

    private:
        const Device& mDevice;
        ShaderStage mStage;
        std::vector<uint8_t> mCode;
        ShaderCodeFormat mCodeFormat;
        HString mDebugName;
    };
}
