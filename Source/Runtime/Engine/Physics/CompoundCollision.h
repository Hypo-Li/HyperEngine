#pragma once
#include "PhysicsShapeDesc.h"

#include <Core/Math/Quaternion.h>
#include <Core/Object/Object.h>

#include <vector>

namespace hyp
{
    // 不支持 Plane 子形状。
    class CompoundCollision : public Object
    {
        REFLECT_CLASS(CompoundCollision)
    public:
        struct SubShapeTransform
        {
            float3 localPosition = float3(0.0f);
            Quaternion localRotation = Quaternion(1, 0, 0, 0);
            float3 localScale = float3(1.0f);
        };

        struct BoxEntry { BoxShapeDesc shape; SubShapeTransform transform; };
        struct SphereEntry { SphereShapeDesc shape; SubShapeTransform transform; };
        struct CapsuleEntry { CapsuleShapeDesc shape; SubShapeTransform transform; };
        struct CylinderEntry { CylinderShapeDesc shape; SubShapeTransform transform; };
        struct ConvexHullEntry { ConvexHullShapeDesc shape; SubShapeTransform transform; };
        struct MeshEntry { MeshShapeDesc shape; SubShapeTransform transform; };

        CompoundCollision() = default;

        const std::vector<BoxEntry>& getBoxes() const noexcept { return mBoxes; }
        void setBoxes(std::vector<BoxEntry> boxes) noexcept { mBoxes = std::move(boxes); }

        const std::vector<SphereEntry>& getSpheres() const noexcept { return mSpheres; }
        void setSpheres(std::vector<SphereEntry> spheres) noexcept { mSpheres = std::move(spheres); }

        const std::vector<CapsuleEntry>& getCapsules() const noexcept { return mCapsules; }
        void setCapsules(std::vector<CapsuleEntry> capsules) noexcept { mCapsules = std::move(capsules); }

        const std::vector<CylinderEntry>& getCylinders() const noexcept { return mCylinders; }
        void setCylinders(std::vector<CylinderEntry> cylinders) noexcept { mCylinders = std::move(cylinders); }

        const std::vector<ConvexHullEntry>& getConvexHulls() const noexcept { return mConvexHulls; }
        void setConvexHulls(std::vector<ConvexHullEntry> convexHulls) noexcept { mConvexHulls = std::move(convexHulls); }

        const std::vector<MeshEntry>& getMeshes() const noexcept { return mMeshes; }
        void setMeshes(std::vector<MeshEntry> meshes) noexcept { mMeshes = std::move(meshes); }

        bool isEmpty() const noexcept
        {
            return mBoxes.empty() && mSpheres.empty() && mCapsules.empty()
                && mCylinders.empty() && mConvexHulls.empty() && mMeshes.empty();
        }

        // 含 Mesh 子形状时 Jolt 的 CompoundShape::MustBeStatic() 会返回 true。
        bool requiresStatic() const noexcept
        {
            return !mMeshes.empty();
        }

    private:
        std::vector<BoxEntry> mBoxes;
        std::vector<SphereEntry> mSpheres;
        std::vector<CapsuleEntry> mCapsules;
        std::vector<CylinderEntry> mCylinders;
        std::vector<ConvexHullEntry> mConvexHulls;
        std::vector<MeshEntry> mMeshes;
    };

    template <> Type* Reflection::createType<CompoundCollision::SubShapeTransform>();
    template <> Type* Reflection::createType<CompoundCollision::BoxEntry>();
    template <> Type* Reflection::createType<CompoundCollision::SphereEntry>();
    template <> Type* Reflection::createType<CompoundCollision::CapsuleEntry>();
    template <> Type* Reflection::createType<CompoundCollision::CylinderEntry>();
    template <> Type* Reflection::createType<CompoundCollision::ConvexHullEntry>();
    template <> Type* Reflection::createType<CompoundCollision::MeshEntry>();
    template <> Type* Reflection::createType<CompoundCollision>();
}
