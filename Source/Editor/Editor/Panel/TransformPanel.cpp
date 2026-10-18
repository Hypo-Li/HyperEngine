#include "TransformPanel.h"
#include "../ComponentRef.h"
#include "../Inspector.h"

#include <Engine/Context.h>
#include <Engine/Framework/Entity.h>
#include <Engine/Framework/Scene.h>
#include <Engine/Input/InputSystem.h>

#include <HypImGui/IconsMaterialSymbols.h>
#include <HypImGui/ImGuiDragCapture.h>
#include <HypImGui/ImGuiPropertyWidgets.h>
#include <HypImGui/ImGuiWidgets.h>

#include <imgui.h>

#include <cmath>

namespace hyp::ed
{
    namespace
    {
        constexpr char kScalarFormat[] = "%.3f";

        // Transform 不是 Component, 用不上 ComponentRef, 但"稳定身份 + 现查"这个道理跟
        // ComponentRef 是一样的——直接复用 EntityRef(它已经处理了 Entity 挂在 Scene 还是
        // Prefab 模板里这两种情况), 现查出 Entity 再取它的 Transform。
        struct TransformRef
        {
            EntityRef entityRef;

            static TransformRef capture(Transform& transform)
            {
                return TransformRef{ EntityRef::capture(*static_cast<Entity*>(transform.getOwner())) };
            }

            Transform& resolve() const
            {
                return entityRef.resolve()->getTransform();
            }
        };

        // 被拖动的那一个分量按它自己的变化比例带动另外两个; 该分量原值为 0 时没有比例可言, 三个分量统一取新值。
        float3 applyProportionalScale(const float3& before, const float3& after)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                if (after[axis] == before[axis])
                    continue;

