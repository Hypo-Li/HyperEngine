#pragma once
#define GLM_FORCE_QUAT_DATA_WXYZ
#include <glm/gtc/quaternion.hpp>
#include "../Reflection/Reflection.h"

namespace hyp
{
    using Quaternion = glm::tquat<float>;

    template <> Type* Reflection::createType<Quaternion>();
}
