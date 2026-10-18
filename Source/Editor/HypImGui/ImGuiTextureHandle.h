#pragma once
#include "ImGuiBackend.h"

#include <atomic>

namespace hyp
{
    // 桥接渲染线程产出的 ImTextureID 和逻辑线程的 ImGui::Image() 调用: 渲染线程在对应的
    // 离屏渲染目标(重新)创建之后调用 update(), 逻辑线程每帧调用 get() 读取"上一次渲染线程
    // 写入的值"。两者之间只靠一次原子 store/load 传递, 不需要额外加锁——逻辑线程读到的
    // ImTextureID 允许比渲染线程实际写入落后一帧, 这和 ImDrawData 本身跨线程传递的既有
    // 延迟约定(见 ImGuiFrontend 的注释)是一回事。
    class ImGuiTextureHandle
    {
    public:
        // 如果还有已注册的 ImTextureID, 析构时补一次 unregisterTexture()——不然持有这个
        // handle 的对象(比如 SceneViewport)被销毁时, 它当前引用着的 VkDescriptorSet
        // 不会经任何路径被摘除, 静默泄漏。
        ~ImGuiTextureHandle()
        {
            reset();
        }

        // 渲染线程: 纹理创建/resize 出新的 view 之后调用。旧 id(如果有)会通过
        // ImGuiBackend::unregisterTexture() 处理, 调用方不需要自己关心释放时机。
        void update(rhi::TextureView& view)
        {
            ImTextureID newID = ImGuiBackend::registerTexture(view);
            ImTextureID oldID = mTextureID.exchange(newID, std::memory_order_release);
            if (oldID)
                ImGuiBackend::unregisterTexture(oldID);
        }

        // 逻辑线程: 每帧读取当前有效的 ImTextureID。返回 0 表示对应的离屏渲染目标还没
        // 创建出第一份纹理(或者 ImGui 渲染后端未初始化成功), 这一帧应跳过 ImGui::Image()。
        ImTextureID get() const noexcept
        {
            return mTextureID.load(std::memory_order_acquire);
        }

        // 渲染线程: 在"销毁旧离屏渲染目标本身"之前调用(比如 RenderTarget resize 时先
        // destroy 旧 TextureProxy、再 create 新的)。这里的 unregisterTexture() 和旧
        // TextureProxy 析构时对旧 TextureView 的释放都是延迟 kMaxFramesInFlight 帧执行,
        // 谁先谁后就看谁先被推进 DeferredTaskQueue——不显式 reset() 先把旧描述符的移除
        // 排上, 而是等 update() 里"注册新的之后再顺手摘旧的", 旧 TextureView 的释放就会
        // 先于旧描述符的摘除被排进队列, 到期执行时变成"销毁一个还被某个 VkDescriptorSet
        // 引用着的 VkImageView", 触发校验层报错。
        void reset() noexcept
        {
            ImTextureID oldID = mTextureID.exchange(0, std::memory_order_release);
            if (oldID)
                ImGuiBackend::unregisterTexture(oldID);
        }

    private:
        std::atomic<ImTextureID> mTextureID{ 0 };
    };
}
