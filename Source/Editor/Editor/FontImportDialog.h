#pragma once
#include "FontBake.h"
#include "ImportDialog.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace hyp::ed
{
    class FontImportDialog : public ImportDialog
    {
    public:
        void beginImport();

        bool hasPendingImport() const noexcept override
        {
            return !mFaceData.empty();
        }

        std::optional<Result> draw() override;

    private:
        std::optional<std::filesystem::path> openFileDialog();

        void refreshFaceInfo();

        void close();

        std::filesystem::path mSourcePath;
        std::vector<uint8_t> mFaceData;
        FontFaceInfo mFaceInfo;
        FontBakeSettings mSettings;

        TRef<Font> mFont;
        std::unique_ptr<FontBakeJob> mJob;
        bool mBakeFailed = false;
    };
}
