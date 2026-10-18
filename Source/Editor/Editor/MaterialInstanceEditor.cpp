#include "MaterialInstanceEditor.h"
#include "MaterialInstanceEditorTab.h"
#include "AssetComboEd.h"
#include "AssetEditorRegistry.h"
#include "ThumbnailProvider.h"
#include "UndoStack.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Core/Reflection/Reflection.h>
#include <Engine/Context.h>
#include <Engine/Material/MaterialTemplate.h>
#include <Engine/Texture/Texture2D.h>
#include <Engine/Texture/Texture2DArray.h>
#include <Engine/Texture/Texture3D.h>
#include <Engine/Texture/TextureCube.h>

#include <HypImGui/ImGuiAssetCombo.h>
#include <HypImGui/ImGuiColorWidgets.h>
#include <HypImGui/ImGuiEnumWidgets.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

#include <algorithm>
#include <vector>

namespace hyp::ed
{
    namespace
    {
        AssetEditorRegistrar<MaterialInstance, MaterialInstanceEditorTab> gRegistrar;
    }

    MaterialInstanceEditor::MaterialInstanceEditor(Asset& asset, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        AssetEditorWindow(asset, undoStack),
        mIconProvider(iconProvider),
        mAssetBrowser(assetBrowser),
        mMaterialInstance(asset.getOrLoadRootObject<MaterialInstance>())
    {
    }

    void MaterialInstanceEditor::onDraw()
    {
        drawTemplateRow();
        ImGui::Spacing();
        drawParameterList();
        ImGui::Spacing();
        drawSave();
    }

    void MaterialInstanceEditor::drawTemplateRow()
    {
        if (!BeginPropertyTable("MaterialInstanceProperties", ImGuiTableFlags_BordersInnerH))
            return;

        AssetManager& assetManager = Context::get().getAssetManager();

        // rowMultiplier 2.0f: AssetComboEd 传了非空 iconForAsset, 一律走两行缩略图布局(见
        // AssetComboEd.h 的说明), 跟 MaterialTemplateEditor 里纹理参数行用的高度是同一回事——
        // 传 1.0f(单行)的话 BeginPropertyRow() 会按单行高度摆 label, 实际控件却撑成了两行高,
        // label 文字就会贴在撑高之后那一行的顶部, 而不是真正居中。
        BeginPropertyRow("Template", 2.0f);
        MaterialTemplate* currentTemplate = mMaterialInstance->getTemplate();
        Guid guidBefore = currentTemplate ? currentTemplate->getGuid() : Guid();
        Guid guid = guidBefore;
        bool changed = AssetComboEd("##Template", assetManager, MaterialTemplate::staticClass(), false, guid, mAssetBrowser, mIconProvider);

        auto& mi = *mMaterialInstance;
        auto templateSetter = [&mi, &assetManager](const Guid& g)
        {
            Asset* asset = g.isValid() ? assetManager.getAsset(g) : nullptr;
            TRef<MaterialTemplate> t = asset ? asset->getOrLoadRootObject<MaterialTemplate>() : TRef<MaterialTemplate>();
            mi.setTemplate(t.get());
        };
        undoStack().trackGesture("Change Template"_hs, guidBefore, guid, templateSetter);
        if (changed)
        {
            templateSetter(guid);
            getAsset().setDirty(true);
        }

        EndPropertyTable();
    }

    void MaterialInstanceEditor::drawParameterList()
    {
        ImGui::SeparatorText("Material Parameters");

        MaterialTemplate* materialTemplate = mMaterialInstance->getTemplate();
        if (!materialTemplate)
        {
            ImGui::TextDisabled("Assign a template to edit parameters.");
            return;
        }

        AssetManager& assetManager = Context::get().getAssetManager();

        // 按组分桶展示, 只读——组的归属完全由模板决定, 这里只编辑 override 值, 不编辑分组。
        // 跟 MaterialTemplateEditor 不同, 这边参数集合直接来自 materialTemplate->getParameters()
        // (没有 pending 工作副本这层), 也没有稳定的"什么时候需要重算"信号来支撑一份跨帧缓存
        // (模板可能在另一个 MaterialTemplateEditor 窗口里被改分组, 这里探测不到)。归桶本身是
        // 一趟 O(参数数) 的线性扫描, 不是按组数重复过滤的 O(组数 x 参数数), 每帧重算一次这个
        // 开销量级可以接受, 不需要为了避免它而引入一份容易过期的缓存。
        std::span<const HString> groups = materialTemplate->getParameterGroups();
        std::span<const MaterialParameter> parameters = materialTemplate->getParameters();
        std::vector<std::vector<uint32_t>> buckets(groups.size() + 1);
        for (uint32_t index = 0; index < parameters.size(); ++index)
        {
            auto it = std::find(groups.begin(), groups.end(), parameters[index].group);
            size_t bucketIndex = it != groups.end()
                ? static_cast<size_t>(std::distance(groups.begin(), it))
                : groups.size();
            buckets[bucketIndex].push_back(index);
        }

        auto drawBucket = [&](const char* label, const std::vector<uint32_t>& indices)
        {
            if (indices.empty())
                return;

            ImGui::PushID(label);
            bool expanded = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
            if (expanded && BeginPropertyTable("MaterialInstanceParameters", ImGuiTableFlags_BordersInnerH))
            {
                for (uint32_t index : indices)
                {
                    const MaterialParameter& parameter = parameters[index];
                    bool isTextureParam = parameter.value.getType() == MaterialParameterType::Texture_2D
                        || parameter.value.getType() == MaterialParameterType::Texture_2D_Array
                        || parameter.value.getType() == MaterialParameterType::Texture_3D
                        || parameter.value.getType() == MaterialParameterType::Texture_Cube
                        || parameter.value.getType() == MaterialParameterType::Font;

                    ImGui::PushID(parameter.name.toCString());

                    // ""是因为这一行的Label列不能只是纯文字——checkbox要跟参数名放在同一列里,
                    // 得自己手动画。上面的PushID(parameter.name)已经把BeginPropertyRow内部自己
                    // 那个PushID(label)(分隔条用来防同一张表内ID冲突的)套进了外层唯一作用域,
                    // 所以这里传空字符串不会导致不同参数行之间的分隔条ID冲突。
                    BeginPropertyRow("", isTextureParam ? 2.0f : 1.0f);
                    float rowHeight = GetCurrentPropertyRowHeight();

                    const MaterialInstance::ParameterOverride* existing = mMaterialInstance->getParameterOverride(parameter.name);
                    bool enabledBefore = existing && existing->enabled;
                    bool enabled = enabledBefore;

                    ImGui::TableSetColumnIndex(0);
                    AlignVerticallyInRow(rowHeight, ImGui::GetFrameHeight());
                    bool enabledChanged = ImGui::Checkbox("##Override", &enabled);
                    // trackGesture()读的是ImGui::GetItemID()/IsItemActivated(), 必须紧跟在它要追踪的
                    // 那个控件后面调用——不能等SameLine()/TextUnformatted()画完了再调, 那样读到的就是
                    // 后面这些非交互控件的item状态了(MaterialTemplateEditor里所有trackGesture()调用
                    // 都遵守这同一条顺序规矩)。
                    auto& mi = *mMaterialInstance;
                    HString name = parameter.name;
                    auto enabledSetter = [&mi, name](bool v) { mi.setParameterOverrideEnabled(name, v); };
                    undoStack().trackGesture("Toggle Parameter Override"_hs, enabledBefore, enabled, enabledSetter);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!enabled);
                    ImGui::TextUnformatted(parameter.name.toCString());
                    ImGui::EndDisabled();
                    if (enabledChanged)
                    {
                        mi.setParameterOverrideEnabled(name, enabled);
                        getAsset().setDirty(true);
                    }

                    ImGui::TableSetColumnIndex(1);
                    // BeginPropertyRow()内部第一次切到column 1时已经调过一次AlignVerticallyInRow(),
                    // 但那次的光标偏移在我们跳去column 0画checkbox+名字的时候就被扔掉了——
                    // TableSetColumnIndex()切列时光标Y会重置回这一行的顶部, 不会记着之前算好的偏移,
                    // 所以这里要照BeginPropertyRow()同样的公式重新算一遍contentHeight、重新居中一次,
                    // 不然纹理参数这种2倍行高的控件就会贴在行顶部(单行的bool/float/...控件因为
                    // rowHeight和contentHeight本来就接近, 偏移量很小, 肉眼基本看不出来)。
                    float contentHeight = isTextureParam
                        ? ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y
                        : ImGui::GetFrameHeight();
                    AlignVerticallyInRow(rowHeight, contentHeight);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::BeginDisabled(!enabled);
                    // 编辑的是重载值本身: 已经有重载就用存的值, 否则拿模板当前默认值当起点(第一次编辑
                    // 时setParameterOverriddenValue()会顺带把enabled置true, 但这个控件在!enabled时
                    // 本来就点不到, 不会误触发)。
                    MaterialParameterValue overrideValue = existing ? existing->value : parameter.value;
                    if (drawParameterValueWidget(parameter, overrideValue, assetManager))
                        getAsset().setDirty(true);
                    ImGui::EndDisabled();

                    // 第2列: reset——只有已经存在真实重载值时才有意义比较/重置(DrawPropertyReset本身
                    // 也只在值跟默认值不同的时候才会画出按钮), 而且跟名字/值两列一样, !enabled 时也要
                    // 置灰(BeginDisabled 本身就会挡掉点击, 不需要额外判断)。
                    if (existing)
                    {
                        ImGui::BeginDisabled(!enabled);
                        MaterialParameterValue overrideValueCopy = existing->value;
                        if (DrawPropertyReset(parameter.name.toCString(), overrideValueCopy, parameter.value))
                        {
                            mMaterialInstance->setParameterOverriddenValue(parameter.name, overrideValueCopy);
                            getAsset().setDirty(true);
                        }
                        ImGui::EndDisabled();
                    }

                    ImGui::PopID();
                }
                EndPropertyTable();
            }
            ImGui::PopID();
        };

        for (size_t i = 0; i < groups.size(); ++i)
            drawBucket(groups[i].toCString(), buckets[i]);
        drawBucket("Common", buckets.back());
    }

