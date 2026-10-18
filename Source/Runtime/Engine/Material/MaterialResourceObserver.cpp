#include "MaterialResourceObserver.h"

#include <algorithm>

namespace hyp
{
    MaterialResourceObserver::MaterialResourceObserver(Material& owner) :
        mOwner(owner)
    {
    }

    MaterialResourceObserver::~MaterialResourceObserver()
    {
        clear();
    }

    void MaterialResourceObserver::sync(std::span<const MaterialParameterValue> values)
    {
        clear();

        auto observeTexture = [this](Texture* texture)
        {
            if (!texture || std::find_if(mTextures.begin(), mTextures.end(),
                [texture](const TRef<Texture>& observed) { return observed.get() == texture; }) != mTextures.end())
                return;

            texture->addListener(this);
            mTextures.emplace_back(texture);
        };
        auto observeFont = [this](Font* font)
        {
            if (!font || std::find_if(mFonts.begin(), mFonts.end(),
                [font](const TRef<Font>& observed) { return observed.get() == font; }) != mFonts.end())
                return;

            font->addListener(this);
            mFonts.emplace_back(font);
        };

        for (const MaterialParameterValue& value : values)
        {
            switch (value.getType())
            {
            case MaterialParameterType::Texture_2D:
                observeTexture(value.get<SampledTexture2D>().texture.get());
                break;
            case MaterialParameterType::Texture_2D_Array:
                observeTexture(value.get<SampledTexture2DArray>().texture.get());
                break;
            case MaterialParameterType::Texture_3D:
                observeTexture(value.get<SampledTexture3D>().texture.get());
                break;
            case MaterialParameterType::Texture_Cube:
                observeTexture(value.get<SampledTextureCube>().texture.get());
                break;
            case MaterialParameterType::Font:
                observeFont(value.get<SampledFont>().font.get());
                break;
            default:
                break;
            }
        }
    }

    void MaterialResourceObserver::onTextureChanged()
    {
        mOwner.onParameterResourceChanged();
    }

    void MaterialResourceObserver::onFontChanged()
    {
        mOwner.onParameterResourceChanged();
    }

    void MaterialResourceObserver::clear()
    {
        for (TRef<Texture>& texture : mTextures)
            texture->removeListener(this);
        mTextures.clear();

        for (TRef<Font>& font : mFonts)
            font->removeListener(this);
        mFonts.clear();
    }
}
