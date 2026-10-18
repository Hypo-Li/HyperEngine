#include "Buffer.h"

#include <Core/Common/LogSystem.h>
#include <cassert>

namespace hyp::wgpu
{
    static WGPUBufferUsage convertBufferUsage(rhi::BufferUsage usage) noexcept
    {
        WGPUBufferUsage result = WGPUBufferUsage_None;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Transfer_Src))
            result |= WGPUBufferUsage_CopySrc;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Transfer_Dst))
            result |= WGPUBufferUsage_CopyDst;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Uniform))
            result |= WGPUBufferUsage_Uniform;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Storage))
            result |= WGPUBufferUsage_Storage;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Index))
            result |= WGPUBufferUsage_Index;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Vertex))
            result |= WGPUBufferUsage_Vertex;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Indirect))
            result |= WGPUBufferUsage_Indirect;
        return result;
    }

    Buffer::Buffer(const Device& device, const rhi::BufferDesc& desc, rhi::MemoryType memoryType)
        : rhi::Buffer(device, desc, memoryType)
    {
        WGPUBufferUsage usage = convertBufferUsage(desc.usage);

        switch (memoryType)
        {
        case rhi::MemoryType::Cpu_To_Gpu:
            usage |= WGPUBufferUsage_MapWrite;
            break;
        case rhi::MemoryType::Gpu_To_Cpu:
            usage |= WGPUBufferUsage_MapRead;
            break;
        case rhi::MemoryType::Gpu_Only:
        default:
            break;
        }

        WGPUBufferDescriptor bufferDesc = {};
        bufferDesc.nextInChain = nullptr;
        bufferDesc.label = WGPUStringView{ "UnnamedWebGPU Buffer", WGPU_STRLEN };
        bufferDesc.usage = usage;
        bufferDesc.size = desc.size;
        bufferDesc.mappedAtCreation = false;

        mBuffer = wgpuDeviceCreateBuffer(device.getHandle(), &bufferDesc);
    }

    Buffer::~Buffer()
    {
        if (mBuffer)
        {
            wgpuBufferRelease(mBuffer);
            mBuffer = nullptr;
        }
    }

    struct BufferMapUserData
    {
        bool done = false;
        WGPUMapAsyncStatus status;
    };

    static void onBufferMapped(WGPUMapAsyncStatus status, WGPUStringView /*message*/,
                               void* userdata1, void* /*userdata2*/)
    {
        auto* data = static_cast<BufferMapUserData*>(userdata1);
        data->status = status;
        data->done = true;
    }

    void* Buffer::map()
    {
        assert(getMemoryType() != rhi::MemoryType::Gpu_Only && "Cannot map Gpu_Only WebGPU buffer");

        WGPUMapMode mode = (getMemoryType() == rhi::MemoryType::Gpu_To_Cpu)
            ? WGPUMapMode_Read
            : WGPUMapMode_Write;

        BufferMapUserData userdata;
        WGPUBufferMapCallbackInfo callbackInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = onBufferMapped;
        callbackInfo.userdata1 = &userdata;

        WGPUFuture future = wgpuBufferMapAsync(mBuffer, mode, 0, getDesc().size, callbackInfo);

        WGPUFutureWaitInfo waitInfo = {};
        waitInfo.future = future;
        waitInfo.completed = WGPU_FALSE;

        WGPUWaitStatus waitStatus = wgpuInstanceWaitAny(
            getWgpuDevice().getInstance(), 1, &waitInfo, UINT64_MAX);

        if (waitStatus != WGPUWaitStatus_Success ||
            !waitInfo.completed ||
            userdata.status != WGPUMapAsyncStatus_Success)
        {
            LOG_ERROR("Failed to map WebGPU buffer");
            return nullptr;
        }

        // 读映射必须用 GetConstMappedRange: WebGPU C API 把可写/只读映射拆成了两个函数, 对
        // MapMode::Read 映射的 buffer 调 wgpuBufferGetMappedRange() 会直接返回 nullptr——它不
        // 走校验层报错, 是静默失败, 所以调用方只会看到"map 返回空"而没有任何 Dawn 错误信息。
        mMappedData = (mode == WGPUMapMode_Read)
            ? const_cast<void*>(wgpuBufferGetConstMappedRange(mBuffer, 0, getDesc().size))
            : wgpuBufferGetMappedRange(mBuffer, 0, getDesc().size);

        if (!mMappedData)
        {
            // MapAsync 已经成功了, 此时 buffer 在 Dawn 那边处于 mapped 状态。这里必须还原成
            // unmapped 再返回, 否则它会一直停在 mapped: 后续任何用到它的提交都会被判成
            // "used in submit while mapped", 下一次 MapAsync 也会被判成 "is already mapped",
            // 而且由于调用方一般只在拿到非空指针时才配对调用 unmap(), 这个状态再也回不来。
            LOG_ERROR("Failed to get mapped range of WebGPU buffer");
            wgpuBufferUnmap(mBuffer);
        }
        return mMappedData;
    }

    void Buffer::unmap()
    {
        mMappedData = nullptr;
        wgpuBufferUnmap(mBuffer);
    }

    void Buffer::setDebugName(HString name)
    {
        rhi::Buffer::setDebugName(name);
        WGPUStringView label = WGPUStringView{ getDebugName().toCString(), WGPU_STRLEN };
        wgpuBufferSetLabel(mBuffer, label);
    }
}
