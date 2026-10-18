#include "Animation.h"

namespace hyp
{
    REGISTER_CLASS(Animation)

    template <> Type* Reflection::createType<Animation>()
    {
        auto c = new TClass<Animation>("Animation");
        c->addProperty("Looping", &Animation::mLooping);
        return c;
    }
}
