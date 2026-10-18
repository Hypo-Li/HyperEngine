#pragma once
#include "../../Framework/Component.h"

#include <Core/Math/Vector.h>
#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    struct AtmosphereParameters
    {
        float3 groundAlbedo = float3(0.4f);
        float groundRadiusKm = 6360.0f;
        float atmosphereHeightKm = 60.0f;
        float multiScatteringFactor = 1.0f;

        float rayleighScatteringScale = 0.0331f;
        float3 rayleighScattering = float3(0.175287f, 0.409607f, 1.0f);
        float rayleighScaleHeight = 8.0f;

        float mieScatteringScale = 0.003996f;
        float3 mieScattering = float3(1.0f);
        float mieAbsorptionScale = 0.000444f;
        float3 mieAbsorption = float3(1.0f);
        float mieAnisotropy = 0.8f;
        float mieScaleHeight = 1.2f;

        float ozoneAbsorptionScale = 0.001881f;
        float3 ozoneAbsorption = float3(0.345561f, 1.0f, 0.045189f);
        float ozoneHeight = 25.0f;
        float ozoneThickness = 15.0f;

        // UndoStack::trackGesture() 需要按值比较"手势前后是否真的有变化"来过滤掉空的
        // undo 记录——AtmospherePanel 把整份 AtmosphereParameters 当 trackGesture 的
        // payload(而不是逐字段各写一套), 所以整个结构体需要能比较。
        friend bool operator==(const AtmosphereParameters&, const AtmosphereParameters&) = default;
    };

    class Atmosphere : public Component
    {
        REFLECT_CLASS(Atmosphere)
    public:
        Atmosphere();

        ~Atmosphere() noexcept;

        virtual void onRegister() override;

        virtual void onUnregister() override;

        virtual void onTick(float deltaTime) override;

        virtual void onVisibilityChanged(bool visible) override;

        AtmosphereParameters& getParameters() noexcept
        {
            return mParameters;
        }

        const AtmosphereParameters& getParameters() const noexcept
        {
            return mParameters;
        }

        void updateParameters();

    private:
        AtmosphereParameters mParameters;
        AtmosphereProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<Atmosphere>();
}
