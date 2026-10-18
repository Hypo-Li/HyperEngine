#include "DirectionalLightPanel.h"
#include "../ComponentRef.h"
#include "../UndoStack.h"
#include "ComponentPanelRegistry.h"

#include <Core/Reflection/Class.h>

#include <HypImGui/ImGuiColorWidgets.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

#include <algorithm>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<DirectionalLight, DirectionalLightPanel> gRegistrar;

        // 固定挡位: atlas 按最多 4 列打包(DirectionalLightProxy::CascadeAtlasColumnCount),
        // 4k 挡位对应 16384 宽的 atlas。
        constexpr int kCascadeResolutionOptions[] = { 1024, 2048, 4096 };

        int cascadeResolutionToIndex(int resolution)
        {
            for (int i = 0; i < static_cast<int>(std::size(kCascadeResolutionOptions)); ++i)
            {
                if (kCascadeResolutionOptions[i] == resolution)
                    return i;
            }
            return 0;
        }

        // Cascade Count/Resolution/ShadowDistance/DistributionExponent/DepthBias/SlopeBias/CascadeBiasDistribution
        // 这 7 个字段在 DirectionalLight 那边是靠 setShadowParameters() 一次性批量设置的
        // (跟 DirectionalLightProxy::setShadowParameters() 的分组一致), 不是各自独立的
        // setter——所以 undo 的 payload 也用一份把 7 个值合在一起的快照, 而不是逐个字段
        // 各写一套, 跟 AtmospherePanel 把整份 AtmosphereParameters 当 payload 是同一个思路。
        struct ShadowSnapshot
        {
            int cascadeCount;
            int cascadeResolution;
            float shadowDistance;
            float distributionExponent;
            float depthBias;
            float slopeBias;
            float cascadeBiasDistribution;

            friend bool operator==(const ShadowSnapshot&, const ShadowSnapshot&) = default;
        };

        ShadowSnapshot captureShadowSnapshot(DirectionalLight& light)
        {
            return {
                static_cast<int>(light.getCascadeCount()),
                static_cast<int>(light.getCascadeResolution()),
                light.getShadowDistance(),
                light.getDistributionExponent(),
                light.getDepthBias(),
                light.getSlopeBias(),
                light.getCascadeBiasDistribution()
            };
        }

        void applyShadowSnapshot(DirectionalLight& light, const ShadowSnapshot& s)
        {
            light.setShadowParameters(
                static_cast<uint32_t>(s.cascadeCount),
                static_cast<uint32_t>(s.cascadeResolution),
                s.shadowDistance,
                s.distributionExponent,
                s.depthBias,
                s.slopeBias,
                s.cascadeBiasDistribution
            );
        }
    }

    DirectionalLightPanel::DirectionalLightPanel(DirectionalLight& light) noexcept :
        IComponentPanel(light),
        mLight(light)
    {
    }

    void DirectionalLightPanel::onDraw()
    {
        // 不直接捕获 &mLight: 这个组件之后如果被删除又被撤销恢复, 恢复出来的是新地址的对象
        // (见 Entity::attachComponent()), 直接捕获的引用到那时候就是悬空的——用 ComponentRef
        // 存 Scene*/Entity Guid/Component Guid 这些稳定身份, 每次现查一遍。
        ComponentRef lightRef = ComponentRef::capture(mLight);

        auto trackShadow = [&](HString name, const ShadowSnapshot& before, const ShadowSnapshot& current)
        {
            getUndoStack().trackGesture(name, before, current,
                [ref = lightRef](const ShadowSnapshot& snapshot) { applyShadowSnapshot(*ref.resolveAs<DirectionalLight>(), snapshot); });
        };

        // Reset 按钮点击是一次性的离散动作, 不是拖拽手势, 走普通 push() 而不是
        // trackGesture()——而且必须跟上面的 trackGesture()/track 分开调用, 顺序不能提前:
        // DrawPropertyReset() 内部只有值不等于默认值时才会真的画一个 Button, 这个 Button 一旦
        // 画出来就成了 ImGui 的"上一个 item", 如果紧跟着调 trackGesture() 的话, 内部
        // GetItemID()/IsItemActivated()/IsItemDeactivatedAfterEdit() 读到的就是这个 Reset
        // 按钮的状态而不是前面那个 DragFloat/ColorEdit/Checkbox 的状态, 拖拽/勾选手势的 undo
        // 记录就废了(这正是 AtmospherePanel 和这个面板都各自踩过一次的坑)。所以每个字段必须
        // 先调 trackGesture(), Reset 按钮永远放在它之后单独处理, 用这个 pushReset 走 push()。
        auto pushReset = [&](HString name, auto setter, const auto& before, const auto& after)
        {
            getUndoStack().push(name,
                [setter, before]() mutable { setter(before); },
                [setter, after]() mutable { setter(after); });
        };

        if (!BeginPropertyTable("DirectionalLightProperties", ImGuiTableFlags_BordersInnerH))
            return;

        auto& ddl = *static_cast<const DirectionalLight*>(DirectionalLight::staticClass()->getDefaultObject());

        BeginPropertyRow("Color");
        float3 colorBefore = mLight.getColor();
        float3 color = colorBefore;
        bool colorChanged = LinearColorEdit3("##Color", &color.x);
        auto colorSetter = [ref = lightRef](const float3& v) { ref.resolveAs<DirectionalLight>()->setColor(v); };
        getUndoStack().trackGesture("Change Color"_hs, colorBefore, color, colorSetter);
        if (colorChanged)
            mLight.setColor(color);
        float3 colorBeforeReset = color;
        if (DrawPropertyReset("Color", color, ddl.getColor()))
        {
            mLight.setColor(color);
            pushReset("Reset Color"_hs, colorSetter, colorBeforeReset, color);
        }

        BeginPropertyRow("Intensity");
        float intensityBefore = mLight.getIntensity();
        float intensity = intensityBefore;
        bool intensityChanged = ImGui::DragFloat("##Intensity", &intensity, 0.05f, 0.0f, 0.0f, "%.3f");
        auto intensitySetter = [ref = lightRef](float v) { ref.resolveAs<DirectionalLight>()->setIntensity(v); };
        getUndoStack().trackGesture("Change Intensity"_hs, intensityBefore, intensity, intensitySetter);
        if (intensityChanged)
            mLight.setIntensity(intensity);
        float intensityBeforeReset = intensity;
        if (DrawPropertyReset("Intensity", intensity, ddl.getIntensity()))
        {
            mLight.setIntensity(intensity);
            pushReset("Reset Intensity"_hs, intensitySetter, intensityBeforeReset, intensity);
        }

        BeginPropertyRow("Cast Shadows");
        bool castShadowsBefore = mLight.isCastShadows();
        bool castShadows = castShadowsBefore;
        bool castShadowsChanged = ImGui::Checkbox("##CastShadows", &castShadows);
        auto castShadowsSetter = [ref = lightRef](bool v) { ref.resolveAs<DirectionalLight>()->setCastShadows(v); };
        getUndoStack().trackGesture("Change Cast Shadows"_hs, castShadowsBefore, castShadows, castShadowsSetter);
        if (castShadowsChanged)
            mLight.setCastShadows(castShadows);
        bool castShadowsBeforeReset = castShadows;
        if (DrawPropertyReset("Cast Shadows", castShadows, ddl.isCastShadows()))
        {
            mLight.setCastShadows(castShadows);
            pushReset("Reset Cast Shadows"_hs, castShadowsSetter, castShadowsBeforeReset, castShadows);
        }

        BeginPropertyRow("Affect Atmosphere");
        bool affectAtmosphereBefore = mLight.isAffectAtmosphere();
        bool affectAtmosphere = affectAtmosphereBefore;
        bool affectAtmosphereChanged = ImGui::Checkbox("##AffectAtmosphere", &affectAtmosphere);
        auto affectAtmosphereSetter = [ref = lightRef](bool v) { ref.resolveAs<DirectionalLight>()->setAffectAtmosphere(v); };
        getUndoStack().trackGesture("Change Affect Atmosphere"_hs, affectAtmosphereBefore, affectAtmosphere, affectAtmosphereSetter);
        if (affectAtmosphereChanged)
            mLight.setAffectAtmosphere(affectAtmosphere);
        bool affectAtmosphereBeforeReset = affectAtmosphere;
        if (DrawPropertyReset("Affect Atmosphere", affectAtmosphere, ddl.isAffectAtmosphere()))
        {
            mLight.setAffectAtmosphere(affectAtmosphere);
            pushReset("Reset Affect Atmosphere"_hs, affectAtmosphereSetter, affectAtmosphereBeforeReset, affectAtmosphere);
        }

        BeginPropertyRow("Atmosphere Sun Light Index");
        int sunIndexBefore = static_cast<int>(mLight.getAtmosphereSunLightIndex());
        int sunIndex = sunIndexBefore;
        bool sunIndexChanged = ImGui::SliderInt("##AtmosphereSunLightIndex", &sunIndex, 0, 1);
        auto sunIndexSetter = [ref = lightRef](int v) { ref.resolveAs<DirectionalLight>()->setAtmosphereSunLightIndex(static_cast<uint32_t>(v)); };
        getUndoStack().trackGesture("Change Atmosphere Sun Light Index"_hs, sunIndexBefore, sunIndex, sunIndexSetter);
        if (sunIndexChanged)
            mLight.setAtmosphereSunLightIndex(static_cast<uint32_t>(sunIndex));
        int sunIndexBeforeReset = sunIndex;
        if (DrawPropertyReset("Atmosphere Sun Light Index", sunIndex, static_cast<int>(ddl.getAtmosphereSunLightIndex())))
        {
            mLight.setAtmosphereSunLightIndex(static_cast<uint32_t>(sunIndex));
            pushReset("Reset Atmosphere Sun Light Index"_hs, sunIndexSetter, sunIndexBeforeReset, sunIndex);
        }

        auto shadowSetter = [ref = lightRef](const ShadowSnapshot& s) { applyShadowSnapshot(*ref.resolveAs<DirectionalLight>(), s); };

        BeginPropertyRow("Cascade Count");
        ShadowSnapshot shadowBefore = captureShadowSnapshot(mLight);
        int cascadeCount = shadowBefore.cascadeCount;
        bool cascadeCountChanged = ImGui::DragInt("##CascadeCount", &cascadeCount, 1.0f, 1, 8);
        cascadeCount = std::clamp(cascadeCount, 1, 8);
        ShadowSnapshot cascadeCountCurrent = shadowBefore;
        cascadeCountCurrent.cascadeCount = cascadeCount;
        trackShadow("Change Cascade Count"_hs, shadowBefore, cascadeCountCurrent);
        if (cascadeCountChanged)
            applyShadowSnapshot(mLight, cascadeCountCurrent);
        ShadowSnapshot cascadeCountBeforeReset = cascadeCountCurrent;
        if (DrawPropertyReset("Cascade Count", cascadeCountCurrent.cascadeCount, static_cast<int>(ddl.getCascadeCount())))
        {
            applyShadowSnapshot(mLight, cascadeCountCurrent);
            pushReset("Reset Cascade Count"_hs, shadowSetter, cascadeCountBeforeReset, cascadeCountCurrent);
        }

        BeginPropertyRow("Cascade Resolution");
        shadowBefore = captureShadowSnapshot(mLight);
        int cascadeResolutionIndex = cascadeResolutionToIndex(shadowBefore.cascadeResolution);
        bool cascadeResolutionChanged = ImGui::Combo("##CascadeResolution", &cascadeResolutionIndex, "1024\0""2048\0""4096\0");
        int cascadeResolution = kCascadeResolutionOptions[cascadeResolutionIndex];
        ShadowSnapshot cascadeResolutionCurrent = shadowBefore;
        cascadeResolutionCurrent.cascadeResolution = cascadeResolution;
        trackShadow("Change Cascade Resolution"_hs, shadowBefore, cascadeResolutionCurrent);
        if (cascadeResolutionChanged)
            applyShadowSnapshot(mLight, cascadeResolutionCurrent);
        ShadowSnapshot cascadeResolutionBeforeReset = cascadeResolutionCurrent;
        if (DrawPropertyReset("Cascade Resolution", cascadeResolutionCurrent.cascadeResolution, static_cast<int>(ddl.getCascadeResolution())))
        {
            applyShadowSnapshot(mLight, cascadeResolutionCurrent);
            pushReset("Reset Cascade Resolution"_hs, shadowSetter, cascadeResolutionBeforeReset, cascadeResolutionCurrent);
        }

        BeginPropertyRow("Shadow Distance");
        shadowBefore = captureShadowSnapshot(mLight);
        float shadowDistance = shadowBefore.shadowDistance;
        bool shadowDistanceChanged = ImGui::DragFloat("##ShadowDistance", &shadowDistance, 1.0f, 0.0f, 0.0f, "%.1f");
        ShadowSnapshot shadowDistanceCurrent = shadowBefore;
        shadowDistanceCurrent.shadowDistance = shadowDistance;
        trackShadow("Change Shadow Distance"_hs, shadowBefore, shadowDistanceCurrent);
        if (shadowDistanceChanged)
            applyShadowSnapshot(mLight, shadowDistanceCurrent);
        ShadowSnapshot shadowDistanceBeforeReset = shadowDistanceCurrent;
        if (DrawPropertyReset("Shadow Distance", shadowDistanceCurrent.shadowDistance, ddl.getShadowDistance()))
        {
            applyShadowSnapshot(mLight, shadowDistanceCurrent);
            pushReset("Reset Shadow Distance"_hs, shadowSetter, shadowDistanceBeforeReset, shadowDistanceCurrent);
        }

        BeginPropertyRow("Distribution Exponent");
        shadowBefore = captureShadowSnapshot(mLight);
        float distributionExponent = shadowBefore.distributionExponent;
        bool distributionExponentChanged = ImGui::DragFloat("##DistributionExponent", &distributionExponent, 0.05f, 1.0f, 4.0f, "%.2f");
        distributionExponent = std::clamp(distributionExponent, 1.0f, 4.0f);
        ShadowSnapshot distributionExponentCurrent = shadowBefore;
        distributionExponentCurrent.distributionExponent = distributionExponent;
        trackShadow("Change Distribution Exponent"_hs, shadowBefore, distributionExponentCurrent);
        if (distributionExponentChanged)
            applyShadowSnapshot(mLight, distributionExponentCurrent);
        ShadowSnapshot distributionExponentBeforeReset = distributionExponentCurrent;
        if (DrawPropertyReset("Distribution Exponent", distributionExponentCurrent.distributionExponent, ddl.getDistributionExponent()))
        {
            applyShadowSnapshot(mLight, distributionExponentCurrent);
            pushReset("Reset Distribution Exponent"_hs, shadowSetter, distributionExponentBeforeReset, distributionExponentCurrent);
        }

        BeginPropertyRow("Depth Bias");
        shadowBefore = captureShadowSnapshot(mLight);
        float depthBias = shadowBefore.depthBias;
        bool depthBiasChanged = ImGui::DragFloat("##DepthBias", &depthBias, 0.01f, 0.0f, 0.0f, "%.3f");
        ShadowSnapshot depthBiasCurrent = shadowBefore;
        depthBiasCurrent.depthBias = depthBias;
        trackShadow("Change Depth Bias"_hs, shadowBefore, depthBiasCurrent);
        if (depthBiasChanged)
            applyShadowSnapshot(mLight, depthBiasCurrent);
        ShadowSnapshot depthBiasBeforeReset = depthBiasCurrent;
        if (DrawPropertyReset("Depth Bias", depthBiasCurrent.depthBias, ddl.getDepthBias()))
        {
            applyShadowSnapshot(mLight, depthBiasCurrent);
            pushReset("Reset Depth Bias"_hs, shadowSetter, depthBiasBeforeReset, depthBiasCurrent);
        }

        BeginPropertyRow("Slope Bias");
        shadowBefore = captureShadowSnapshot(mLight);
        float slopeBias = shadowBefore.slopeBias;
        bool slopeBiasChanged = ImGui::DragFloat("##SlopeBias", &slopeBias, 0.01f, 0.0f, 0.0f, "%.3f");
        ShadowSnapshot slopeBiasCurrent = shadowBefore;
        slopeBiasCurrent.slopeBias = slopeBias;
        trackShadow("Change Slope Bias"_hs, shadowBefore, slopeBiasCurrent);
        if (slopeBiasChanged)
            applyShadowSnapshot(mLight, slopeBiasCurrent);
        ShadowSnapshot slopeBiasBeforeReset = slopeBiasCurrent;
        if (DrawPropertyReset("Slope Bias", slopeBiasCurrent.slopeBias, ddl.getSlopeBias()))
        {
            applyShadowSnapshot(mLight, slopeBiasCurrent);
            pushReset("Reset Slope Bias"_hs, shadowSetter, slopeBiasBeforeReset, slopeBiasCurrent);
        }

        BeginPropertyRow("Cascade Bias Distribution");
        shadowBefore = captureShadowSnapshot(mLight);
        float cascadeBiasDistribution = shadowBefore.cascadeBiasDistribution;
        bool cascadeBiasDistributionChanged = ImGui::DragFloat(
            "##CascadeBiasDistribution",
            &cascadeBiasDistribution,
            0.01f,
            0.0f,
            1.0f,
            "%.3f"
        );
        cascadeBiasDistribution = std::clamp(cascadeBiasDistribution, 0.0f, 1.0f);
        ShadowSnapshot cascadeBiasDistributionCurrent = shadowBefore;
        cascadeBiasDistributionCurrent.cascadeBiasDistribution = cascadeBiasDistribution;
        trackShadow(
            "Change Cascade Bias Distribution"_hs,
            shadowBefore,
            cascadeBiasDistributionCurrent
        );
        if (cascadeBiasDistributionChanged)
            applyShadowSnapshot(mLight, cascadeBiasDistributionCurrent);
        ShadowSnapshot cascadeBiasDistributionBeforeReset = cascadeBiasDistributionCurrent;
        if (DrawPropertyReset(
            "Cascade Bias Distribution",
            cascadeBiasDistributionCurrent.cascadeBiasDistribution,
            ddl.getCascadeBiasDistribution()
        ))
        {
            applyShadowSnapshot(mLight, cascadeBiasDistributionCurrent);
            pushReset(
                "Reset Cascade Bias Distribution"_hs,
                shadowSetter,
                cascadeBiasDistributionBeforeReset,
                cascadeBiasDistributionCurrent
            );
        }

        BeginPropertyRow("Cascade Transition Fraction");
        float transitionFractionBefore = mLight.getTransitionFraction();
        float transitionFraction = transitionFractionBefore;
        bool transitionFractionChanged = ImGui::DragFloat("##TransitionFraction", &transitionFraction, 0.005f, 0.0f, 0.5f, "%.3f");
        transitionFraction = std::clamp(transitionFraction, 0.0f, 0.5f);
        auto transitionFractionSetter = [ref = lightRef](float v) { ref.resolveAs<DirectionalLight>()->setTransitionFraction(v); };
        getUndoStack().trackGesture("Change Cascade Transition Fraction"_hs, transitionFractionBefore, transitionFraction, transitionFractionSetter);
        if (transitionFractionChanged)
            mLight.setTransitionFraction(transitionFraction);
        float transitionFractionBeforeReset = transitionFraction;
        if (DrawPropertyReset("Cascade Transition Fraction", transitionFraction, ddl.getTransitionFraction()))
        {
            mLight.setTransitionFraction(transitionFraction);
            pushReset("Reset Cascade Transition Fraction"_hs, transitionFractionSetter, transitionFractionBeforeReset, transitionFraction);
        }

        BeginPropertyRow("Distance Fadeout Fraction");
        float distanceFadeoutFractionBefore = mLight.getDistanceFadeoutFraction();
        float distanceFadeoutFraction = distanceFadeoutFractionBefore;
        bool distanceFadeoutFractionChanged = ImGui::DragFloat("##DistanceFadeoutFraction", &distanceFadeoutFraction, 0.005f, 0.0f, 0.5f, "%.3f");
        distanceFadeoutFraction = std::clamp(distanceFadeoutFraction, 0.0f, 0.5f);
        auto distanceFadeoutFractionSetter = [ref = lightRef](float v) { ref.resolveAs<DirectionalLight>()->setDistanceFadeoutFraction(v); };
        getUndoStack().trackGesture("Change Distance Fadeout Fraction"_hs, distanceFadeoutFractionBefore, distanceFadeoutFraction, distanceFadeoutFractionSetter);
        if (distanceFadeoutFractionChanged)
            mLight.setDistanceFadeoutFraction(distanceFadeoutFraction);
        float distanceFadeoutFractionBeforeReset = distanceFadeoutFraction;
        if (DrawPropertyReset("Distance Fadeout Fraction", distanceFadeoutFraction, ddl.getDistanceFadeoutFraction()))
        {
            mLight.setDistanceFadeoutFraction(distanceFadeoutFraction);
            pushReset("Reset Distance Fadeout Fraction"_hs, distanceFadeoutFractionSetter, distanceFadeoutFractionBeforeReset, distanceFadeoutFraction);
        }

        BeginPropertyRow("Shadow Filter Quality");
        int filterQualityBefore = static_cast<int>(mLight.getShadowFilterQuality());
        int filterQuality = filterQualityBefore;
        bool filterQualityChanged = ImGui::Combo("##ShadowFilterQuality", &filterQuality, "None\0PCF 1x1\0PCF 3x3\0PCF 5x5\0");
        auto filterQualitySetter = [ref = lightRef](int v) { ref.resolveAs<DirectionalLight>()->setShadowFilterQuality(static_cast<ShadowFilterQuality>(v)); };
        getUndoStack().trackGesture("Change Shadow Filter Quality"_hs, filterQualityBefore, filterQuality, filterQualitySetter);
        if (filterQualityChanged)
            mLight.setShadowFilterQuality(static_cast<ShadowFilterQuality>(filterQuality));
        int filterQualityBeforeReset = filterQuality;
        if (DrawPropertyReset("Shadow Filter Quality", filterQuality, static_cast<int>(ddl.getShadowFilterQuality())))
        {
            mLight.setShadowFilterQuality(static_cast<ShadowFilterQuality>(filterQuality));
            pushReset("Reset Shadow Filter Quality"_hs, filterQualitySetter, filterQualityBeforeReset, filterQuality);
        }

        EndPropertyTable();
    }
}
