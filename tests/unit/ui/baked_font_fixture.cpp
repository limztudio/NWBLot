// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "baked_font_fixture.h"

#include <global/sha256.h>
#include <global/scope_exit.h>

#include <hb.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiFixtureSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void PopulateBakedFontTables(const Core::Assets::AssetBytes& bytes, FontAtlasPayload& payload, Core::Alloc::GlobalArena& arena){
    hb_blob_t* const blob = hb_blob_create(reinterpret_cast<const char*>(bytes.data()), static_cast<u32>(bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    ScopeExit releaseBlob([blob]()noexcept{ hb_blob_destroy(blob); });
    hb_face_t* const face = hb_face_create(blob, 0u);
    ScopeExit releaseFace([face]()noexcept{ hb_face_destroy(face); });
    payload.unitsPerEm = hb_face_get_upem(face);
    payload.sourceGlyphCount = hb_face_get_glyph_count(face);
    const u32 tags[]{ s_FontAtlasGdefTag, s_FontAtlasGposTag, s_FontAtlasKernTag };
    for(const u32 tag : tags){
        hb_blob_t* const tableBlob = hb_face_reference_table(face, tag);
        ScopeExit releaseTable([tableBlob]()noexcept{ hb_blob_destroy(tableBlob); });
        u32 size = 0u;
        const char* data = hb_blob_get_data(tableBlob, &size);
        if(size != 0u){
            payload.positioningTables.emplace_back(arena);
            FontAtlasPositioningTable& table = payload.positioningTables.back();
            table.tag = tag;
            table.bytes.assign(reinterpret_cast<const u8*>(data), reinterpret_cast<const u8*>(data) + size);
            table.sha256 = ComputeSha256({ table.bytes.data(), table.bytes.size() });
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

