#pragma once
#include "AssetEditorWindow.h"

#include <Core/Common/Guid.h>
#include <Engine/Material/MaterialInstance.h>
#include <Engine/Texture/SampleState.h>

#include <imgui.h>

#include <functional>

namespace hyp
{
    class AssetManager;
}

namespace hyp::ed
{
    class ThumbnailProvider;
    class AssetBrowser;
    class UndoStack;

    class MaterialInstanceEditor final : public AssetEditorWindow
    {
    public:
        MaterialInstanceEditor(Asset& asset, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);

    protected:
        void onDraw() override;

    private:
        void drawTemplateRow();
        void drawParameterList();
        void drawSave();

        // Draws the value widget for one template parameter's override, editing overrideValue in
        // place — the caller (drawParameterList) decides where overrideValue came from (an
        // existing override, or a copy of the template's default seeded on first edit) and what
        // to do with it afterward (write back via setParameterOverriddenValue()).
        // 返回这一帧有没有真的改动值(用来驱动 getAsset().setDirty(true))——调用方不用在每个
        // switch 分支里各自记一遍。
        bool drawParameterValueWidget(
            const MaterialParameter& templateParameter,
            MaterialParameterValue& overrideValue,
            AssetManager& assetManager
        );

        // 跟 MaterialTemplateEditor 里同名机制的道理一样: 用参数名(不是下标)当 undo 的身份,
        // 具体设值统一走 MaterialInstance::setParameterOverriddenValue(HString, ...), 不受
        // mParameterOverrides 内部存储顺序影响。
        template <typename T>
        void trackParameterValue(HString name, const T& before, const T& current);

        // 纹理参数按 Guid 生成快照, 跟 MaterialTemplateEditor::TextureParamSnapshot 同理——
        // 不捕获 TRef/Asset*, undo/redo 真正执行时才现查 AssetManager。
        struct TextureParamSnapshot
        {
            Guid textureGuid;
            bool hasOverride = false;
            SampleState overrideState;

            friend bool operator==(const TextureParamSnapshot&, const TextureParamSnapshot&) = default;
        };

        template <typename TextureT>
        TextureParamSnapshot makeTextureSnapshot(const TRef<TextureT>& texture, const std::optional<SampleState>& sampleStateOverride) const;

        template <typename TextureT>
        void trackTextureParameterValue(HString name, const TextureParamSnapshot& before, const TextureParamSnapshot& current);

        template <typename TextureT>
        bool drawTextureValue(TRef<TextureT>& texture, std::optional<SampleState>& sampleStateOverride, AssetManager& assetManager);

    private:
        ThumbnailProvider& mIconProvider;
        AssetBrowser& mAssetBrowser;
        TRef<MaterialInstance> mMaterialInstance;
    };
}
