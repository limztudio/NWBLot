// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_FontMaxSourceBytes = 32u * 1024u * 1024u;
inline constexpr u32 s_FontMaxTableCount = 256u;

// Schema 1 stores a single static, scalable SFNT face; shaping and glyph atlases belong to the UI text domain.
class Font final : public Core::Assets::TypedAsset<Font>{
public:
    NWB_DEFINE_ASSET_TYPE("font")


public:
    explicit Font(Core::Assets::AssetArena& arena)
        : m_fontBytes(arena)
    {}
    Font(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Font>(virtualPath)
        , m_fontBytes(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;
    void setFontBytes(Core::Assets::AssetBytes&& bytes, u32 faceIndex = 0u);


public:
    [[nodiscard]] const Core::Assets::AssetBytes& fontBytes()const{ return m_fontBytes; }
    [[nodiscard]] u32 faceIndex()const{ return m_faceIndex; }


private:
    Core::Assets::AssetBytes m_fontBytes;
    u32 m_faceIndex = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(FontAssetCodec, Font);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

