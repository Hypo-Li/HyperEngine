#pragma once
#include <Core/Math/Vector.h>
#include <RenderBase/RenderBackend.h>
#include <RHI/Buffer.h>
#include <RHI/CommandList.h>

namespace hyp
{
    struct GPUSplatData
    {
        float3 position;
        uint32_t instanceIndex;
        uint3 sigma;
        uint32_t color;
    };

    class GSplatScene
    {
    public:
        GSplatScene(RenderBackend& renderBackend);

        ~GSplatScene();

        uint32_t addGSplat(std::vector<GPUSplatData>&& splatData);

        void removeGSplat(uint32_t gsplatId);

        void prepare(rhi::CommandList& cmdList);

        rhi::Buffer& getSplatDataBuffer() const noexcept
        {
            return *mSplatDataBuffer;
        }

        uint32_t getTotalSplatCount() const noexcept
        {
            return mTotalSplatCount;
        }

    private:
        void flushPendingOperations(rhi::CommandList& cmdcmdList);

    private:
        RenderBackend& mRenderBackend;
        TRef<rhi::Buffer> mSplatDataBuffer;

        struct GSplatInfo
        {
            uint32_t offset = 0;
            uint32_t count = 0;
        };
        std::vector<GSplatInfo> mGSplatInfos;
        std::vector<uint32_t> mFreeGSplatIds;
        uint32_t mSplatCapacity = 0;
        uint32_t mTotalSplatCount = 0;

        enum class OpType
        {
            Add,
            Remove
        };

        struct Operation
        {
            OpType type;
            uint32_t gsplatId;
            std::vector<GPUSplatData> data;
        };
        std::vector<Operation> mPendingOperations;
    };
}
