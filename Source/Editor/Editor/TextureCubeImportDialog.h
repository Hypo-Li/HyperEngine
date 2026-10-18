#pragma once
#include "ImportDialog.h"

#include <Engine/Texture/TextureCube.h>

#include <Core/Math/Vector.h>

#include <filesystem>
#include <optional>
#include <vector>

namespace hyp::ed
{
    // "Add > Texture Cube"的导入流程: 弹原生文件框选一张等距投影(equirectangular) HDR 图
    // -> 解码 -> 配置属性 -> Import 时用 equirectMapToCubemap() 重投影成 6 个 face -> TextureCube。
    class TextureCubeImportDialog : public ImportDialog
    {
    public:
        // 弹原生文件选择框(hdr/exr), 选中后同步解码。取消、解码失败、或者宽高比不是标准的
        // 2:1 等距投影都什么都不做(比例不对的情况会打一条 LOG_WARN, 不是完全无声)。
        void beginImport();

        bool hasPendingImport() const noexcept override
        {
            return !mEquirectPixels.empty();
        }

        std::optional<Result> draw() override;

    private:
        std::optional<std::filesystem::path> openFileDialog();

        std::filesystem::path mSourcePath;
        // 线性 HDR, 不含 alpha——等距环境图设计上就不需要透明通道。
        std::vector<float3> mEquirectPixels;
        int mWidth = 0;
        int mHeight = 0;
        // kFaceSizeOptions(见 .cpp)里的下标, 不是自由拖拽的分辨率——cube face 尺寸要求是
        // 2 的幂, 用固定挡位的 Combo 跟 SkyLightPanel 的 Prefilter Resolution 一致。解码成功
        // 时按 mHeight 自动选中一档, 用户可以再手动改选。
        int mFaceSizeIndex = 0;
        bool mIsSRGB = false;
        CompressionPreset mPreset = CompressionPreset::HDR;
        SampleState mSampleState;
    };
}
