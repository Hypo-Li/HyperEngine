#include "SkyLightPanel.h"
#include "../AssetComboEd.h"
#include "../ThumbnailProvider.h"
#include "../ComponentRef.h"
#include "../UndoStack.h"
#include "ComponentPanelRegistry.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Engine/Context.h>
#include <Engine/Texture/TextureCube.h>

#include <HypImGui/ImGuiColorWidgets.h>
#include <HypImGui/ImGuiEnumWidgets.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

#include <algorithm>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<SkyLight, SkyLightPanel> gRegistrar;

        // 必须是 2 的幂(SkyLight::isValidPrefilterResolution() 强制要求), 固定挡位而不是
        // 自由拖拽的 DragInt。
        constexpr uint32_t kPrefilterResolutionOptions[] = { 128, 256, 512 };

        int prefilterResolutionToIndex(uint32_t resolution)
        {
            for (int i = 0; i < static_cast<int>(std::size(kPrefilterResolutionOptions)); ++i)
            {
                if (kPrefilterResolutionOptions[i] == resolution)
                    return i;
            }
            return 0;
        }

        // undo/redo 的 lambda 只捕获 Guid, 不捕获 TRef——资产可能在此期间被卸载/重新加载。
        TRef<TextureCube> resolveTextureCubeAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<TextureCube>() : TRef<TextureCube>();
        }
    }

    SkyLightPanel::SkyLightPanel(SkyLight& skyLight) noexcept :
        IComponentPanel(skyLight),
        mSkyLight(skyLight)
    {
    }

    void SkyLightPanel::onDraw()
    {
        AssetManager& assetManager = Context::get().getAssetManager();
        // 不直接捕获 &mSkyLight——被删除又撤销恢复后地址会变, ComponentRef 存稳定身份现查。
        ComponentRef skyLightRef = ComponentRef::capture(mSkyLight);

        auto pushReset = [&](HString name, auto setter, const auto& before, const auto& after)
        {
            getUndoStack().push(name,
                [setter, before]() mutable { setter(before); },
                [setter, after]() mutable { setter(after); });
        };

        if (!BeginPropertyTable("SkyLightProperties", ImGuiTableFlags_BordersInnerH))
            return;

        auto& ddl = *static_cast<const SkyLight*>(SkyLight::staticClass()->getDefaultObject());

        BeginPropertyRow("Light Source");
        SkyLightSource sourceBefore = mSkyLight.getLightSource();
        SkyLightSource source = sourceBefore;
        bool sourceChanged = EnumCombo("##LightSource", &source);
        auto sourceSetter = [ref = skyLightRef](SkyLightSource v) { ref.resolveAs<SkyLight>()->setLightSource(v); };
        getUndoStack().trackGesture("Change Light Source"_hs, sourceBefore, source, sourceSetter);
        if (sourceChanged)
            mSkyLight.setLightSource(source);
        SkyLightSource sourceBeforeReset = source;
        if (DrawPropertyReset("Light Source", source, ddl.getLightSource()))
        {
            mSkyLight.setLightSource(source);
            pushReset("Reset Light Source"_hs, sourceSetter, sourceBeforeReset, source);
        }

        BeginPropertyRow("Environment Map", 2.0f); // AssetCombo 是两行缩略图布局
        TextureCube* environmentMap = mSkyLight.getEnvironmentMap();
        Guid environmentMapGuidBefore = environmentMap ? environmentMap->getGuid() : Guid();
        Guid environmentMapGuid = environmentMapGuidBefore;
        bool environmentMapChanged = AssetComboEd(
            "##EnvironmentMap", assetManager, TextureCube::staticClass(), false, environmentMapGuid, getAssetBrowser(), getIconProvider());
        environmentMapChanged |= DrawPropertyReset("Environment Map", environmentMapGuid, Guid());
        if (environmentMapChanged)
        {
            mSkyLight.setEnvironmentMap(resolveTextureCubeAsset(assetManager, environmentMapGuid).get());
            getUndoStack().push("Change Environment Map"_hs,
                [ref = skyLightRef, guid = environmentMapGuidBefore]()
                {
                    ref.resolveAs<SkyLight>()->setEnvironmentMap(resolveTextureCubeAsset(Context::get().getAssetManager(), guid).get());
                },
                [ref = skyLightRef, guid = environmentMapGuid]()
                {
                    ref.resolveAs<SkyLight>()->setEnvironmentMap(resolveTextureCubeAsset(Context::get().getAssetManager(), guid).get());
                });
        }

        BeginPropertyRow("Intensity");
        float intensityBefore = mSkyLight.getIntensity();
        float intensity = intensityBefore;
        bool intensityChanged = ImGui::DragFloat("##Intensity", &intensity, 0.05f, 0.0f, 20.0f, "%.3f");
        auto intensitySetter = [ref = skyLightRef](float v) { ref.resolveAs<SkyLight>()->setIntensity(v); };
        getUndoStack().trackGesture("Change Intensity"_hs, intensityBefore, intensity, intensitySetter);
        if (intensityChanged)
            mSkyLight.setIntensity(intensity);
        float intensityBeforeReset = intensity;
        if (DrawPropertyReset("Intensity", intensity, ddl.getIntensity()))
        {
            mSkyLight.setIntensity(intensity);
            pushReset("Reset Intensity"_hs, intensitySetter, intensityBeforeReset, intensity);
        }

        BeginPropertyRow("Color");
        float3 colorBefore = mSkyLight.getColor();
        float3 color = colorBefore;
        bool colorChanged = LinearColorEdit3("##Color", &color.x);
        auto colorSetter = [ref = skyLightRef](const float3& v) { ref.resolveAs<SkyLight>()->setColor(v); };
        getUndoStack().trackGesture("Change Color"_hs, colorBefore, color, colorSetter);
        if (colorChanged)
            mSkyLight.setColor(color);
        float3 colorBeforeReset = color;
        if (DrawPropertyReset("Color", color, ddl.getColor()))
        {
            mSkyLight.setColor(color);
            pushReset("Reset Color"_hs, colorSetter, colorBeforeReset, color);
        }

        // 只在 Captured Sky 模式下有效, 见 SkyLight::isRealTimeCapture() 的注释。
        bool realTimeCaptureRelevant = source == SkyLightSource::Captured_Sky;
        ImGui::BeginDisabled(!realTimeCaptureRelevant);
        BeginPropertyRow("Real Time Capture");
        bool realTimeCaptureBefore = mSkyLight.isRealTimeCapture();
        bool realTimeCapture = realTimeCaptureBefore;
        bool realTimeCaptureChanged = ImGui::Checkbox("##RealTimeCapture", &realTimeCapture);
        auto realTimeCaptureSetter = [ref = skyLightRef](bool v) { ref.resolveAs<SkyLight>()->setRealTimeCapture(v); };
        getUndoStack().trackGesture("Change Real Time Capture"_hs, realTimeCaptureBefore, realTimeCapture, realTimeCaptureSetter);
        if (realTimeCaptureChanged)
            mSkyLight.setRealTimeCapture(realTimeCapture);
        bool realTimeCaptureBeforeReset = realTimeCapture;
        if (DrawPropertyReset("Real Time Capture", realTimeCapture, ddl.isRealTimeCapture()))
        {
            mSkyLight.setRealTimeCapture(realTimeCapture);
            pushReset("Reset Real Time Capture"_hs, realTimeCaptureSetter, realTimeCaptureBeforeReset, realTimeCapture);
        }
        ImGui::EndDisabled();

        BeginPropertyRow("Prefilter Resolution");
        uint32_t prefilterResolutionBefore = mSkyLight.getPrefilterResolution();
        int prefilterResolutionIndex = prefilterResolutionToIndex(prefilterResolutionBefore);
        bool prefilterResolutionChanged = ImGui::Combo("##PrefilterResolution", &prefilterResolutionIndex, "128\0""256\0""512\0");
        uint32_t prefilterResolution = kPrefilterResolutionOptions[prefilterResolutionIndex];
        auto prefilterResolutionSetter = [ref = skyLightRef](uint32_t v) { ref.resolveAs<SkyLight>()->setPrefilterResolution(v); };
        getUndoStack().trackGesture("Change Prefilter Resolution"_hs, prefilterResolutionBefore, prefilterResolution, prefilterResolutionSetter);
        if (prefilterResolutionChanged)
            mSkyLight.setPrefilterResolution(prefilterResolution);
        uint32_t prefilterResolutionBeforeReset = prefilterResolution;
        if (DrawPropertyReset("Prefilter Resolution", prefilterResolution, ddl.getPrefilterResolution()))
        {
            mSkyLight.setPrefilterResolution(prefilterResolution);
            pushReset("Reset Prefilter Resolution"_hs, prefilterResolutionSetter, prefilterResolutionBeforeReset, prefilterResolution);
        }

        // 动作而非属性, 没有 before/after 状态, 不走 undo。
        ImGui::BeginDisabled(realTimeCaptureRelevant && realTimeCapture);
        BeginPropertyRow("Recapture");
        if (ImGui::Button("Recapture"))
            mSkyLight.recapture();
        ImGui::EndDisabled();

        EndPropertyTable();
    }
}
