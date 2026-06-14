#pragma once
#include <Core/Reflection/Reflection.h>

namespace hyp
{
    enum class PixelFormat
    {
        Undefined,

        R8_UNORM,
        RG8_UNORM,
        RGB8_UNORM,
        RGBA8_UNORM,

        R16_UNORM,
        RG16_UNORM,
        RGB16_UNORM,
        RGBA16_UNORM,

        R16_SFLOAT,
        RG16_SFLOAT,
        RGB16_SFLOAT,
        RGBA16_SFLOAT,

        R32_SFLOAT,
        RG32_SFLOAT,
        RGB32_SFLOAT,
        RGBA32_SFLOAT,
    };

    template <> inline Type* Reflection::createType<PixelFormat>()
    {
        auto e = new TEnumeration<PixelFormat>("PixelFormat");
        e->addValue("Undefined", PixelFormat::Undefined);

        e->addValue("R8_UNORM", PixelFormat::R8_UNORM);
        e->addValue("RG8_UNORM", PixelFormat::RG8_UNORM);
        e->addValue("RGB8_UNORM", PixelFormat::RGB8_UNORM);
        e->addValue("RGBA8_UNORM", PixelFormat::RGBA8_UNORM);

        e->addValue("R16_UNORM", PixelFormat::R16_UNORM);
        e->addValue("RG16_UNORM", PixelFormat::RG16_UNORM);
        e->addValue("RGB16_UNORM", PixelFormat::RGB16_UNORM);
        e->addValue("RGBA16_UNORM", PixelFormat::RGBA16_UNORM);

        e->addValue("R16_SFLOAT", PixelFormat::R16_SFLOAT);
        e->addValue("RG16_SFLOAT", PixelFormat::RG16_SFLOAT);
        e->addValue("RGB16_SFLOAT", PixelFormat::RGB16_SFLOAT);
        e->addValue("RGBA16_SFLOAT", PixelFormat::RGBA16_SFLOAT);

        e->addValue("R32_SFLOAT", PixelFormat::R32_SFLOAT);
        e->addValue("RG32_SFLOAT", PixelFormat::RG32_SFLOAT);
        e->addValue("RGB32_SFLOAT", PixelFormat::RGB32_SFLOAT);
        e->addValue("RGBA32_SFLOAT", PixelFormat::RGBA32_SFLOAT);

        return e;
    }
}
