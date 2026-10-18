#pragma once
#include <RHI/Texture.h>

#include "VolkInclude.h"

namespace hyp::vk
{
    VkFormat convertToVkFormat(rhi::TextureFormat format);

    VkImageAspectFlags convertToVkImageAspectFlags(rhi::TextureAspect aspect);
}
