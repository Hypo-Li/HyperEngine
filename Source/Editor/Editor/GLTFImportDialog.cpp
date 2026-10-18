#include "GLTFImportDialog.h"

#include <AssetInterchange/GLTFLoader.h>
#include <Engine/Animation/AnimationClip.h>
#include <Engine/Mesh/SkeletalMesh.h>
#include <Engine/Mesh/StaticMesh.h>
#include <Engine/Skeleton/Skeleton.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <shobjidl.h>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#endif

namespace hyp::ed
{
    std::optional<std::filesystem::path> GLTFImportDialog::openFileDialog()
    {
#ifdef _WIN32
        std::optional<std::filesystem::path> result;

        HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        {
            constexpr COMDLG_FILTERSPEC filters[] = {
                { L"glTF Files", L"*.gltf;*.glb" },
                { L"All Files", L"*.*" },
            };
            dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
            dialog->SetTitle(L"Import glTF Model");

            if (SUCCEEDED(dialog->Show(nullptr)))
            {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item)))
                {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                    {
                        result = std::filesystem::path(path);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            dialog->Release();
        }

        if (comResult == S_OK || comResult == S_FALSE)
            CoUninitialize();

        return result;
#else
        return std::nullopt;
#endif
    }

    void GLTFImportDialog::beginImport()
    {
        std::optional<std::filesystem::path> sourcePath = openFileDialog();
        if (!sourcePath)
            return;

        mSourcePath = *sourcePath;
        mImportMaterials = true;
        mImportSkinning = true;
        mImportAnimations = true;
    }

    std::optional<ImportDialog::Result> GLTFImportDialog::draw()
    {
        ImGui::OpenPopup("GLTFImportDialog");

        if (!ImGui::BeginPopupModal("GLTFImportDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return std::nullopt;

        if (BeginPropertyTable("GLTFImportDialog.Properties", ImGuiTableFlags_BordersInnerH))
        {
            BeginPropertyRow("File");
            std::string fileName = mSourcePath.filename().string();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(fileName.c_str());

            BeginPropertyRow("Import Materials");
            ImGui::Checkbox("##ImportMaterials", &mImportMaterials);

            BeginPropertyRow("Import Skinning");
            ImGui::Checkbox("##ImportSkinning", &mImportSkinning);

            BeginPropertyRow("Import Animations");
            ImGui::BeginDisabled(!mImportSkinning);
            ImGui::Checkbox("##ImportAnimations", &mImportAnimations);
            ImGui::EndDisabled();

            EndPropertyTable();
        }

        ImGui::Spacing();

        std::optional<Result> result;

        if (ImGui::Button("Import"))
        {
            GLTFLoader loader;
            bool importAnimations = mImportSkinning && mImportAnimations;
            if (std::optional<GLTFLoadResult> loadResult = loader.load(mSourcePath, mImportMaterials, mImportSkinning, importAnimations))
            {
                Result importResult;
                importResult.folderName = mSourcePath.stem().string();

                for (size_t i = 0; i < loadResult->meshes.size(); ++i)
                {
                    // Mesh Card 数据量小, 不走 DDC, 导入时直接烘一次落盘就够(跟 MDF 不同——
                    // 后者只在真正需要时才烘, 见 StaticMesh::ensureDistanceFieldData())
                    loadResult->meshes[i]->bakeCardData();
                    importResult.entries.push_back({ "StaticMeshes/" + loadResult->meshNames[i], loadResult->meshes[i] });
                }
                for (size_t i = 0; i < loadResult->skeletalMeshes.size(); ++i)
                    importResult.entries.push_back({ "SkeletalMeshes/" + loadResult->skeletalMeshNames[i], loadResult->skeletalMeshes[i] });
                for (size_t i = 0; i < loadResult->skeletons.size(); ++i)
                    importResult.entries.push_back({ "Skeletons/" + loadResult->skeletonNames[i], loadResult->skeletons[i] });
                for (size_t i = 0; i < loadResult->animations.size(); ++i)
                    importResult.entries.push_back({ "Animations/" + loadResult->animationNames[i], loadResult->animations[i] });
                for (size_t i = 0; i < loadResult->materials.size(); ++i)
                    importResult.entries.push_back({ "Materials/" + loadResult->materialNames[i], loadResult->materials[i] });
                for (size_t i = 0; i < loadResult->textures.size(); ++i)
                    importResult.entries.push_back({ "Textures/" + loadResult->textureNames[i], loadResult->textures[i] });

                if (!importResult.entries.empty())
                    result = std::move(importResult);
            }

            mSourcePath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            mSourcePath.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
        return result;
    }
}
