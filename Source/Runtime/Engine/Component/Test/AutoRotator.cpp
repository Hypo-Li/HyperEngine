#include "AutoRotator.h"
#include "../../Framework/Entity.h"

namespace hyp
{
    REGISTER_CLASS(AutoRotator)

    void AutoRotator::onTick(float deltaTime)
    {
        auto& transform = getEntity()->getTransform();
        float angle = glm::radians(45.0f * deltaTime);
        Quaternion delta = glm::angleAxis(angle, glm::normalize(float3(1.0f, 1.0f, 0.0f)));
        transform.setLocalRotation(delta * transform.getLocalRotation());
    }

    template <> Type* Reflection::createType<AutoRotator>()
    {
        auto c = new TClass<AutoRotator, Component>("AutoRotator");
        c->setDisplayName("Auto Rotator");
        return c;
    }
}
