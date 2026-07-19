#pragma once
#include "Buffer.h"
#include "Texture.h"
#include "Pipeline.h"

#include <Core/Common/EnumBitwise.h>
#include <Core/Math/Vector.h>

#include <algorithm>
#include <deque>
#include <functional>

namespace hyp::rhi
{
    enum class QueueType
    {
        Graphics,
        Compute,
        Transfer
    };

    enum class CommandType : uint16_t
    {
        // State
        Bind_Pipeline,
        Set_Bind_Group,
        Bind_Vertex_Buffers,
        Bind_Index_Buffer,
        Set_Viewport,
        Set_Scissor,
        Set_Blend_Constants,
        Set_Stencil_Reference,
        Set_Immediate_Data,

        // Render Pass
        Begin_Render_Pass,
        End_Render_Pass,

        // Compute Pass
        Begin_Compute_Pass,
        End_Compute_Pass,

        // Draw Call
        Draw,
        Draw_Indexed,
        Draw_Indirect,
        Draw_Indexed_Indirect,

        Dispatch,
        Dispatch_Indirect,

        // Barriers
        Barrier,

        // Buffer
        Copy_Buffer,
        Copy_Buffer_To_Texture,
        Clear_Buffer,

        // Texture
        Copy_Texture,
        Copy_Texture_To_Buffer,

        // Debug
        Begin_Debug_Group,
        End_Debug_Group,
        Insert_Debug_Marker,

        // 逃生舱: 把某个后端的原生 pass 句柄(VkCommandBuffer / WGPURenderPassEncoder)交给调用方
        // 自己去调用第三方库(比如 ImGui 的 backend)—— 见 NativeRenderCallback 的注释。
        Native_Callback,
    };

    struct CommandHeader
    {
        CommandType type;
    };

    struct BindPipelineCommand : public CommandHeader
    {
        const Pipeline* pipeline;
    };

    struct SetBindGroupCommand : public CommandHeader
    {
        uint32_t index;
        PipelineBindPoint bindPoint;
        const PipelineLayout* pipelineLayout;
        const BindGroup* bindGroup;
        uint32_t dynamicOffsetCount;
        // uint32_t dynamicOffsets[dynamicOffsetCount];
    };

    struct BindVertexBuffersCommand : public CommandHeader
    {
        uint32_t firstBinding;
        uint32_t bindingCount;
        // const Buffer* buffers[bindingCount];
        // uint64_t offsets[bindingCount];
    };

    enum class IndexType : uint8_t
    {
        Uint16,
        Uint32
    };

    struct BindIndexBufferCommand : public CommandHeader
    {
        const Buffer* buffer;
        uint64_t offset;
        IndexType indexType;
    };

    struct SetViewportCommand : public CommandHeader
    {
        float x;
        float y;
        float width;
        float height;
        float minDepth;
        float maxDepth;
    };

    struct SetScissorCommand : public CommandHeader
    {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
    };

    struct SetBlendConstantsCommand : public CommandHeader
    {
        float4 blendConstants;
    };

    struct SetStencilReferenceCommand : public CommandHeader
    {
        uint32_t reference;
    };

    struct SetImmediateDataCommand : public CommandHeader
    {
        const PipelineLayout* pipelineLayout;
        uint32_t offset;
        uint32_t size;
        // uint8_t data[size];
    };

    struct RenderArea
    {
        int32_t x;
        int32_t y;
        uint32_t width;
        uint32_t height;
    };

    enum class AttachmentLoadOp : uint8_t
    {
        Load,
        Clear,
        Dont_Care
    };

    enum class AttachmentStoreOp : uint8_t
    {
        Store,
        Dont_Care
    };

    union ClearColorValue
    {
        float4 float32;
        int4 int32;
        uint4 uint32;
    };

    enum class ResourceState
    {
        None = 0,
        Indirect_Argument = 1 << 0,
        Index_Attachment = 1 << 1,
        Vertex_Attachment = 1 << 2,

        Graphics_Shader_Uniform = 1 << 3,
        Compute_Shader_Uniform = 1 << 4,

