#include "FontImportDialog.h"

#include <Core/Common/ModuleServiceRegistry.h>

#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

#include <fstream>
#include <iterator>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <shobjidl.h>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#endif

namespace hyp::ed
{
    std::optional<std::filesystem::path> FontImportDialog::openFileDialog()
    {
#ifdef _WIN32
        std::optional<std::filesystem::path> result;

        HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        {
            constexpr COMDLG_FILTERSPEC filters[] = {
                { L"Font Files", L"*.ttf;*.otf;*.ttc;*.otc" },
                { L"All Files", L"*.*" },
            };
            dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
            dialog->SetTitle(L"Import Font");

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

    void FontImportDialog::beginImport()
    {
        std::optional<std::filesystem::path> sourcePath = openFileDialog();
        if (!sourcePath)
            return;

        std::ifstream file(*sourcePath, std::ios::binary);
        std::vector<uint8_t> faceData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (faceData.empty())
            return;

        IFontUtility* fontUtility = ModuleServiceRegistry::get().getService<IFontUtility>();
        if (!fontUtility)
            return;

        std::optional<FontFaceInfo> info = fontUtility->inspectFace(faceData, 0);
        if (!info)
            return;

        mSourcePath = *sourcePath;
        mFaceData = std::move(faceData);
        mFaceInfo = std::move(*info);
        mSettings = FontBakeSettings{};
    }

    void FontImportDialog::refreshFaceInfo()
    {
        IFontUtility* fontUtility = ModuleServiceRegistry::get().getService<IFontUtility>();
        if (!fontUtility)
            return;

        if (std::optional<FontFaceInfo> info = fontUtility->inspectFace(mFaceData, mSettings.faceIndex))
            mFaceInfo = std::move(*info);
        else
            mSettings.faceIndex = 0;
    }

    void FontImportDialog::close()
    {
        mJob.reset();
        mFont.reset();
        mFaceData.clear();
        mBakeFailed = false;
        ImGui::CloseCurrentPopup();
    }

    std::optional<ImportDialog::Result> FontImportDialog::draw()
    {
        ImGui::OpenPopup("FontImportDialog");

        if (!ImGui::BeginPopupModal("FontImportDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return std::nullopt;

        std::optional<Result> result;

        if (mJob)
        {
            ImGui::TextUnformatted("Baking font...");
            ImGui::ProgressBar(mJob->getProgress(), ImVec2(ImGui::GetFontSize() * 24.0f, 0.0f));

            if (ImGui::Button("Cancel"))
                mJob->cancel();

            if (mJob->isFinished())
            {
                std::optional<FontBakeResult> bakeResult = mJob->takeResult();
                if (bakeResult)
                {
                    mFont->applyBakeResult(std::move(*bakeResult), mJob->getSettings());
                    result = Result{ "", { Result::Entry{ mSourcePath.stem().string(), mFont } } };
                    close();
                }
                else
                {
                    mBakeFailed = true;
                    mJob.reset();
                    mFont.reset();
                }
            }

            ImGui::EndPopup();
            return result;
        }

        if (BeginPropertyTable("FontImportDialog.Info", ImGuiTableFlags_BordersInnerH))
        {
            BeginPropertyRow("File");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(mSourcePath.filename().string().c_str());

            BeginPropertyRow("Family");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(mFaceInfo.familyName.c_str());

            BeginPropertyRow("Style");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(mFaceInfo.styleName.c_str());

            BeginPropertyRow("Faces");
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%u", mFaceInfo.faceCount);

            BeginPropertyRow("Glyphs");
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%u", mFaceInfo.glyphCount);

            EndPropertyTable();
        }

        ImGui::Spacing();

        const uint32_t previousFaceIndex = mSettings.faceIndex;
        DrawFontBakeSettings(mSettings);
        if (mSettings.faceIndex != previousFaceIndex)
            refreshFaceInfo();

        if (mBakeFailed)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextUnformatted("Bake failed or was cancelled, see the log.");
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        if (ImGui::Button("Import"))
        {
            mFont = createObject<Font>(nullptr);
            mFont->setSourceFace(mFaceData);
            mFont->setBakeSettings(mSettings);
            mJob = std::make_unique<FontBakeJob>(mFaceData, mSettings);
            mBakeFailed = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            close();

        ImGui::EndPopup();
        return result;
    }
}
