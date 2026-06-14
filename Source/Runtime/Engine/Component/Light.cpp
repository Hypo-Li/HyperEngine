#include "Light.h"

namespace hyp
{
    REGISTER_CLASS(Light)

    template <> Type* Reflection::createType<Light>()
    {
        auto c = new TClass<Light, Component>("Light");
        return c;
    }
}