        Graphics_Shader_Sampled = 1 << 5,
        Compute_Shader_Sampled = 1 << 6,

        Graphics_Shader_Storage_Read = 1 << 7,
        Compute_Shader_Storage_Read = 1 << 8,
        Graphics_Shader_Storage_Write = 1 << 9,
        Compute_Shader_Storage_Write = 1 << 10,

        Color_Attachment_Write = 1 << 11,
        Color_Attachment = 1 << 12,
        Depth_Attachment_Read = 1 << 13,
        Depth_Attachment = 1 << 14,
        Stencil_Attachment_Read = 1 << 15,
        Stencil_Attachment = 1 << 16,

        Transfer_Src = 1 << 17,
        Transfer_Dst = 1 << 18,

        Readback = 1 << 19,
        Present = 1 << 20,

        Shader_Uniform = Graphics_Shader_Uniform | Compute_Shader_Uniform,

        Shader_Sampled = Graphics_Shader_Sampled | Compute_Shader_Sampled,

        Graphics_Shader_Storage = Graphics_Shader_Storage_Read | Graphics_Shader_Storage_Write,
        Compute_Shader_Storage = Compute_Shader_Storage_Read | Compute_Shader_Storage_Write,
        Shader_Storage_Read = Graphics_Shader_Storage_Read | Compute_Shader_Storage_Read,
        Shader_Storage_Write = Graphics_Shader_Storage_Write | Compute_Shader_Storage_Write,
        Shader_Storage = Graphics_Shader_Storage | Compute_Shader_Storage,

        Depth_Stencil_Attachment_Read = Depth_Attachment_Read | Stencil_Attachment_Read,
        Depth_Read_Stencil_Attachment = Depth_Attachment_Read | Stencil_Attachment,
        Depth_Stencil_Read_Attachment = Depth_Attachment | Stencil_Attachment_Read,
        Depth_Stencil_Attachment = Depth_Attachment | Stencil_Attachment,
    };
    ENABLE_ENUM_CLASS_BITWISE(ResourceState)

    struct ColorAttachmentInfo
    {
        const TextureView* textureView;
        AttachmentLoadOp loadOp;
        AttachmentStoreOp storeOp;
        ClearColorValue clearColor;
    };

    struct ClearDepthStencilValue
    {
        float depth;
        uint32_t stencil;
    };

    struct DepthStencilAttachmentInfo
    {
        const TextureView* textureView;
        ResourceState state;
        AttachmentLoadOp depthLoadOp;
        AttachmentStoreOp depthStoreOp;
        AttachmentLoadOp stencilLoadOp;
        AttachmentStoreOp stencilStoreOp;
        ClearDepthStencilValue clearDepthStencil;
    };

    struct BeginRenderPassCommand : public CommandHeader
    {
        RenderArea renderArea;
        DepthStencilAttachmentInfo depthStencilAttachment;
        uint32_t colorAttachmentCount;
        // ColorAttachmentInfo colorAttachments[colorAttachmentCount];
    };

    struct EndRenderPassCommand : public CommandHeader
    {
    };

    struct BeginComputePassCommand : public CommandHeader
    {
    };

    struct EndComputePassCommand : public CommandHeader
    {
    };

    struct DrawCommand : public CommandHeader
    {
        uint32_t vertexCount;
        uint32_t instanceCount;
        uint32_t firstVertex;
        uint32_t firstInstance;
    };

    struct DrawIndexedCommand : public CommandHeader
    {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t vertexOffset;
        uint32_t firstInstance;
    };

    struct DrawIndirectCommand : public CommandHeader
    {
        const Buffer* buffer;
        uint64_t offset;
        uint32_t drawCount;
        uint32_t stride;
    };

    struct DrawIndexedIndirectCommand : public CommandHeader
    {
        const Buffer* buffer;
        uint64_t offset;
        uint32_t drawCount;
        uint32_t stride;
    };

    struct DispatchCommand : public CommandHeader
    {
        uint32_t groupCountX;
        uint32_t groupCountY;
        uint32_t groupCountZ;
    };

    struct DispatchIndirectCommand : public CommandHeader
    {
        const Buffer* buffer;
        uint64_t offset;
    };

