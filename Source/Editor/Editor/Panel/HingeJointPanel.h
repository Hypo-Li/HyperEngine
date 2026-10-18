#pragma once

#include "JointPanel.h"

#include <Engine/Component/Joint/HingeJoint.h>

namespace hyp::ed
{
    class HingeJointPanel final : public JointPanel
    {
    public:
        explicit HingeJointPanel(HingeJoint& hingeJoint) noexcept;

    protected:
        void onDraw() override;

    private:
        HingeJoint& mHingeJoint;
    };
}
