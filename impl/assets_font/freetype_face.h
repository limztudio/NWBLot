// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/common/log.h>

#include <global/diagnostics.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_SYSTEM_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FreeTypeMemoryFace final : NoCopy{
private:
    [[nodiscard]] static void* Allocate(FT_Memory memory, FT_Long size)noexcept;
    static void Release(FT_Memory memory, void* block)noexcept;
    [[nodiscard]] static void* Reallocate(FT_Memory memory, FT_Long oldSize, FT_Long newSize, void* block)noexcept;


public:
    explicit FreeTypeMemoryFace(Core::Assets::AssetArena& arena)noexcept;
    ~FreeTypeMemoryFace()noexcept;


public:
    void reset()noexcept;
    [[nodiscard]] Expected<void, NativeFontFailure> openStaticScalableFace(Span<const u8> bytes)noexcept;
    [[nodiscard]] b8 openAndValidate(Span<const u8> bytes, u32 faceIndex);

    [[nodiscard]] FT_Library library()const noexcept{ return m_library; }
    [[nodiscard]] FT_Face face()const noexcept{ return m_face; }


private:
    FT_MemoryRec_ m_memory = {};
    FT_Library m_library = nullptr;
    FT_Face m_face = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

