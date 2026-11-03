// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"

#include <core/common/log.h>

#include <global/sha256.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 PackGlyphs(const BakeOptions& options, const Span<const RasterGlyph> glyphs, Core::Alloc::ScratchArena& scratch, Impl::FontAtlasPayload& outPayload){
    Vector<u32, Core::Alloc::ScratchArena> order(scratch);
    order.reserve(glyphs.size());
    for(u32 index = 0u; index < glyphs.size(); ++index){
        if(glyphs[index].record.drawable != 0u)
            order.push_back(index);
    }
    Sort(order.begin(), order.end(), [&glyphs](u32 left, u32 right)noexcept{
        return IsFontAtlasGlyphPackOrderBefore(glyphs[left].record, glyphs[right].record);
    });
    outPayload.glyphs.reserve(glyphs.size());
    for(const RasterGlyph& glyph : glyphs)
        outPayload.glyphs.push_back(glyph.record);
    Array<Shelf, s_MaxBakeShelfPages> shelves{};
    u32 lastPage = 0u;
    for(const u32 index : order){
        const RasterGlyph& glyph = glyphs[index];
        const u32 width = glyph.record.width + s_GlyphGuardExtent;
        const u32 height = glyph.record.height + s_GlyphGuardExtent;
        const auto placed = TryPlaceGlyphInShelves(width, height, options.extent, options.maxGroups, shelves);
        if(!placed){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: capacity exhausted at glyph {} footprint {}x{} in {}x{} / {} groups; increase extent or lower ppem"), glyph.record.glyphId, width, height, options.extent, options.extent, options.maxGroups);
            return false;
        }
        const u32 groupIndex = placed->group;
        while(outPayload.groups.size() <= groupIndex){
            Impl::FontAtlasGroup group(outPayload.groups.get_allocator().arena());
            group.width = options.extent;
            group.height = options.extent;
            group.channelCount = Impl::s_FontAtlasMaxChannelCount;
            group.pixels.resize(static_cast<usize>(options.extent) * options.extent * Impl::s_FontAtlasMaxChannelCount, 0u);
            outPayload.groups.push_back(Move(group));
        }
        CopyGlyphToAtlasGroup(glyph.pixels, options.extent, *placed, outPayload.glyphs[index], outPayload.groups[groupIndex].pixels);
        lastPage = Max(lastPage, placed->page);
    }
    CompactAtlasGroups(outPayload);
    for(auto& group : outPayload.groups)
        group.sha256 = ComputeSha256(BinaryByteView{ .bytes = group.pixels.data(), .byteCount = group.pixels.size() });
    NWB_LOGGER_INFO(NWB_TEXT("font_builder: packed {} glyphs into {} logical pages / {} compact groups"), glyphs.size(), order.empty() ? 0u : lastPage + 1u, outPayload.groups.size());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