    struct BufferBarrier
    {
        const Buffer* buffer;
        uint64_t offset;
        uint64_t size;
        ResourceState oldState;
        ResourceState newState;
    };

    struct TextureBarrier
    {
        const Texture* texture;
        TextureRange range;
        ResourceState oldState;
        ResourceState newState;
    };

    // buffer/texture barrier 合成一条 command,底层(Vulkan)可以用一个 VkDependencyInfo/
    // vkCmdPipelineBarrier2 同时携带 pBufferMemoryBarriers 和 pImageMemoryBarriers,
    // 不用为同一个同步点分别发两次 pipeline barrier
    struct BarrierCommand : public CommandHeader
    {
        uint32_t bufferBarrierCount;
        uint32_t textureBarrierCount;
        // BufferBarrier bufferBarriers[bufferBarrierCount];
        // TextureBarrier textureBarriers[textureBarrierCount];
    };

    struct BufferCopyRegion
    {
        uint64_t srcOffset;
        uint64_t dstOffset;
        uint64_t size;
    };

    struct CopyBufferCommand : public CommandHeader
    {
        const Buffer* srcBuffer;
        const Buffer* dstBuffer;
        uint32_t regionCount;
        // BufferCopyRegion regions[regionCount];
    };

    struct ClearBufferCommand : public CommandHeader
    {
        const Buffer* dstBuffer;
        uint64_t dstOffset;
        uint64_t size;
    };

    struct BufferTextureCopyRegion
    {
        uint64_t bufferOffset;
        // Unit in bytes
        uint32_t bytesPerRow;
        uint32_t rowsPerTexture;
        TextureSlice slice;
        int3 textureOffset;
        uint3 textureExtent;
    };

    struct CopyBufferToTextureCommand : public CommandHeader
    {
        const Buffer* srcBuffer;
        const Texture* dstTexture;
        uint32_t regionCount;
        // BufferTextureCopyRegion regions[regionCount];
    };

    struct TextureCopyRegion
    {
        TextureSlice srcSlice;
        int3 srcOffset;
        TextureSlice dstSlice;
        int3 dstOffset;
        uint3 extent;
    };

    struct CopyTextureCommand : public CommandHeader
    {
        const Texture* srcTexture;
        const Texture* dstTexture;
        uint32_t regionCount;
        // TextureCopyRegion regions[regionCount];
    };

    struct CopyTextureToBufferCommand : public CommandHeader
    {
        const Texture* srcTexture;
        const Buffer* dstBuffer;
        uint32_t regionCount;
        // BufferTextureCopyRegion regions[regionCount];
    };

    struct BeginDebugGroupCommand : public CommandHeader
    {
        HString label;
        float4 color;
    };

    struct EndDebugGroupCommand : public CommandHeader
    {
    };

    struct InsertDebugMarkerCommand : public CommandHeader
    {
        HString label;
        float4 color;
    };

    // 部分第三方库(比如 Dear ImGui 的 backend)需要直接拿到"正在录制中的原生 render pass 句柄"
    // 才能工作: Vulkan 是一个 VkCommandBuffer(在这个引擎里任何时候都能立即拿到 —— 见
    // Vulkan::CommandList::getHandle()), 但 WebGPU 需要一个活跃的 WGPURenderPassEncoder,
    // 而这个引擎默认按"先记录成一段不透明的 opcode 流, 提交时才重放"的方式使用 rhi::CommandList
    // (HYP_RHI_COMMAND_RECORDING_IMMEDIATE 默认关闭) —— 重放发生在 RenderGraph 的 execute()
    // 早就返回之后(FrameContext::render() 里 device.submit() 内部调用 CommandList::decode()),
    // 所以 pass 的 execute 回调运行期间根本不存在一个"活的" WGPURenderPassEncoder。
    //
    // invokeNativeCallback() 是解决办法: 把回调本身也当作一条 opcode 记录进指令流, 真正执行
    // (对 WebGPU 来说就是 decode() 重放到这一条时, 对 Vulkan 来说不管是否立即模式都一样)时,
    // 才用当时后端已经就绪的原生句柄去调用它 —— 这样调用方(比如 ImGui 集成代码)不需要关心
    // 自己此刻是在"记录"还是"重放", 也不需要关心当前是哪个后端, 只要在回调里根据
    // NativePassHandle::type 分支处理两种原生类型即可。
    enum class NativePassHandleType : uint8_t
    {
        Vulkan_Command_Buffer,   // handle 实际类型是 VkCommandBuffer, 已经处于某个 render pass 内
        WebGPU_Render_Pass_Encoder, // handle 实际类型是 WGPURenderPassEncoder
    };

