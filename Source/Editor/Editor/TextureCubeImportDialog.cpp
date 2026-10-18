#include "TextureCubeImportDialog.h"

#include <HypImGui/ImGuiEnumWidgets.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#include <Core/Common/Float16.h>
#include <Core/Common/LogSystem.h>

#include <stb_image.h>

// 不能 #define TINYEXR_IMPLEMENTATION——这个 vcpkg port 把实现体预编译进了 tinyexr.lib(见
// CMakeLists.txt 里的注释), 这里只用它的声明, 实现由链接期解析。
#include <tinyexr.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <shobjidl.h>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#endif

namespace hyp::ed
{
    namespace
    {
        // cube face 尺寸要求是 2 的幂(TextureCube::initialize() 的 mip 链计算用 log2), 固定
        // 挡位而不是自由拖拽——跟 SkyLightPanel 的 Prefilter Resolution 同一个理由。
        constexpr uint32_t kFaceSizeOptions[] = { 128, 256, 512, 1024, 2048 };

        // 等距图纬度方向(mHeight)对应 180°, 一个 cube face 覆盖 90°, 取 mHeight/2 最接近的
        // 挡位作为默认值。
        int suggestFaceSizeIndex(int equirectHeight)
        {
            uint32_t target = static_cast<uint32_t>(std::max(equirectHeight / 2, 1));
            int best = 0;
            uint32_t bestDiff = std::numeric_limits<uint32_t>::max();
            for (int i = 0; i < static_cast<int>(std::size(kFaceSizeOptions)); ++i)
            {
                uint32_t option = kFaceSizeOptions[i];
                uint32_t diff = option > target ? option - target : target - option;
                if (diff < bestDiff)
                {
                    bestDiff = diff;
                    best = i;
                }
            }
            return best;
        }
    }

    std::optional<std::filesystem::path> TextureCubeImportDialog::openFileDialog()
    {
#ifdef _WIN32
        std::optional<std::filesystem::path> result;

        HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        {
            constexpr COMDLG_FILTERSPEC filters[] = {
                { L"HDR Images", L"*.hdr;*.exr" },
                { L"All Files", L"*.*" },
            };
            dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
            dialog->SetTitle(L"Import Texture Cube");

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

    void TextureCubeImportDialog::beginImport()
    {
        std::optional<std::filesystem::path> sourcePath = openFileDialog();
        if (!sourcePath)
            return;

        std::string ext = sourcePath->extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

        int width = 0, height = 0;
        std::vector<float3> equirectPixels;

        if (ext == ".exr")
        {
            float* pixels = nullptr;
            const char* err = nullptr;
            int ret = LoadEXR(&pixels, &width, &height, sourcePath->string().c_str(), &err);
            if (ret != TINYEXR_SUCCESS)
            {
                if (err)
                    FreeEXRErrorMessage(err);
                return;
            }
            // LoadEXR() 固定输出 4 通道, 等距环境图不需要 alpha, 只取 RGB。
            equirectPixels.resize(static_cast<size_t>(width) * height);
            for (size_t i = 0; i < equirectPixels.size(); ++i)
                equirectPixels[i] = float3(pixels[i * 4 + 0], pixels[i * 4 + 1], pixels[i * 4 + 2]);
            free(pixels);
        }
        else
        {
            int channels = 0;
            float* pixels = stbi_loadf(sourcePath->string().c_str(), &width, &height, &channels, 3);
            if (!pixels)
                return;
            equirectPixels.assign(
                reinterpret_cast<const float3*>(pixels),
                reinterpret_cast<const float3*>(pixels) + static_cast<size_t>(width) * height);
            stbi_image_free(pixels);
        }

        // equirectMapToCubemap() 要求严格 2:1, 不符合就没法重投影——比"整个文件解码失败"更
        // 容易踩到(常见错误: 拿了一张 1:1 或者被裁过的等距图), 打个 log 而不是完全无声。
        if (height == 0 || width != height * 2)
        {
            LOG_WARN("TextureCube import: expected 2:1 equirectangular aspect ratio, got {}x{} ({})",
                width, height, sourcePath->filename().string());
            return;
        }

        mSourcePath = *sourcePath;
        mEquirectPixels = std::move(equirectPixels);
        mWidth = width;
        mHeight = height;
        mFaceSizeIndex = suggestFaceSizeIndex(mHeight);
    }

    std::optional<ImportDialog::Result> TextureCubeImportDialog::draw()
    {
        ImGui::OpenPopup("TextureCubeImportDialog");

        if (!ImGui::BeginPopupModal("TextureCubeImportDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return std::nullopt;

        if (BeginPropertyTable("TextureCubeImportDialog.Properties", ImGuiTableFlags_BordersInnerH))
        {
            BeginPropertyRow("File");
            std::string fileName = mSourcePath.filename().string();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(fileName.c_str());

            BeginPropertyRow("Width");
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", mWidth);

            BeginPropertyRow("Height");
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", mHeight);

            BeginPropertyRow("Face Size");
            ImGui::Combo("##FaceSize", &mFaceSizeIndex, "128\0""256\0""512\0""1024\0""2048\0");

            BeginPropertyRow("sRGB");
            ImGui::Checkbox("##sRGB", &mIsSRGB);

            BeginPropertyRow("Compression Preset");
            EnumCombo("##CompressionPreset", &mPreset);

            BeginPropertyRow("Filter");
            EnumCombo("##Filter", &mSampleState.filter);

            BeginPropertyRow("Address Mode U");
            EnumCombo("##AddressU", &mSampleState.addressModeU);

            BeginPropertyRow("Address Mode V");
            EnumCombo("##AddressV", &mSampleState.addressModeV);

            BeginPropertyRow("Address Mode W");
            EnumCombo("##AddressW", &mSampleState.addressModeW);

            EndPropertyTable();
        }

        ImGui::Spacing();

        std::optional<Result> result;

        if (ImGui::Button("Import"))
        {
            uint32_t faceSize = kFaceSizeOptions[mFaceSizeIndex];
            std::vector<float3> cubemapPixels = equirectMapToCubemap(
                mEquirectPixels, static_cast<uint32_t>(mWidth), static_cast<uint32_t>(mHeight), faceSize);

            std::vector<uint8_t> packedPixels(cubemapPixels.size() * 3 * sizeof(float16));
            float16* dst = reinterpret_cast<float16*>(packedPixels.data());
            for (size_t i = 0; i < cubemapPixels.size(); ++i)
            {
                dst[i * 3 + 0] = float32ToFloat16(cubemapPixels[i].x);
                dst[i * 3 + 1] = float32ToFloat16(cubemapPixels[i].y);
                dst[i * 3 + 2] = float32ToFloat16(cubemapPixels[i].z);
            }

            TRef<TextureCube> texture = createObject<TextureCube>(nullptr);
            texture->initialize(faceSize, PixelFormat::RGB16_SFLOAT);
            texture->setSRGB(mIsSRGB);
            texture->setCompressionPreset(mPreset);
            texture->setSampleState(mSampleState);
            texture->setSourcePixels(std::move(packedPixels));
            texture->bakeMips();
            result = Result{ "", { Result::Entry{ mSourcePath.stem().string(), texture } } };

            mEquirectPixels.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            mEquirectPixels.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
        return result;
    }
}
