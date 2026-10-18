#include "Texture2DImportDialog.h"

#include <HypImGui/ImGuiEnumWidgets.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#include <Core/Common/Float16.h>
#include <TextureUtility/Mipmap.h>
#include <TextureUtility/TextureUtility.h>

#include <stb_image.h>

// 不能 #define TINYEXR_IMPLEMENTATION——这个 vcpkg port 把实现体预编译进了 tinyexr.lib(见
// CMakeLists.txt 里的注释), 这里只用它的声明, 实现由链接期解析。
#include <tinyexr.h>

#include <algorithm>
#include <cctype>
#include <cstring>

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
        PixelFormat channelsToPixelFormat(int channels)
        {
            switch (channels)
            {
            case 1: return PixelFormat::R8_UNORM;
            case 2: return PixelFormat::RG8_UNORM;
            case 3: return PixelFormat::RGB8_UNORM;
            default: return PixelFormat::RGBA8_UNORM;
            }
        }

        const char* pixelFormatName(PixelFormat format)
        {
            switch (format)
            {
            case PixelFormat::R8_UNORM: return "R8_UNORM";
            case PixelFormat::RG8_UNORM: return "RG8_UNORM";
            case PixelFormat::RGB8_UNORM: return "RGB8_UNORM";
            case PixelFormat::RGBA16_SFLOAT: return "RGBA16_SFLOAT";
            default: return "RGBA8_UNORM";
            }
        }
    }

    std::optional<std::filesystem::path> Texture2DImportDialog::openFileDialog()
    {
#ifdef _WIN32
        // IFileOpenDialog 不像 GetOpenFileName 那样默认会改变进程当前目录, 不需要设 OFN_NOCHANGEDIR。
        std::optional<std::filesystem::path> result;

        HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        IFileOpenDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        {
            constexpr COMDLG_FILTERSPEC filters[] = {
                { L"Image Files", L"*.jpg;*.jpeg;*.png;*.tga;*.exr" },
                { L"All Files", L"*.*" },
            };
            dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
            dialog->SetTitle(L"Import Texture");

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

    void Texture2DImportDialog::beginImport()
    {
        std::optional<std::filesystem::path> sourcePath = openFileDialog();
        if (!sourcePath)
            return;

        std::string ext = sourcePath->extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

        if (ext == ".exr")
        {
            float* pixels = nullptr;
            int width = 0, height = 0;
            const char* err = nullptr;
            int ret = LoadEXR(&pixels, &width, &height, sourcePath->string().c_str(), &err);
            if (ret != TINYEXR_SUCCESS)
            {
                if (err)
                    FreeEXRErrorMessage(err);
                return;
            }

            mSourcePath = *sourcePath;
            mFormat = PixelFormat::RGBA16_SFLOAT;
            const size_t pixelCount = static_cast<size_t>(width) * height;
            mPixels.resize(pixelCount * 4 * sizeof(float16));
            float16* dst = reinterpret_cast<float16*>(mPixels.data());
            for (size_t i = 0; i < pixelCount * 4; ++i)
                dst[i] = float32ToFloat16(pixels[i]);
            mWidth = width;
            mHeight = height;
            mDiscardAlpha = false;
            // EXR 是线性 HDR 数据, 没有 gamma, 默认不该勾 sRGB(下面 8-bit 分支默认勾 true,
            // 这里要显式覆盖, 不能沿用类成员的默认值)
            mIsSRGB = false;
            // fp16 格式下这个预设才真的会走到 BC6H_UFLOAT(见 Texture.cpp 的 is16BitSfloat),
            // 默认选它
            mPreset = CompressionPreset::HDR;
            free(pixels);
            return;
        }

        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load(sourcePath->string().c_str(), &width, &height, &channels, STBI_default);
        if (!pixels)
            return;

        mSourcePath = *sourcePath;
        mFormat = channelsToPixelFormat(channels);
        mPixels.assign(pixels, pixels + static_cast<size_t>(width) * height * channels);
        mWidth = width;
        mHeight = height;
        mDiscardAlpha = false;
        stbi_image_free(pixels);
    }

    std::optional<ImportDialog::Result> Texture2DImportDialog::draw()
    {
        ImGui::OpenPopup("Texture2DImportDialog");

        if (!ImGui::BeginPopupModal("Texture2DImportDialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return std::nullopt;

        if (BeginPropertyTable("Texture2DImportDialog.Properties", ImGuiTableFlags_BordersInnerH))
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

            BeginPropertyRow("Depth");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("1");

            BeginPropertyRow("Format");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(pixelFormatName(mFormat));

            if (pixelFormatHasAlpha(mFormat))
            {
                BeginPropertyRow("Discard Alpha");
                ImGui::Checkbox("##DiscardAlpha", &mDiscardAlpha);
            }

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
            PixelFormat importFormat = mFormat;
            std::vector<uint8_t> importPixels = std::move(mPixels);
            const size_t pixelCount = static_cast<size_t>(mWidth) * mHeight;

            if (mDiscardAlpha && pixelFormatHasAlpha(importFormat))
            {
                const uint32_t bytesPerChannel = getPixelSizeByFormat(importFormat) / getPixelFormatChannels(importFormat);
                std::vector<uint8_t> rgbPixels(pixelCount * 3 * bytesPerChannel);
                for (size_t i = 0; i < pixelCount; ++i)
                {
                    std::memcpy(&rgbPixels[i * 3 * bytesPerChannel], &importPixels[i * 4 * bytesPerChannel], 3 * bytesPerChannel);
                }
                importPixels = std::move(rgbPixels);
                importFormat = importFormat == PixelFormat::RGBA16_SFLOAT ? PixelFormat::RGB16_SFLOAT : PixelFormat::RGB8_UNORM;
            }

            // EXR 导入的 fp16 数据配非 HDR 压缩预设时, convertToRHITextureFormat()(Texture.cpp)
            // 的 BC1/BC3/BC7/BC4/BC5 分支只认 8-bit unorm 源, 匹配不上会直接退化成不压缩的
            // 16F——这里先把数据本身转成 8-bit unorm, 选中的压缩预设才能真正生效。
            if ((importFormat == PixelFormat::RGB16_SFLOAT || importFormat == PixelFormat::RGBA16_SFLOAT) &&
                mPreset != CompressionPreset::HDR)
            {
                const uint32_t channels = getPixelFormatChannels(importFormat);
                const float16* src = reinterpret_cast<const float16*>(importPixels.data());
                std::vector<uint8_t> unormPixels(pixelCount * channels);
                for (size_t i = 0; i < pixelCount; ++i)
                {
                    for (uint32_t c = 0; c < channels; ++c)
                    {
                        float linear = float16ToFloat32(src[i * channels + c]);
                        // alpha 通道始终线性存储, 不做 sRGB gamma 编码
                        unormPixels[i * channels + c] = (mIsSRGB && c < 3)
                            ? ColorSpace::linearToSrgb(linear)
                            : static_cast<uint8_t>(std::round(std::clamp(linear, 0.0f, 1.0f) * 255.0f));
                    }
                }
                importPixels = std::move(unormPixels);
                importFormat = channels == 4 ? PixelFormat::RGBA8_UNORM : PixelFormat::RGB8_UNORM;
            }

            TRef<Texture2D> texture = createObject<Texture2D>(nullptr);
            texture->initialize(static_cast<uint32_t>(mWidth), static_cast<uint32_t>(mHeight), importFormat);
            texture->setSRGB(mIsSRGB);
            texture->setCompressionPreset(mPreset);
            texture->setSampleState(mSampleState);
            texture->setSourcePixels(std::move(importPixels));
            texture->bakeMips();
            result = Result{ "", { Result::Entry{ mSourcePath.stem().string(), texture } } };

            mPixels.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            mPixels.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
        return result;
    }
}
