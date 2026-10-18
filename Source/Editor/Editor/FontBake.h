#pragma once
#include <Engine/Font/FontUtility.h>

#include <atomic>
#include <optional>
#include <thread>
#include <vector>

namespace hyp::ed
{
    // 在后台线程跑 IFontUtility::bake(), 主线程轮询进度, 完成后取结果
    class FontBakeJob
    {
    public:
        FontBakeJob(std::vector<uint8_t> faceData, FontBakeSettings settings);

        ~FontBakeJob();

        FontBakeJob(const FontBakeJob&) = delete;
        FontBakeJob& operator=(const FontBakeJob&) = delete;

        float getProgress() const noexcept
        {
            return mProgress.load(std::memory_order_relaxed);
        }

        bool isFinished() const noexcept
        {
            return mFinished.load(std::memory_order_acquire);
        }

        void cancel() noexcept
        {
            mCancelled.store(true, std::memory_order_relaxed);
        }

        const FontBakeSettings& getSettings() const noexcept
        {
            return mSettings;
        }

        // 仅在 isFinished() 之后调用
        std::optional<FontBakeResult> takeResult() noexcept
        {
            return std::move(mResult);
        }

    private:
        std::vector<uint8_t> mFaceData;
        FontBakeSettings mSettings;
        std::atomic<float> mProgress{ 0.0f };
        std::atomic<bool> mCancelled{ false };
        std::atomic<bool> mFinished{ false };
        std::optional<FontBakeResult> mResult;
        std::jthread mThread;
    };

    bool DrawFontBakeSettings(FontBakeSettings& settings);
}
