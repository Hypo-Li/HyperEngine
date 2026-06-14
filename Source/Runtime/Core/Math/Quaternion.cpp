#include "Quaternion.h"

namespace hyp
{
    template <> Type* Reflection::createType<Quaternion>()
    {
        auto s = new TStructure<Quaternion>("Quaternion");
        s->addProperty("w", &Quaternion::w);
        s->addProperty("x", &Quaternion::x);
        s->addProperty("y", &Quaternion::y);
        s->addProperty("z", &Quaternion::z);
        return s;
    }
}