    struct NativePassHandle
    {
        NativePassHandleType type;
        void* handle;
    };

    using NativeRenderCallback = std::function<void(const NativePassHandle&)>;

    struct NativeCallbackCommand : public CommandHeader
    {
        // 指向 CommandList::mNativeCallbacks 里某个堆分配的 std::function —— 调用方不需要
        // 自己另外准备一份稳定存储, 见 CommandList::invokeNativeCallback() 的注释。
        // (mStorage 是纯字节缓冲区, 写入新指令时会重新分配/搬移, 只适合存放 POD; std::function
        // 不保证能被裸内存拷贝安全"搬家", 所以这里不能像其它指令那样把它整个内嵌在指令流里,
        // 只能存一个指向别处稳定存储的指针。)
        const NativeRenderCallback* callback;
    };

#define HYP_RHI_COMMAND_LIST_METHODS(X) \
    X(void, bindPipeline, (const hyp::rhi::Pipeline* pipeline)) \
    X(void, setBindGroup, (uint32_t index, hyp::rhi::PipelineBindPoint bindPoint, const hyp::rhi::PipelineLayout* pipelineLayout, const hyp::rhi::BindGroup* bindGroup, std::span<const uint32_t> dynamicOffsets)) \
    X(void, bindVertexBuffers, (uint32_t firstBinding, std::span<const hyp::rhi::Buffer* const> buffers, std::span<const uint64_t> offsets)) \
    X(void, bindIndexBuffer, (const hyp::rhi::Buffer* buffer, uint64_t offset, hyp::rhi::IndexType indexType)) \
    X(void, setViewport, (float x, float y, float width, float height, float minDepth, float maxDepth)) \
    X(void, setScissor, (uint32_t x, uint32_t y, uint32_t width, uint32_t height)) \
    X(void, setBlendConstants, (float4 blendConstants)) \
    X(void, setStencilReference, (uint32_t reference)) \
    X(void, setImmediateData, (const hyp::rhi::PipelineLayout* pipelineLayout, uint32_t offset, std::span<const uint8_t> data)) \
    X(void, beginRenderPass, (hyp::rhi::RenderArea renderArea, const hyp::rhi::DepthStencilAttachmentInfo& depthStencilAttachment, std::span<const hyp::rhi::ColorAttachmentInfo> colorAttachments)) \
    X(void, endRenderPass, ()) \
    X(void, beginComputePass, ()) \
    X(void, endComputePass, ()) \
    X(void, draw, (uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)) \
    X(void, drawIndexed, (uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)) \
    X(void, drawIndirect, (const hyp::rhi::Buffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)) \
    X(void, drawIndexedIndirect, (const hyp::rhi::Buffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)) \
    X(void, dispatch, (uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)) \
    X(void, dispatchIndirect, (const hyp::rhi::Buffer* buffer, uint64_t offset)) \
    X(void, insertBarriers, (std::span<const hyp::rhi::BufferBarrier> bufferBarriers, std::span<const hyp::rhi::TextureBarrier> textureBarriers)) \
    X(void, copyBuffer, (const hyp::rhi::Buffer* srcBuffer, const hyp::rhi::Buffer* dstBuffer, std::span<const hyp::rhi::BufferCopyRegion> regions)) \
    X(void, copyBufferToTexture, (const hyp::rhi::Buffer* srcBuffer, const hyp::rhi::Texture* dstTexture, std::span<const hyp::rhi::BufferTextureCopyRegion> regions)) \
    X(void, clearBuffer, (const hyp::rhi::Buffer* dstBuffer, uint64_t dstOffset, uint64_t size)) \
    X(void, copyTexture, (const hyp::rhi::Texture* srcTexture, const hyp::rhi::Texture* dstTexture, std::span<const hyp::rhi::TextureCopyRegion> regions)) \
    X(void, copyTextureToBuffer, (const hyp::rhi::Texture* srcTexture, const hyp::rhi::Buffer* dstBuffer, std::span<const hyp::rhi::BufferTextureCopyRegion> regions)) \
    X(void, beginDebugGroup, (HString label, float4 color)) \
    X(void, endDebugGroup, ()) \
    X(void, insertDebugMarker, (HString label, float4 color)) \
    X(void, invokeNativeCallback, (const hyp::rhi::NativeRenderCallback& callback))

