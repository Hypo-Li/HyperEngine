#pragma once
#include "SceneElementProxy.h"
#include <Core/Math/Matrix.h>

namespace hyp
{
    class InstanceProxy : public SceneElementProxy
    {
    public:
        struct Desc
        {
            bool visible = true;
            Mobility mobility = Mobility::Static;
            double4x4 worldMatrix = double4x4(1.0);
        };

        InstanceProxy(SceneProxy& sceneProxy, const Desc& desc);

        ~InstanceProxy() noexcept override;

        // 非虚, 隐藏基类同名方法, 必须经由具体类型调用才会同步 InstanceData
        void setVisible(bool visible) noexcept;

        uint32_t getInstanceIndex() const noexcept
        {
            return mInstanceIndex;
        }

        // 非虚, DrawableProxy 用同名虚函数把它隐藏
        void setMobility(Mobility mobility) noexcept
        {
            mMobility = mobility;
        }

        Mobility getMobility() const noexcept
        {
            return mMobility;
        }

        // 用 world matrix 更新这个 proxy 在 SceneProxy 的 InstanceData 表里的那一份并标脏。
        // 真正覆盖之前会把当前值顺手存进 InstanceData::prevObjectToWorld, 供 TAA motion
        // vector 用——只在这里更新, 不需要额外的"每帧开始"钩子: 没动过的实例 prev 和 current
        // 天然相等, 速度天然是 0。
        void updateInstanceTransform(const double4x4& worldMatrix) noexcept;

    private:
        void syncVisibleToInstanceData() noexcept;

    private:
        uint32_t mInstanceIndex;
        Mobility mMobility;
    };
}
