#include "MaterialFunctionLibraryEditor.h"
#include "MaterialFunctionLibraryEditorTab.h"
#include "AssetComboEd.h"
#include "AssetEditorRegistry.h"
#include "ThumbnailProvider.h"
#include "MaterialTemplateEditor.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Engine/Context.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        AssetEditorRegistrar<MaterialFunctionLibrary, MaterialFunctionLibraryEditorTab> gRegistrar;

        MaterialFunctionLibrary* resolveLibraryAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<MaterialFunctionLibrary>().get() : nullptr;
        }
    }

    MaterialFunctionLibraryEditor::MaterialFunctionLibraryEditor(Asset& asset, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        AssetEditorWindow(asset, undoStack),
        mIconProvider(iconProvider),
        mAssetBrowser(assetBrowser),
        mLibrary(asset.getOrLoadRootObject<MaterialFunctionLibrary>()),
        mSourceEditor(buildSourcePreamble(), mLibrary->getLibrarySource())
    {
    }

    void MaterialFunctionLibraryEditor::onDraw()
    {
        if (!mLibrary)
        {
            ImGui::TextDisabled("Material function library failed to load.");
            return;
        }

        drawImportedLibraries();
        ImGui::Spacing();
        drawSource();
        ImGui::Spacing();
        drawApplyAndSave();
    }

    void MaterialFunctionLibraryEditor::drawImportedLibraries()
    {
        ImGui::SeparatorText("Imported Libraries");

        AssetManager& assetManager = Context::get().getAssetManager();

        // 排除掉选了就会失败的候选项(自己/已经引入过/会成环), 不用等用户点 Add 才发现选不了
        auto canImport = [this](Asset* asset) -> bool
        {
            MaterialFunctionLibrary* candidate = asset->getOrLoadRootObject<MaterialFunctionLibrary>().get();
            return mLibrary->canImportLibrary(candidate);
        };

        std::span<const TRef<MaterialFunctionLibrary>> imported = mLibrary->getImportedLibraries();
        MaterialFunctionLibrary* toRemove = nullptr;
        for (uint32_t i = 0; i < imported.size(); ++i)
        {
            MaterialFunctionLibrary* library = imported[i].get();
            Guid guid = library->getGuid();
            ImGui::PushID(static_cast<int>(i));
            bool changed = AssetComboEd("##ImportedLibrary", assetManager, MaterialFunctionLibrary::staticClass(), false, guid, mAssetBrowser, mIconProvider, canImport);
            ImGui::SameLine();
            bool removeClicked = ImGui::Button("Remove");
            ImGui::PopID();

            if (removeClicked)
            {
                toRemove = library;
            }
            else if (changed)
            {
                // 换成引用另一个库——先试着加新的, 成功了才把旧的摘掉; 加不进去(比如会成环)
                // 原样保留旧引用, 不留下"看着换了、其实没生效"的中间态
                MaterialFunctionLibrary* replacement = resolveLibraryAsset(assetManager, guid);
                if (replacement && mLibrary->addImportedLibrary(replacement))
                {
                    mLibrary->removeImportedLibrary(library);
                    mImportError.clear();
                    mSourceEditor.setPreamble(buildSourcePreamble());
                }
                else
                {
                    mImportError = "Can't switch to that library (empty, already imported, or would create a cycle).";
                }
            }
        }
        if (toRemove)
        {
            mLibrary->removeImportedLibrary(toRemove);
            mSourceEditor.setPreamble(buildSourcePreamble());
        }

        ImGui::PushID("NewImportedLibrary");
        AssetComboEd("##NewImportedLibrary", assetManager, MaterialFunctionLibrary::staticClass(), false, mNewImportedLibraryGuid, mAssetBrowser, mIconProvider, canImport);
        ImGui::SameLine();
        if (ImGui::Button("Add"))
        {
            MaterialFunctionLibrary* toAdd = resolveLibraryAsset(assetManager, mNewImportedLibraryGuid);
            if (toAdd && mLibrary->addImportedLibrary(toAdd))
            {
                mNewImportedLibraryGuid = Guid();
                mImportError.clear();
                mSourceEditor.setPreamble(buildSourcePreamble());
            }
            else
            {
                mImportError = "Can't add that library (empty, already imported, or would create a cycle).";
            }
        }
        ImGui::PopID();

        if (!mImportError.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", mImportError.c_str());
    }

    void MaterialFunctionLibraryEditor::drawSource()
    {
        ImGui::SeparatorText("Library Source");
        if (ImGui::BeginChild("LibrarySourceEditorChild", ImVec2(-1.0f, 320.0f), ImGuiChildFlags_ResizeY))
        {
            mSourceModified |= mSourceEditor.render("##LibrarySourceEditor", ImVec2(-1.0f, -1.0f));
        }
        ImGui::EndChild();
    }

    bool MaterialFunctionLibraryEditor::needsApply() const noexcept
    {
        return mSourceModified;
    }

    bool MaterialFunctionLibraryEditor::needsSave() const noexcept
    {
        return needsApply() || getAsset().isDirty();
    }

    void MaterialFunctionLibraryEditor::applyPendingChanges()
    {
        mLibrary->setLibrarySource(mSourceEditor.getText());
        mSourceModified = false;
        getAsset().setDirty(true);

        // 通知所有(直接或经嵌套)引用了这个库、当前开着的 MaterialTemplateEditor 刷新 preamble
        MaterialTemplateEditor::forEachInstance(
            [library = mLibrary.get()](MaterialTemplateEditor& editor)
            {
                if (editor.referencesFunctionLibrary(library))
                    editor.refreshFunctionLibraryPreamble();
            }
        );
    }

    void MaterialFunctionLibraryEditor::drawApplyAndSave()
    {
        ImGui::BeginDisabled(!needsApply());
        if (ImGui::Button("Apply"))
        {
            applyPendingChanges();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!needsSave());
        if (ImGui::Button("Save"))
        {
            if (needsApply())
            {
                applyPendingChanges();
            }
            getAsset().save();
        }
        ImGui::EndDisabled();
    }

    std::string MaterialFunctionLibraryEditor::buildSourcePreamble() const
    {
        return resolveMaterialFunctionLibrarySources(mLibrary->getImportedLibraries());
    }
}