    class CommandList : public Refable
    {
    public:
        CommandList(const Device& device, QueueType queueType);

        virtual ~CommandList() = default;

        virtual void reset();

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        QueueType getQueueType() const noexcept
        {
            return mQueueType;
        }

        void bindPipeline(const Pipeline* pipeline);

        void setBindGroup(uint32_t index, PipelineBindPoint bindPoint, const PipelineLayout* pipelineLayout, const BindGroup* bindGroup, std::span<const uint32_t> dynamicOffsets);

        void bindVertexBuffers(uint32_t firstBinding, std::span<const Buffer* const> buffers, std::span<const uint64_t> offsets);

        void bindIndexBuffer(const Buffer* buffer, uint64_t offset, IndexType indexType);

        void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth);

        void setScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

        void setBlendConstants(float4 blendConstants);

        void setStencilReference(uint32_t reference);

        void setImmediateData(const PipelineLayout* pipelineLayout, uint32_t offset, std::span<const uint8_t> data);

        void beginRenderPass(RenderArea renderArea,
            const DepthStencilAttachmentInfo& depthStencilAttachment,
            std::span<const ColorAttachmentInfo> colorAttachments);

        void endRenderPass();

        void beginComputePass();

        void endComputePass();

        void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);

        void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);

        void drawIndirect(const Buffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride);

        void drawIndexedIndirect(const Buffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride);

        void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

        void dispatchIndirect(const Buffer* buffer, uint64_t offset);

        void insertBarriers(std::span<const BufferBarrier> bufferBarriers = {}, std::span<const TextureBarrier> textureBarriers = {});

        void copyBuffer(const Buffer* srcBuffer, const Buffer* dstBuffer, std::span<const BufferCopyRegion> regions);

        void copyBufferToTexture(const Buffer* srcBuffer, const Texture* dstTexture, std::span<const BufferTextureCopyRegion> regions);

        void clearBuffer(const Buffer* dstBuffer, uint64_t dstOffset, uint64_t size);

        void copyTexture(const Texture* srcTexture, const Texture* dstTexture, std::span<const TextureCopyRegion> regions);

        void copyTextureToBuffer(const Texture* srcTexture, const Buffer* dstBuffer, std::span<const BufferTextureCopyRegion> regions);

        void beginDebugGroup(HString label, float4 color);

        void endDebugGroup();

        void insertDebugMarker(HString label, float4 color);

        // 调用方按值传入(可以直接传一个临时 lambda), 不需要自己另外保证生命周期 ——
        // CommandList 会把它 move 进一份堆分配的稳定存储(mNativeCallbacks), 一直保留到
        // reset() 才释放。reset() 只在下一帧开始录制前才被调用, 而这一帧的 decode() 严格发生
        // 在 reset() 之前(FrameContext::render() 里先 execute() 记录、再 submit() 内部调用
        // decode() 重放, 都在渲染线程同一次调用里顺序发生), 所以 decode() 真正执行这条指令时,
        // 存储一定还没被清空。
        void invokeNativeCallback(NativeRenderCallback callback);

#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
#define HYP_RHI_DECLARE_BACKEND(ret, name, params) \
        virtual ret name##Backend params = 0;
        HYP_RHI_COMMAND_LIST_METHODS(HYP_RHI_DECLARE_BACKEND)
