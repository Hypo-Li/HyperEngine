#include "AtmospherePanel.h"
#include "../ComponentRef.h"
#include "../UndoStack.h"
#include "ComponentPanelRegistry.h"

#include <HypImGui/ImGuiColorWidgets.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<Atmosphere, AtmospherePanel> gRegistrar;

        void applyAtmosphereParameters(ComponentRef ref, const AtmosphereParameters& snapshot)
        {
            Atmosphere* atmosphere = ref.resolveAs<Atmosphere>();
            atmosphere->getParameters() = snapshot;
            atmosphere->updateParameters();
        }
    }

    AtmospherePanel::AtmospherePanel(Atmosphere& atmosphere) noexcept :
        IComponentPanel(atmosphere),
        mAtmosphere(atmosphere)
    {
    }

    void AtmospherePanel::onDraw()
    {
        // params 是直接指向 Atmosphere 内部存储的引用, 控件写的时候就是在原地改活的数据,
        // 不需要像 TransformPanel 那样"读一份局部拷贝、改完再调 setter 写回去"——
        // Atmosphere 本来就没有逐字段的 setter, 只有 updateParameters() 这一个把整份
        // mParameters 推给渲染 proxy 的统一入口。
        AtmosphereParameters& params = mAtmosphere.getParameters();

        const AtmosphereParameters& defaultParams = static_cast<const Atmosphere*>(Atmosphere::staticClass()->getDefaultObject())->getParameters();

        // 17 个字段共用同一个 undo payload 类型(整份 AtmosphereParameters)和同一个
        // setter, 不需要为每个字段各写一套——每个控件仍然各自按 ImGui 自己的 ItemID
        // 单独识别手势、单独产生一条 undo 记录(这是 ImGui 的机制决定的, 没法合并成一条),
        // 这里省的是"逐字段写不同类型/不同 setter"这件事。
        //
        // 不直接捕获 &mAtmosphere: 这个组件之后如果被删除又被撤销恢复, 恢复出来的是新地址的
        // 对象(见 Entity::attachComponent()), 直接捕获的引用到那时候就是悬空的——用
        // ComponentRef 存 Scene*/Entity Guid/Component Guid 这些稳定身份, 每次现查一遍。
        ComponentRef atmosphereRef = ComponentRef::capture(mAtmosphere);
        auto setter = [ref = atmosphereRef](const AtmosphereParameters& snapshot)
        {
            applyAtmosphereParameters(ref, snapshot);
        };

        auto track = [&](HString name, const AtmosphereParameters& before)
        {
            getUndoStack().trackGesture(name, before, params, setter);
        };

        // Reset 按钮点击是一次性的离散动作, 不是拖拽手势, 走普通 push() 而不是
        // trackGesture()——而且必须跟 track() 分开调用: DrawPropertyReset() 内部只有值
        // 不等于默认值时才会真的画一个 Button, 这个 Button 一旦画出来就成了 ImGui 的
        // "上一个 item", 如果紧跟着调 track() 的话, trackGesture() 内部
        // GetItemID()/IsItemActivated()/IsItemDeactivatedAfterEdit() 读到的就是这个
        // Reset 按钮的状态而不是前面那个 DragFloat/ColorEdit 的状态, 拖拽手势的 undo 记录
        // 就废了(这正是之前发现"大气属性 undo 不起作用"的原因)。所以 track() 必须紧跟在
        // 值控件之后立刻调用, Reset 按钮永远放在 track() 之后单独处理。
        auto applyReset = [&](HString name, const AtmosphereParameters& before)
        {
            getUndoStack().push(name,
                [ref = atmosphereRef, snapshot = before]() { applyAtmosphereParameters(ref, snapshot); },
                [ref = atmosphereRef, snapshot = params]() { applyAtmosphereParameters(ref, snapshot); });
        };

        if (!BeginPropertyTable("AtmosphereProperties", ImGuiTableFlags_BordersInnerH))
            return;

        BeginPropertyRow("Ground Albedo");
        AtmosphereParameters beforeDrag = params;
        if (LinearColorEdit3("##GroundAlbedo", &params.groundAlbedo.x))
            mAtmosphere.updateParameters();
        track("Change Ground Albedo"_hs, beforeDrag);
        AtmosphereParameters beforeReset = params;
        if (DrawPropertyReset("Ground Albedo", params.groundAlbedo, defaultParams.groundAlbedo))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Ground Albedo"_hs, beforeReset);
        }

        BeginPropertyRow("Ground Radius");
        beforeDrag = params;
        if (ImGui::DragFloat("##GroundRadiusKm", &params.groundRadiusKm, 10.0f, 0.0f, 0.0f, "%.1f"))
            mAtmosphere.updateParameters();
        track("Change Ground Radius"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Ground Radius", params.groundRadiusKm, defaultParams.groundRadiusKm))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Ground Radius"_hs, beforeReset);
        }

        BeginPropertyRow("Atmosphere Height");
        beforeDrag = params;
        if (ImGui::DragFloat("##AtmosphereHeightKm", &params.atmosphereHeightKm, 1.0f, 0.0f, 0.0f, "%.1f"))
            mAtmosphere.updateParameters();
        track("Change Atmosphere Height"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Atmosphere Height", params.atmosphereHeightKm, defaultParams.atmosphereHeightKm))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Atmosphere Height"_hs, beforeReset);
        }

        BeginPropertyRow("Multi Scattering Factor");
        beforeDrag = params;
        if (ImGui::DragFloat("##MultiScatteringFactor", &params.multiScatteringFactor, 0.01f, 0.0f, 0.0f, "%.3f"))
            mAtmosphere.updateParameters();
        track("Change Multi Scattering Factor"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Multi Scattering Factor", params.multiScatteringFactor, defaultParams.multiScatteringFactor))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Multi Scattering Factor"_hs, beforeReset);
        }

        BeginPropertyRow("Rayleigh Scattering Scale");
        beforeDrag = params;
        if (ImGui::DragFloat("##RayleighScatteringScale", &params.rayleighScatteringScale, 0.0001f, 0.0f, 0.0f, "%.5f"))
            mAtmosphere.updateParameters();
        track("Change Rayleigh Scattering Scale"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Rayleigh Scattering Scale", params.rayleighScatteringScale, defaultParams.rayleighScatteringScale))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Rayleigh Scattering Scale"_hs, beforeReset);
        }

        BeginPropertyRow("Rayleigh Scattering");
        beforeDrag = params;
        if (LinearColorEdit3("##RayleighScattering", &params.rayleighScattering.x))
            mAtmosphere.updateParameters();
        track("Change Rayleigh Scattering"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Rayleigh Scattering", params.rayleighScattering, defaultParams.rayleighScattering))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Rayleigh Scattering"_hs, beforeReset);
        }

        BeginPropertyRow("Rayleigh Scale Height");
        beforeDrag = params;
        if (ImGui::DragFloat("##RayleighScaleHeight", &params.rayleighScaleHeight, 0.1f, 0.0f, 0.0f, "%.2f"))
            mAtmosphere.updateParameters();
        track("Change Rayleigh Scale Height"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Rayleigh Scale Height", params.rayleighScaleHeight, defaultParams.rayleighScaleHeight))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Rayleigh Scale Height"_hs, beforeReset);
        }

        BeginPropertyRow("Mie Scattering Scale");
        beforeDrag = params;
        if (ImGui::DragFloat("##MieScatteringScale", &params.mieScatteringScale, 0.0001f, 0.0f, 0.0f, "%.5f"))
            mAtmosphere.updateParameters();
        track("Change Mie Scattering Scale"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Mie Scattering Scale", params.mieScatteringScale, defaultParams.mieScatteringScale))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Mie Scattering Scale"_hs, beforeReset);
        }

        BeginPropertyRow("Mie Scattering");
        beforeDrag = params;
        if (LinearColorEdit3("##MieScattering", &params.mieScattering.x))
            mAtmosphere.updateParameters();
        track("Change Mie Scattering"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Mie Scattering", params.mieScattering, defaultParams.mieScattering))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Mie Scattering"_hs, beforeReset);
        }

        BeginPropertyRow("Mie Absorption Scale");
        beforeDrag = params;
        if (ImGui::DragFloat("##MieAbsorptionScale", &params.mieAbsorptionScale, 0.00001f, 0.0f, 0.0f, "%.6f"))
            mAtmosphere.updateParameters();
        track("Change Mie Absorption Scale"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Mie Absorption Scale", params.mieAbsorptionScale, defaultParams.mieAbsorptionScale))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Mie Absorption Scale"_hs, beforeReset);
        }

        BeginPropertyRow("Mie Absorption");
        beforeDrag = params;
        if (LinearColorEdit3("##MieAbsorption", &params.mieAbsorption.x))
            mAtmosphere.updateParameters();
        track("Change Mie Absorption"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Mie Absorption", params.mieAbsorption, defaultParams.mieAbsorption))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Mie Absorption"_hs, beforeReset);
        }

        BeginPropertyRow("Mie Anisotropy");
        beforeDrag = params;
        if (ImGui::DragFloat("##MieAnisotropy", &params.mieAnisotropy, 0.01f, -0.999f, 0.999f, "%.3f"))
            mAtmosphere.updateParameters();
        track("Change Mie Anisotropy"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Mie Anisotropy", params.mieAnisotropy, defaultParams.mieAnisotropy))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Mie Anisotropy"_hs, beforeReset);
        }

        BeginPropertyRow("Mie Scale Height");
        beforeDrag = params;
        if (ImGui::DragFloat("##MieScaleHeight", &params.mieScaleHeight, 0.05f, 0.0f, 0.0f, "%.2f"))
            mAtmosphere.updateParameters();
        track("Change Mie Scale Height"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Mie Scale Height", params.mieScaleHeight, defaultParams.mieScaleHeight))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Mie Scale Height"_hs, beforeReset);
        }

        BeginPropertyRow("Ozone Absorption Scale");
        beforeDrag = params;
        if (ImGui::DragFloat("##OzoneAbsorptionScale", &params.ozoneAbsorptionScale, 0.0001f, 0.0f, 0.0f, "%.5f"))
            mAtmosphere.updateParameters();
        track("Change Ozone Absorption Scale"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Ozone Absorption Scale", params.ozoneAbsorptionScale, defaultParams.ozoneAbsorptionScale))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Ozone Absorption Scale"_hs, beforeReset);
        }

        BeginPropertyRow("Ozone Absorption");
        beforeDrag = params;
        if (LinearColorEdit3("##OzoneAbsorption", &params.ozoneAbsorption.x))
            mAtmosphere.updateParameters();
        track("Change Ozone Absorption"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Ozone Absorption", params.ozoneAbsorption, defaultParams.ozoneAbsorption))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Ozone Absorption"_hs, beforeReset);
        }

        BeginPropertyRow("Ozone Height");
        beforeDrag = params;
        if (ImGui::DragFloat("##OzoneHeight", &params.ozoneHeight, 0.5f, 0.0f, 0.0f, "%.2f"))
            mAtmosphere.updateParameters();
        track("Change Ozone Height"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Ozone Height", params.ozoneHeight, defaultParams.ozoneHeight))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Ozone Height"_hs, beforeReset);
        }

        BeginPropertyRow("Ozone Thickness");
        beforeDrag = params;
        if (ImGui::DragFloat("##OzoneThickness", &params.ozoneThickness, 0.5f, 0.0f, 0.0f, "%.2f"))
            mAtmosphere.updateParameters();
        track("Change Ozone Thickness"_hs, beforeDrag);
        beforeReset = params;
        if (DrawPropertyReset("Ozone Thickness", params.ozoneThickness, defaultParams.ozoneThickness))
        {
            mAtmosphere.updateParameters();
            applyReset("Reset Ozone Thickness"_hs, beforeReset);
        }

        EndPropertyTable();
    }
}
