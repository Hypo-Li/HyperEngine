#pragma once
#include "Material.h"
#include "MaterialParameter.h"

#include <span>
#include <vector>

namespace hyp
{
    class MaterialResourceObserver final : public ITextureListener, public IFontListener
    {
    public:
        explicit MaterialResourceObserver(Material& owner);

        ~MaterialResourceObserver();

        MaterialResourceObserver(const MaterialResourceObserver&) = delete;

        MaterialResourceObserver& operator=(const MaterialResourceObserver&) = delete;

        // 先摘掉之前监听的全部资源, 再按这批参数值里引用到的纹理和字体重新监听
        void sync(std::span<const MaterialParameterValue> values);

        void onTextureChanged() override;

        void onFontChanged() override;

    private:
        void clear();

    private:
        Material& mOwner;
        std::vector<TRef<Texture>> mTextures;
        std::vector<TRef<Font>> mFonts;
    };
}
