// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"
#include "atlas_compact.h"

#include <core/common/log.h>

#include <global/sha256.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_builder_pack{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Shelf{
    u32 x = 0u;
    u32 y = 0u;
    u32 height = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PackGlyphs(const BakeOptions& options, const RasterGlyphs& glyphs, Impl::FontAtlasPayload& outPayload, Core::Alloc::ScratchArena& scratch){
    Vector<u32, Core::Alloc::ScratchArena> order(scratch);
    order.reserve(glyphs.size());
    for(u32 index = 0u; index < glyphs.size(); ++index){
        if(glyphs[index].record.drawable != 0u)
            order.push_back(index);
    }
    Sort(order.begin(), order.end(), [&glyphs](u32 left, u32 right){
        const auto& a = glyphs[left].record;
        const auto& b = glyphs[right].record;
        if(a.height != b.height)
            return a.height > b.height;
        if(a.width != b.width)
            return a.width > b.width;
        return a.glyphId < b.glyphId;
    });
    outPayload.glyphs.reserve(glyphs.size());
    for(const RasterGlyph& glyph : glyphs)
        outPayload.glyphs.push_back(glyph.record);
    __hidden_font_builder_pack::Shelf shelves[Impl::s_FontAtlasMaxGroupCount * 4u]{};
    u32 lastPage = 0u;
    for(const u32 index : order){
        const RasterGlyph& glyph = glyphs[index];
        const u32 width = glyph.record.width + 2u;
        const u32 height = glyph.record.height + 2u;
        bool placed = false;
        for(u32 page = 0u; page < options.maxGroups * 4u; ++page){
            auto next = shelves[page];
            if(next.x + width > options.extent){
                next.x = 0u;
                next.y += next.height;
                next.height = 0u;
            }
            if(next.y + height > options.extent)
                continue;
            const u32 groupIndex = page / 4u;
            while(outPayload.groups.size() <= groupIndex){
                Impl::FontAtlasGroup group(outPayload.groups.get_allocator().arena());
                group.width = options.extent;
                group.height = options.extent;
                group.channelCount = 4u;
                group.pixels.resize(static_cast<usize>(options.extent) * options.extent * 4u, 0u);
                outPayload.groups.push_back(Move(group));
            }
            auto& record = outPayload.glyphs[index];
            record.group = groupIndex;
            record.channel = page % 4u;
            record.x = next.x + 1u;
            record.y = next.y + 1u;
            auto& pixels = outPayload.groups[groupIndex].pixels;
            for(u32 y = 0u; y < glyph.record.height; ++y){
                for(u32 x = 0u; x < glyph.record.width; ++x){
                    const usize destination = (static_cast<usize>(record.y + y) * options.extent + record.x + x) * 4u + record.channel;
                    pixels[destination] = glyph.pixels[static_cast<usize>(y) * glyph.record.width + x];
                }
            }
            next.x += width;
            next.height = Max(next.height, height);
            shelves[page] = next;
            lastPage = Max(lastPage, page);
            placed = true;
            break;
        }
        if(!placed){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: capacity exhausted at glyph {} footprint {}x{} in {}x{} / {} groups; increase extent or lower ppem")
                , glyph.record.glyphId
                , width
                , height
                , options.extent
                , options.extent
                , options.maxGroups
            );
            return false;
        }
    }
    CompactAtlasGroups(outPayload);
    for(auto& group : outPayload.groups)
        group.sha256 = ComputeSha256(BinaryByteView{ .bytes = group.pixels.data(), .byteCount = group.pixels.size() });
    NWB_LOGGER_INFO(NWB_TEXT("font_builder: packed {} glyphs into {} logical pages / {} compact groups")
        , glyphs.size()
        , order.empty() ? 0u : lastPage + 1u
        , outPayload.groups.size()
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

