#include "RenderGraphTypes.h"

#include <Core/Common/Hash.h>

namespace hyp::rg
{
    bool BufferKey::operator==(const BufferKey& other) const noexcept
    {
        if (desc.size == other.desc.size &&
            desc.usage == other.desc.usage &&
            name == other.name)
        {
            return true;
        }
        return false;
    }

    size_t BufferKeyHash::operator()(const BufferKey& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.desc.size);
        Hash::combine(seed, key.desc.usage);
        Hash::combine(seed, key.name);
        return seed;
    }

    bool TextureKey::operator==(const TextureKey& other) const noexcept
    {
        if (desc.type == other.desc.type &&
            desc.format == other.desc.format &&
            desc.extent == other.desc.extent &&
            desc.mipCount == other.desc.mipCount &&
            desc.layerCount == other.desc.layerCount &&
            desc.samples == other.desc.samples &&
            desc.usage == other.desc.usage &&
            name == other.name)
        {
            return true;
        }
        return false;
    }

    size_t TextureKeyHash::operator()(const TextureKey& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.desc.type);
        Hash::combine(seed, key.desc.format);
        Hash::combine(seed, key.desc.extent.x);
        Hash::combine(seed, key.desc.extent.y);
        Hash::combine(seed, key.desc.extent.z);
        Hash::combine(seed, key.desc.mipCount);
        Hash::combine(seed, key.desc.layerCount);
        Hash::combine(seed, key.desc.samples);
        Hash::combine(seed, key.desc.usage);
        Hash::combine(seed, key.name);
        return seed;
    }

    bool SubTextureKey::operator==(const SubTextureKey& other) const noexcept
    {
        if (desc.type == other.desc.type &&
            desc.format == other.desc.format &&
            desc.range.aspect == other.desc.range.aspect &&
            desc.range.baseMip == other.desc.range.baseMip &&
            desc.range.mipCount == other.desc.range.mipCount &&
            desc.range.baseLayer == other.desc.range.baseLayer &&
            desc.range.layerCount == other.desc.range.layerCount &&
            name == other.name)
        {
            return true;
        }
        return false;
    }

    size_t SubTextureKeyHash::operator()(const SubTextureKey& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.desc.type);
        Hash::combine(seed, key.desc.format);
        Hash::combine(seed, key.desc.range.aspect);
        Hash::combine(seed, key.desc.range.baseMip);
        Hash::combine(seed, key.desc.range.mipCount);
        Hash::combine(seed, key.desc.range.baseLayer);
        Hash::combine(seed, key.desc.range.layerCount);
        Hash::combine(seed, key.name);
        return seed;
    }

}
