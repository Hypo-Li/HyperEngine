#pragma once
#include "ImportDialog.h"

#include <Engine/Texture/Texture2D.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace hyp::ed
{
    // "Add > Texture2D"的导入流程: 弹原生文件框选图 -> 解码 -> 配置属性 -> Import/Cancel。
    class Texture2DImportDialog : public ImportDialog
    {
    public:
        // 弹原生文件选择框(jpg/jpeg/png/tga/exr), 选中后同步解码; 取消或解码失败什么都不做。
        void beginImport();

        bool hasPendingImport() const noexcept override
        {
            return !mPixels.empty();
        }

        std::optional<Result> draw() override;

    private:
        std::optional<std::filesystem::path> openFileDialog();

        std::filesystem::path mSourcePath;
        std::vector<uint8_t> mPixels;
        int mWidth = 0;
        int mHeight = 0;
        PixelFormat mFormat = PixelFormat::RGBA8_UNORM;
        // 只在 pixelFormatHasAlpha(mFormat) 时才在 UI 里展示这个选项(RGBA8_UNORM 来自
        // stb_image, RGBA16_SFLOAT 来自 EXR)——源图片本来就没有 Alpha 通道的话没有什么好丢的。
        // 勾选后 Import 会把源像素重新打包成不含 Alpha 的格式再导入, 这样 CompressionPreset::Default
        // 才会选到不含 Alpha 的 BC1(全部位宽留给 RGB, 画质更高), 而不是因为源图片带 Alpha
        // 就被迫选中画质更低的 BC3。
        bool mDiscardAlpha = false;
        bool mIsSRGB = true;
        CompressionPreset mPreset = CompressionPreset::Default;
        SampleState mSampleState;
    };
}
