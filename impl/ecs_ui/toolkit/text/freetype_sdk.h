// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_font/freetype_sdk.h>
#include <global/optional.h>
#include FT_OUTLINE_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::UiFontSdk{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiFreeTypeFace final : NoCopy{
public:
    explicit UiFreeTypeFace(::ArenaBase& arena) : m_library(arena){}
    ~UiFreeTypeFace()noexcept{
        if(closeFace() != 0)
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = "impl/ecs_ui/font", .message = "FT_Done_Face failed", .file = __FILE__, .line = __LINE__ });
    }

    [[nodiscard]] i32 createLibrary()noexcept{ return m_library.createLibrary(); }
    void addDefaultModules()noexcept{ m_library.addDefaultModules(); }
    [[nodiscard]] i32 closeLibrary()noexcept{ return m_view ? FT_Err_Invalid_Argument : m_library.closeLibrary(); }

    // FontFace owns the unchanged SFNT bytes until after both native handles close.
    [[nodiscard]] i32 open(const Span<const u8> bytes, const i64 index)noexcept{
        if(m_view)
            return FT_Err_Invalid_Argument;
        FT_Face face = nullptr;
        const i32 error = m_library.openBytes(bytes, index, face);
        if(error == 0)
            m_view.emplace(face);
        return error;
    }

    [[nodiscard]] FontSdk::FreeTypeFaceView& view()noexcept{ return m_view.value(); }

    [[nodiscard]] i32 closeFace()noexcept{
        if(!m_view)
            return 0;
        const i32 result = FT_Done_Face(m_view->handle());
        m_view.reset();
        return result;
    }


private:
    FontSdk::FreeTypeLibrary m_library;
    Optional<FontSdk::FreeTypeFaceView> m_view;
};

[[nodiscard]] inline FT_BBox OutlineControlBox(const FontSdk::FreeTypeFaceView& face)noexcept{
    FT_BBox box{};
    FT_Outline_Get_CBox(&face.handle()->glyph->outline, &box);
    return box;
}

[[nodiscard]] inline b8 IsTrickyFace(const FontSdk::FreeTypeFaceView& face)noexcept{ return FT_IS_TRICKY(face.handle()) != 0; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