                if (std::abs(before[axis]) < 1e-6f)
                    return float3(after[axis]);
                return before * (after[axis] / before[axis]);
            }
            return after;
        }

        bool IsDescendantOf(Transform& transform, Transform& ancestor)
        {
            for (Transform* parent = transform.getParent(); parent; parent = parent->getParent())
            {
                if (parent == &ancestor)
                    return true;
            }
            return false;
        }
    }

    TransformPanel::TransformPanel(Transform& transform, TransformSpace& space, bool& scaleLocked, UndoStack& undoStack, Inspector& inspector) noexcept :
        IInspectorPanel(transform),
        mTransform(transform),
        mSpace(space),
        mScaleLocked(scaleLocked),
        mUndoStack(undoStack),
        mInspector(inspector),
        mRotationCacheSpace(space),
        mRotationCache(space == TransformSpace::World ? transform.getWorldRotationEuler() : transform.getLocalRotationEuler())
    {
    }

    void TransformPanel::syncRotationCache(bool sourceWasWorldSpace, const float3& euler) noexcept
    {
        bool currentWorldSpace = mSpace == TransformSpace::World;
        mRotationCache = (sourceWasWorldSpace == currentWorldSpace)
            ? euler
            : (currentWorldSpace ? mTransform.getWorldRotationEuler() : mTransform.getLocalRotationEuler());
    }

    void TransformPanel::refreshRotationCache() noexcept
    {
        bool worldSpace = mSpace == TransformSpace::World;
        mRotationCache = worldSpace ? mTransform.getWorldRotationEuler() : mTransform.getLocalRotationEuler();
    }

    void TransformPanel::onDraw()
    {
        Window& window = Context::get().getInputSystem().getWindow();

        if (!BeginPropertyTable("TransformProperties", ImGuiTableFlags_BordersInnerH))
        {
            return;
        }

        BeginPropertyRow("Space");
        int selectedSpace = static_cast<int>(mSpace);
        if (ImGui::Combo("##Space", &selectedSpace, "Local\0World\0"))
        {
            mSpace = static_cast<TransformSpace>(selectedSpace);
        }

        const bool worldSpace = mSpace == TransformSpace::World;
        TransformRef transformRef = TransformRef::capture(mTransform);

        BeginPropertyRow("Position");
        double3 position = worldSpace
            ? mTransform.getWorldPosition()
            : mTransform.getLocalPosition();
        double3 positionBeforeWidget = position;
        // 不能捕获 this: Transform 目标一变(切换实体、Entity 被删除又撤销恢复出新地址)
        // Inspector 就会把这个 TransformPanel 整个销毁重建, 而 undo command 要活到之后
        // 任意一帧才可能被调用, 捕获 this 存进 undo 栈到那时候就是野指针。也不能直接捕获
        // &mTransform, 见上面 TransformRef 的注释, 改成捕获 transformRef, 每次现查。
        auto applyPosition = [ref = transformRef, worldSpace](const double3& v)
        {
            if (worldSpace)
                ref.resolve().setWorldPosition(v);
            else
                ref.resolve().setLocalPosition(v);
        };
        bool positionChanged = ImGui::DragScalarN(
            "##Position",
            ImGuiDataType_Double,
            &position.x,
            3,
            0.05f,
            nullptr,
            nullptr,
            kScalarFormat);
        CaptureCursorDuringDrag(window);
        mUndoStack.trackGesture("Change Position"_hs, positionBeforeWidget, position, applyPosition);
        if (positionChanged)
            applyPosition(position);
        double3 positionBeforeReset = position;
        if (DrawPropertyReset("Position", position, double3(0.0)))
        {
            applyPosition(position);
            mUndoStack.push("Reset Position"_hs,
                [applyPosition, positionBeforeReset]() { applyPosition(positionBeforeReset); },
                [applyPosition, position]() { applyPosition(position); });
        }

        BeginPropertyRow("Rotation");
        if (mRotationCacheSpace != mSpace)
        {
            mRotationCacheSpace = mSpace;
            mRotationCache = worldSpace
                ? mTransform.getWorldRotationEuler()
                : mTransform.getLocalRotationEuler();
        }
        float3& rotation = mRotationCache;
        float3 rotationBeforeWidget = rotation;
        // 跟 applyPosition 一样不能捕获 this, 但 Inspector 是单例窗口, 寿命贯穿整个编辑器
        // 会话, 捕获它是安全的——undo/redo 触发时借它现查一遍当前显示的 TransformPanel,
        // 是不是还是这同一个 Transform, 是的话把历史欧拉角原样写回它的缓存。
        auto applyRotation = [ref = transformRef, worldSpace, inspector = &mInspector](const float3& v)
        {
            Transform& transform = ref.resolve();
            if (worldSpace)
                transform.setWorldRotationEuler(v);
            else
                transform.setLocalRotationEuler(v);

            if (TransformPanel* panel = inspector->getTransformPanel())
            {
                Transform& displayed = panel->getTransform();
                if (&displayed == &transform)
                    panel->syncRotationCache(worldSpace, v);
                else if (IsDescendantOf(displayed, transform))
                    panel->refreshRotationCache();
            }
        };
        bool rotationChanged = ImGui::DragFloat3(
            "##Rotation",
            &rotation.x,
            0.25f,
            0.0f,
            0.0f,
            kScalarFormat);
        CaptureCursorDuringDrag(window);
        mUndoStack.trackGesture("Change Rotation"_hs, rotationBeforeWidget, rotation, applyRotation);
        if (rotationChanged)
            applyRotation(rotation);
        float3 rotationBeforeReset = rotation;
        if (DrawPropertyReset("Rotation", rotation, float3(0.0f)))
        {
            applyRotation(rotation);
            mUndoStack.push("Reset Rotation"_hs,
                [applyRotation, rotationBeforeReset]() { applyRotation(rotationBeforeReset); },
                [applyRotation, rotationAfterReset = rotation]() { applyRotation(rotationAfterReset); });
        }

        BeginPropertyRow("Scale");
        float3 scale = worldSpace
            ? mTransform.getWorldScale()
            : mTransform.getLocalScale();
        float3 scaleBeforeWidget = scale;
        auto applyScale = [ref = transformRef, worldSpace](const float3& v)
        {
            if (worldSpace)
                ref.resolve().setWorldScale(v);
            else
                ref.resolve().setLocalScale(v);
        };
        bool scaleChanged = ImGui::DragFloat3(
            "##Scale",
            &scale.x,
            0.01f,
            0.0f,
            0.0f,
            kScalarFormat);
        CaptureCursorDuringDrag(window);
        if (scaleChanged && mScaleLocked)
            scale = applyProportionalScale(scaleBeforeWidget, scale);
        mUndoStack.trackGesture("Change Scale"_hs, scaleBeforeWidget, scale, applyScale);
        if (scaleChanged)
            applyScale(scale);
        float3 scaleBeforeReset = scale;
        if (DrawPropertyReset("Scale", scale, float3(1.0f)))
        {
            applyScale(scale);
            mUndoStack.push("Reset Scale"_hs,
                [applyScale, scaleBeforeReset]() { applyScale(scaleBeforeReset); },
                [applyScale, scale]() { applyScale(scale); });
        }

        // 锁图标画在 Property 列的 "Scale" 标签后面, 必须等上面的 DragFloat3 和 trackGesture 都
        // 调完再切回第 0 列, 中间不能插入别的 item。
        ImGui::TableSetColumnIndex(0);
        const char* lockIcon = mScaleLocked ? ICON_MS_LOCK : ICON_MS_LOCK_OPEN;
        AlignVerticallyInRow(GetCurrentPropertyRowHeight(), ImGui::CalcTextSize(lockIcon).y);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::CalcTextSize("Scale").x + ImGui::GetStyle().ItemSpacing.x);
        if (TextOnlyButton(lockIcon))
            mScaleLocked = !mScaleLocked;

        EndPropertyTable();
    }
}
