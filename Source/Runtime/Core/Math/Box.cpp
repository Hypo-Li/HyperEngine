#include "Box.h"

namespace hyp
{
    template <> Type* Reflection::createType<Boxf>()
    {
        auto s = new TStructure<Boxf>("Boxf");
        s->addProperty("Min", &Boxf::mMin);
        s->addProperty("Max", &Boxf::mMax);
        return s;
    }

    template <> Type* Reflection::createType<Boxd>()
    {
        auto s = new TStructure<Boxd>("Boxd");
        s->addProperty("Min", &Boxd::mMin);
        s->addProperty("Max", &Boxd::mMax);
        return s;
    }
}
