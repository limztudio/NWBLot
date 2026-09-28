// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#if defined(NWB_COOK)


#include "cook.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FontAtlasMetadata{
    inline constexpr AStringView s_DiagnosticPrefix = "Font atlas meta";

    [[nodiscard]] bool ReadU32(const Path& path, const Core::Metascript::Value& object, AStringView field, u32& outValue);
    [[nodiscard]] bool ReadHash(const Path& path, const Core::Metascript::Value& object, AStringView field, Sha256Digest& outValue);
    [[nodiscard]] bool ReadU32List(const Path& path, const Core::Metascript::Value& object, AStringView field, const NotNull<u32*> outValues, usize count);
    [[nodiscard]] bool ReadFloatList(const Path& path, const Core::Metascript::Value& object, AStringView field, const NotNull<f32*> outValues, usize count);
    [[nodiscard]] bool CheckToken(const Path& path, const Core::Metascript::Value& object, AStringView field, AStringView expected);
    [[nodiscard]] bool ReadRawPayload(const Path& path, const Core::Metascript::Value& record, u32 limit, Core::Assets::AssetBytes& outBytes);
    [[nodiscard]] bool ReadGroups(const Path& path, const Core::Metascript::Value& asset, FontAtlasPayload& outPayload);
    [[nodiscard]] bool ReadGlyphs(const Path& path, const Core::Metascript::Value& asset, FontAtlasPayload& outPayload);
    [[nodiscard]] bool ReadPositioning(const Path& path, const Core::Metascript::Value& asset, FontAtlasPayload& outPayload);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

