// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>


#include <global/generic.h>
#include <global/span.h>

#include <ft2build.h>
#include FT_FREETYPE_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::FontCases{

class FreeTypeProbe final : NoCopy{
public:
    ~FreeTypeProbe()noexcept{
        closeFace();
        closeLibrary();
    }
    [[nodiscard]] i32 initialize()noexcept{ return FT_Init_FreeType(&m_library); }
    [[nodiscard]] i32 open(const Span<const u8> bytes)noexcept{
        return FT_New_Memory_Face(m_library, bytes.data(), static_cast<FT_Long>(bytes.size()), 0, &m_face);
    }
    i32 closeFace()noexcept{
        if(!m_face)
            return 0;
        const auto result = FT_Done_Face(m_face);
        m_face = nullptr;
        return result;
    }
    i32 closeLibrary()noexcept{
        if(!m_library)
            return 0;
        const auto result = FT_Done_FreeType(m_library);
        m_library = nullptr;
        return result;
    }

private:
    FT_Library m_library = nullptr;
    FT_Face m_face = nullptr;
};

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