#undef HYP_RHI_DECLARE_BACKEND
#endif

    protected:
        std::span<const uint8_t> getStorage() const noexcept
        {
            return mStorage;
        }

        static const uint8_t* alignPointer(const uint8_t* ptr, size_t alignment) noexcept;

    private:
        static constexpr size_t alignUp(size_t value, size_t alignment)
        {
            return (value + (alignment - 1)) & ~(alignment - 1);
        }

        template <typename CmdTypeStruct, typename... Args>
        void pushCommand(CommandType type, Args&&... args)
        {
            uint8_t* ptr = allocateCommand(sizeof(CmdTypeStruct), alignof(CmdTypeStruct));
            CmdTypeStruct* cmd = ::new (ptr) CmdTypeStruct{ type, std::forward<Args>(args)... };
        }

        template <typename CmdTypeStruct, typename TrailingType = uint8_t, typename... Args>
        void pushCommand1Trailing(CommandType type, std::span<const TrailingType> trailingData, Args&&... args)
        {
            static constexpr size_t kStructSize = sizeof(CmdTypeStruct);
            static constexpr size_t kTrailingAlignment = alignof(TrailingType);
            const size_t trailingByteSize = trailingData.size_bytes();

            size_t totalSize = kStructSize;
            if (trailingByteSize > 0)
            {
                totalSize = alignUp(totalSize, kTrailingAlignment) + trailingByteSize;
            }

            uint8_t* ptr = allocateCommand(totalSize, alignof(CmdTypeStruct));
            CmdTypeStruct* cmd = ::new (ptr) CmdTypeStruct{ type, std::forward<Args>(args)... };

            if (!trailingData.empty())
            {
                uintptr_t trailingAddr = alignUp(reinterpret_cast<uintptr_t>(cmd) + kStructSize, kTrailingAlignment);
                std::memcpy(reinterpret_cast<void*>(trailingAddr), trailingData.data(), trailingByteSize);
            }
        }

        template<typename CmdTypeStruct, typename T1, typename T2, typename... Args>
        void pushCommand2Trailing(
            CommandType type,
            std::span<const T1> trailing1,
            std::span<const T2> trailing2,
            Args&&... args
        )
        {
            const size_t kStructSize = sizeof(CmdTypeStruct);
            const size_t size1 = trailing1.size_bytes();
            const size_t size2 = trailing2.size_bytes();

            size_t totalSize = kStructSize;
            if (size1 > 0)
            {
                totalSize = alignUp(totalSize, alignof(T1)) + size1;
            }
            if (size2 > 0)
            {
                totalSize = alignUp(totalSize, alignof(T2)) + size2;
            }

            uint8_t* ptr = allocateCommand(totalSize, alignof(CmdTypeStruct));
            CmdTypeStruct* cmd = ::new (ptr) CmdTypeStruct{ type, std::forward<Args>(args)... };

            uintptr_t addr = reinterpret_cast<uintptr_t>(cmd) + kStructSize;
            if (!trailing1.empty())
            {
                addr = alignUp(addr, alignof(T1));
                std::memcpy(reinterpret_cast<void*>(addr), trailing1.data(), size1);
                addr += size1;
            }

            if (!trailing2.empty())
            {
                addr = alignUp(addr, alignof(T2));
                std::memcpy(reinterpret_cast<void*>(addr), trailing2.data(), size2);
            }
        }

        uint8_t* allocateCommand(size_t size, size_t alignment);

    private:
        const Device& mDevice;
        QueueType mQueueType;
        std::vector<uint8_t> mStorage;
        // invokeNativeCallback() 的稳定存储, 见其注释; reset() 清空。用 deque 而不是 vector:
        // 标准保证 deque 的 push_back/emplace_back 不会使已有元素的引用/指针失效(它按块分配,
        // 已构造的元素不会被搬移), 因此可以直接把 NativeRenderCallback 存在这里、取地址交给
        // NativeCallbackCommand::callback —— 不像 vector<unique_ptr<...>> 那样每个 callback
        // 还需要额外一次堆分配才能拿到稳定地址。
        std::deque<NativeRenderCallback> mNativeCallbacks;
    };
}

