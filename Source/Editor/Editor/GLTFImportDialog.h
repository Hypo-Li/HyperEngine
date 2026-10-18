#pragma once
#include "ImportDialog.h"

#include <filesystem>
#include <optional>

namespace hyp::ed
{
    // "Add > glTF Model"的导入流程: 弹原生文件框选 .gltf/.glb -> 配置是否导入材质 -> Import/Cancel。
    // 点 Import 之前不解析文件——beginImport() 只记路径, GLTFLoader::load() 只在点 Import 那
    // 一刻才调用一次。
    class GLTFImportDialog : public ImportDialog
    {
    public:
        void beginImport();

        bool hasPendingImport() const noexcept override
        {
            return !mSourcePath.empty();
        }

        std::optional<Result> draw() override;

    private:
        std::optional<std::filesystem::path> openFileDialog();

        std::filesystem::path mSourcePath;
        bool mImportMaterials = true;
        bool mImportSkinning = true;
        bool mImportAnimations = true;
    };
}
