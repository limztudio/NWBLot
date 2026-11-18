// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include <impl/assets_font/freetype_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::FontBuilderUtility::FreeTypeSdk{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ModuleArena, typename PropertyArena>
[[nodiscard]] inline i32 SetUnsignedProperty(const FontSdk::FreeTypeLibrary& library, const ::AString<ModuleArena>& module, const ::AString<PropertyArena>& property, const u32 value)noexcept{
    return FT_Property_Set(library.handle(), module.c_str(), property.c_str(), &value);
}

[[nodiscard]] inline i32 Ascender(const FontSdk::FreeTypeFaceView& face)noexcept{ return face.handle()->ascender; }
[[nodiscard]] inline i32 Descender(const FontSdk::FreeTypeFaceView& face)noexcept{ return face.handle()->descender; }
[[nodiscard]] inline i32 Height(const FontSdk::FreeTypeFaceView& face)noexcept{ return face.handle()->height; }
[[nodiscard]] inline i64 GlyphAdvanceX(const FontSdk::FreeTypeFaceView& face)noexcept{ return face.handle()->glyph->advance.x; }
[[nodiscard]] inline i32 RenderSdf(FontSdk::FreeTypeFaceView& face)noexcept{ return FT_Render_Glyph(face.handle()->glyph, FT_RENDER_MODE_SDF); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
