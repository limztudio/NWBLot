// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "atlas_compact.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CompactAtlasGroups(Impl::FontAtlasPayload& payload){
    for(usize groupIndex = 0u; groupIndex < payload.groups.size(); ++groupIndex){
        auto& group = payload.groups[groupIndex];
        u32 firstX = group.width;
        u32 firstY = group.height;
        u32 endX = 0u;
        u32 endY = 0u;
        u32 channelCount = 0u;
        for(const auto& glyph : payload.glyphs){
            if(glyph.drawable == 0u || glyph.group != groupIndex)
                continue;
            firstX = Min(firstX, glyph.x - payload.guardTexels);
            firstY = Min(firstY, glyph.y - payload.guardTexels);
            endX = Max(endX, glyph.x + glyph.width + payload.guardTexels);
            endY = Max(endY, glyph.y + glyph.height + payload.guardTexels);
            channelCount = Max(channelCount, glyph.channel + 1u);
        }
        NWB_ASSERT(channelCount > 0u);
        const u32 width = endX - firstX;
        const u32 height = endY - firstY;
        Core::Assets::AssetBytes compactPixels(group.pixels.get_allocator().arena());
        compactPixels.resize(static_cast<usize>(width) * height * channelCount);
        for(u32 y = 0u; y < height; ++y){
            for(u32 x = 0u; x < width; ++x){
                const usize source = (static_cast<usize>(firstY + y) * group.width + firstX + x) * group.channelCount;
                const usize destination = (static_cast<usize>(y) * width + x) * channelCount;
                for(u32 channel = 0u; channel < channelCount; ++channel)
                    compactPixels[destination + channel] = group.pixels[source + channel];
            }
        }
        for(auto& glyph : payload.glyphs){
            if(glyph.drawable != 0u && glyph.group == groupIndex){
                glyph.x -= firstX;
                glyph.y -= firstY;
            }
        }
        group.width = width;
        group.height = height;
        group.channelCount = channelCount;
        group.pixels = Move(compactPixels);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

