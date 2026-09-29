// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata.h"

#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FontAtlasMetadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ReadGroups(const Path& path, const Core::Metascript::Value& asset, FontAtlasPayload& outPayload){
    const Core::Metascript::Value* groups = Core::Assets::FindMetadataListField(path, asset, s_DiagnosticPrefix, "groups");
    if(!groups || groups->asList().empty() || groups->asList().size() > s_FontAtlasMaxGroupCount)
        return false;
    outPayload.groups.reserve(groups->asList().size());
    u64 totalBytes = 0u;
    for(const Core::Metascript::Value& record : groups->asList()){
        if(
            !Core::Assets::CheckMetadataAssetMap(path, record, s_DiagnosticPrefix)
            || !Core::Assets::ValidateMetadataAssetFields(path, record, s_DiagnosticPrefix, { "extent", "data", "byte_count", "sha256" })
        )
            return false;
        FontAtlasGroup group(outPayload.groups.get_allocator().arena());
        u32 extent[2u] = {};
        u32 byteCount = 0u;
        if(
            !ReadU32List(path, record, "extent", MakeNotNull(extent), LengthOf(extent)) || !ReadU32(path, record, "byte_count", byteCount)
            || !ReadHash(path, record, "sha256", group.sha256)
        )
            return false;
        group.width = extent[0u];
        group.height = extent[1u];
        if(
            group.width == 0u || group.height == 0u || group.width > s_FontAtlasMaxExtent || group.height > s_FontAtlasMaxExtent
            || static_cast<u64>(group.width) * group.height * 4u != byteCount || totalBytes + byteCount > s_FontAtlasMaxPixelBytes
        )
            return false;
        if(!ReadRawPayload(path, record, byteCount, group.pixels))
            return false;
        totalBytes += byteCount;
        outPayload.groups.push_back(Move(group));
    }
    return true;
}

bool ReadGlyphs(const Path& path, const Core::Metascript::Value& asset, FontAtlasPayload& outPayload){
    const Core::Metascript::Value* glyphs = Core::Assets::FindMetadataListField(path, asset, s_DiagnosticPrefix, "glyphs");
    if(
        !glyphs || glyphs->asList().empty() || glyphs->asList().size() > s_FontAtlasMaxGlyphCount
        || glyphs->asList().size() != outPayload.sourceGlyphCount
    )
        return false;
    outPayload.glyphs.reserve(glyphs->asList().size());
    for(const Core::Metascript::Value& record : glyphs->asList()){
        if(!Core::Assets::CheckMetadataAssetMap(path, record, s_DiagnosticPrefix))
            return false;
        FontAtlasGlyph glyph;
        if(
            !ReadU32(path, record, "glyph_id", glyph.glyphId) || !ReadU32(path, record, "drawable", glyph.drawable)
            || !Core::Assets::ReadMetadataFiniteF32Field(path, record, s_DiagnosticPrefix, "advance_units", true, glyph.advanceUnits)
        )
            return false;
        if(glyph.drawable == 0u){
            if(!Core::Assets::ValidateMetadataAssetFields(path, record, s_DiagnosticPrefix, { "glyph_id", "drawable", "advance_units" }))
                return false;
        }
        else if(glyph.drawable == 1u){
            if(!Core::Assets::ValidateMetadataAssetFields(
                path, record, s_DiagnosticPrefix,
                { "glyph_id", "drawable", "advance_units", "group", "channel", "rect", "plane_bounds_units" }
            ))
                return false;
            u32 rectangle[4u] = {};
            f32 bounds[4u] = {};
            if(
                !ReadU32(path, record, "group", glyph.group) || !ReadU32(path, record, "channel", glyph.channel)
                || !ReadU32List(path, record, "rect", MakeNotNull(rectangle), LengthOf(rectangle))
                || !ReadFloatList(path, record, "plane_bounds_units", MakeNotNull(bounds), LengthOf(bounds))
            )
                return false;
            glyph.x = rectangle[0u]; glyph.y = rectangle[1u]; glyph.width = rectangle[2u]; glyph.height = rectangle[3u];
            glyph.planeLeft = bounds[0u]; glyph.planeTop = bounds[1u]; glyph.planeRight = bounds[2u]; glyph.planeBottom = bounds[3u];
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': drawable must be integer zero or one"), PathToString<tchar>(path));
            return false;
        }
        outPayload.glyphs.push_back(glyph);
    }
    return true;
}

bool ReadPositioning(const Path& path, const Core::Metascript::Value& asset, FontAtlasPayload& outPayload){
    const Core::Metascript::Value* tables = Core::Assets::FindMetadataListField(path, asset, s_DiagnosticPrefix, "positioning_tables");
    if(!tables || tables->asList().size() > 3u)
        return false;
    outPayload.positioningTables.reserve(tables->asList().size());
    u64 totalBytes = 0u;
    for(const Core::Metascript::Value& record : tables->asList()){
        if(
            !Core::Assets::CheckMetadataAssetMap(path, record, s_DiagnosticPrefix)
            || !Core::Assets::ValidateMetadataAssetFields(path, record, s_DiagnosticPrefix, { "tag", "data", "byte_count", "sha256" })
        )
            return false;
        FontAtlasPositioningTable table(outPayload.positioningTables.get_allocator().arena());
        AStringView tag;
        u32 byteCount = 0u;
        if(
            !Core::Assets::ReadMetadataStringField(path, record, s_DiagnosticPrefix, "tag", true, tag)
            || !ReadU32(path, record, "byte_count", byteCount) || !ReadHash(path, record, "sha256", table.sha256)
        )
            return false;
        if(tag == "kern")
            table.tag = s_FontAtlasKernTag;
        else if(tag == "GPOS")
            table.tag = s_FontAtlasGposTag;
        else if(tag == "GDEF")
            table.tag = s_FontAtlasGdefTag;
        else
            return false;
        if(
            byteCount == 0u || totalBytes + byteCount > s_FontAtlasMaxPositioningBytes
            || !ReadRawPayload(path, record, s_FontAtlasMaxPositioningBytes, table.bytes)
        )
            return false;
        totalBytes += byteCount;
        outPayload.positioningTables.push_back(Move(table));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

