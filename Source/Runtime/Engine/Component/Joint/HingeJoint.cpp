#include "HingeJoint.h"

namespace hyp
{
    REGISTER_CLASS(HingeJoint)

    template <> Type* Reflection::createType<HingeJoint>()
    {
        auto c = new TClass<HingeJoint, Joint>("HingeJoint");
        c->setDisplayName("Hinge Joint");
        c->addProperty("Anchor", &HingeJoint::mAnchor);
        c->addProperty("Connected Anchor", &HingeJoint::mConnectedAnchor);
        c->addProperty("Axis", &HingeJoint::mAxis);
        c->addProperty("Min Angle", &HingeJoint::mMinAngle);
        c->addProperty("Max Angle", &HingeJoint::mMaxAngle);
        return c;
    }
}