    bool MaterialInstanceEditor::drawParameterValueWidget(
        const MaterialParameter& templateParameter,
        MaterialParameterValue& overrideValue,
        AssetManager& assetManager)
    {
        HString name = templateParameter.name;

        switch (overrideValue.getType())
        {
            case MaterialParameterType::Bool:
            {
                bool beforeValue = overrideValue.get<bool>();
                bool v = beforeValue;
                bool changed = ImGui::Checkbox("##Value", &v);
                trackParameterValue(name, beforeValue, v);
                if (changed)
                    mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v));
                return changed;
            }
            case MaterialParameterType::Int:
            {
                int beforeValue = overrideValue.get<int>();
                int v = beforeValue;
                bool changed = ImGui::InputInt("##Value", &v);
                trackParameterValue(name, beforeValue, v);
                if (changed)
                    mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v));
                return changed;
            }
            case MaterialParameterType::Float:
            {
                float beforeValue = overrideValue.get<float>();
                float v = beforeValue;
                bool changed = ImGui::DragFloat("##Value", &v);
                trackParameterValue(name, beforeValue, v);
                if (changed)
                    mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v));
                return changed;
            }
            case MaterialParameterType::Float2:
            {
                float2 beforeValue = overrideValue.get<float2>();
                float2 v = beforeValue;
                bool changed = ImGui::DragFloat2("##Value", &v.x);
                trackParameterValue(name, beforeValue, v);
                if (changed)
                    mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v));
                return changed;
            }
            case MaterialParameterType::Float3:
            {
                float3 beforeValue = overrideValue.get<float3>();
                float3 v = beforeValue;
                bool changed = LinearColorEdit3("##Value", &v.x);
                trackParameterValue(name, beforeValue, v);
                if (changed)
                    mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v));
                return changed;
            }
            case MaterialParameterType::Float4:
            {
                float4 beforeValue = overrideValue.get<float4>();
                float4 v = beforeValue;
                bool changed = LinearColorEdit4("##Value", &v.x);
                trackParameterValue(name, beforeValue, v);
                if (changed)
                    mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v));
                return changed;
            }
            case MaterialParameterType::Texture_2D:
            {
                SampledTexture2D v = overrideValue.get<SampledTexture2D>();
                TextureParamSnapshot beforeSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                bool changed = drawTextureValue<Texture2D>(v.texture, v.sampleStateOverride, assetManager);
                TextureParamSnapshot currentSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                trackTextureParameterValue<Texture2D>(name, beforeSnapshot, currentSnapshot);
                if (changed)
                {
                    if (v.sampleStateOverride)
                        mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v.texture.get(), *v.sampleStateOverride));
                    else
                        mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v.texture.get()));
                }
                return changed;
            }
            case MaterialParameterType::Texture_2D_Array:
            {
                SampledTexture2DArray v = overrideValue.get<SampledTexture2DArray>();
                TextureParamSnapshot beforeSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                bool changed = drawTextureValue<Texture2DArray>(v.texture, v.sampleStateOverride, assetManager);
                TextureParamSnapshot currentSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                trackTextureParameterValue<Texture2DArray>(name, beforeSnapshot, currentSnapshot);
                if (changed)
                {
                    if (v.sampleStateOverride)
                        mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v.texture.get(), *v.sampleStateOverride));
                    else
                        mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v.texture.get()));
                }
                return changed;
            }
            case MaterialParameterType::Texture_3D:
            {
                SampledTexture3D v = overrideValue.get<SampledTexture3D>();
                TextureParamSnapshot beforeSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                bool changed = drawTextureValue<Texture3D>(v.texture, v.sampleStateOverride, assetManager);
                TextureParamSnapshot currentSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                trackTextureParameterValue<Texture3D>(name, beforeSnapshot, currentSnapshot);
                if (changed)
                {
                    if (v.sampleStateOverride)
                        mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v.texture.get(), *v.sampleStateOverride));
                    else
                        mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v.texture.get()));
                }
                return changed;
            }
            case MaterialParameterType::Texture_Cube:
            {
                SampledTextureCube v = overrideValue.get<SampledTextureCube>();
                TextureParamSnapshot beforeSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                bool changed = drawTextureValue<TextureCube>(v.texture, v.sampleStateOverride, assetManager);
                TextureParamSnapshot currentSnapshot = makeTextureSnapshot(v.texture, v.sampleStateOverride);
                trackTextureParameterValue<TextureCube>(name, beforeSnapshot, currentSnapshot);
                if (changed)
                {
                    if (v.sampleStateOverride)
                        mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v.texture.get(), *v.sampleStateOverride));
                    else
                        mMaterialInstance->setParameterOverriddenValue(name, MaterialParameterValue(v.texture.get()));
                }
                return changed;
            }
            case MaterialParameterType::Font:
            {
                SampledFont v = overrideValue.get<SampledFont>();
                Guid beforeGuid = v.font ? v.font->getGuid() : Guid();
                Guid guid = beforeGuid;
                bool changed = AssetComboEd("##Font", assetManager, Font::staticClass(), false, guid, mAssetBrowser, mIconProvider);
                auto fontSetter = [&mi = *mMaterialInstance, name](const Guid& g)
                {
                    AssetManager& assetManager = Context::get().getAssetManager();
                    Asset* asset = g.isValid() ? assetManager.getAsset(g) : nullptr;
                    TRef<Font> font = asset ? asset->getOrLoadRootObject<Font>() : TRef<Font>();
                    mi.setParameterOverriddenValue(name, MaterialParameterValue(font.get()));
                };
                undoStack().trackGesture("Change Parameter Override"_hs, beforeGuid, guid, fontSetter);
                if (changed)
                    fontSetter(guid);
                return changed;
            }
            default:
                return false;
        }
    }

    template <typename T>
    void MaterialInstanceEditor::trackParameterValue(HString name, const T& before, const T& current)
    {
        undoStack().trackGesture("Change Parameter Override"_hs, before, current,
            [&mi = *mMaterialInstance, name](const T& v) { mi.setParameterOverriddenValue(name, MaterialParameterValue(v)); });
    }

    template <typename TextureT>
    MaterialInstanceEditor::TextureParamSnapshot MaterialInstanceEditor::makeTextureSnapshot(const TRef<TextureT>& texture, const std::optional<SampleState>& sampleStateOverride) const
    {
        return {
            texture ? texture->getGuid() : Guid(),
            sampleStateOverride.has_value(),
            sampleStateOverride.value_or(SampleState{})
        };
    }

    template <typename TextureT>
    void MaterialInstanceEditor::trackTextureParameterValue(HString name, const TextureParamSnapshot& before, const TextureParamSnapshot& current)
    {
        undoStack().trackGesture("Change Parameter Override"_hs, before, current,
            [&mi = *mMaterialInstance, name](const TextureParamSnapshot& s)
            {
                AssetManager& assetManager = Context::get().getAssetManager();
                Asset* asset = s.textureGuid.isValid() ? assetManager.getAsset(s.textureGuid) : nullptr;
                TRef<TextureT> texture = asset ? asset->getOrLoadRootObject<TextureT>() : TRef<TextureT>();
                if (s.hasOverride)
                    mi.setParameterOverriddenValue(name, MaterialParameterValue(texture.get(), s.overrideState));
                else
                    mi.setParameterOverriddenValue(name, MaterialParameterValue(texture.get()));
            });
    }

    template <typename TextureT>
    bool MaterialInstanceEditor::drawTextureValue(TRef<TextureT>& texture, std::optional<SampleState>& sampleStateOverride, AssetManager& assetManager)
    {
        Guid guid = texture ? texture->getGuid() : Guid();
        bool changed = AssetComboEd("##Texture", assetManager, TextureT::staticClass(), false, guid, mAssetBrowser, mIconProvider);
        if (changed)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            texture = asset ? asset->getOrLoadRootObject<TextureT>() : TRef<TextureT>();
        }

        ImGui::SameLine();
        if (TextOnlyButton(ICON_MS_MORE_HORIZ))
            ImGui::OpenPopup("SamplerOverride");

        if (ImGui::BeginPopup("SamplerOverride"))
        {
            bool hasOverride = sampleStateOverride.has_value();
            if (ImGui::Checkbox("Override Sampler", &hasOverride))
            {
                changed = true;
                sampleStateOverride = hasOverride ? std::optional<SampleState>(SampleState{}) : std::nullopt;
            }
            if (hasOverride)
            {
                SampleState& state = *sampleStateOverride;
                changed |= EnumCombo("##Filter", &state.filter);
                changed |= EnumCombo("##AddressU", &state.addressModeU);
                changed |= EnumCombo("##AddressV", &state.addressModeV);
                changed |= EnumCombo("##AddressW", &state.addressModeW);
            }
            ImGui::EndPopup();
        }
        return changed;
    }

    void MaterialInstanceEditor::drawSave()
    {
        ImGui::BeginDisabled(!getAsset().isDirty());
        if (ImGui::Button("Save"))
        {
            getAsset().save();
        }
        ImGui::EndDisabled();
    }
}
