// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "service.h"
#include "atlas.h"
#include "glyph_visibility.h"
#include "../layout/validation.h"

#include <core/alloc/scratch.h>

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_paint{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static f32 RoundMeasuredBound(const f64 value, const bool upper)noexcept{
    const f32 rounded = static_cast<f32>(value);
    if(upper ? static_cast<f64>(rounded) >= value : static_cast<f64>(rounded) <= value)
        return rounded;
    if(rounded == 0.0f)
        return BitCast<f32>(upper ? 1u : 0x80000001u);
    const u32 bits = BitCast<u32>(rounded);
    return BitCast<f32>((upper == (rounded > 0.0f)) ? bits + 1u : bits - 1u);
}

[[nodiscard]] static Expected<SIMDVectorDouble> MeasuredRectangle(const SIMDVectorDouble minimum, const SIMDVectorDouble maximum)noexcept{
    const f64 left = VectorDoubleGetX(minimum);
    const f64 top = VectorDoubleGetY(minimum);
    const f64 right = VectorDoubleGetX(maximum);
    const f64 bottom = VectorDoubleGetY(maximum);
    if(!IsFinite(left) || !IsFinite(top) || !IsFinite(right) || !IsFinite(bottom) || left < -Limit<f32>::s_Max || top < -Limit<f32>::s_Max || right > Limit<f32>::s_Max || bottom > Limit<f32>::s_Max || right < left || bottom < top)
        return MakeUnexpected(Failure{});
    const f32 x = RoundMeasuredBound(left, false);
    const f32 y = RoundMeasuredBound(top, false);
    const SIMDVectorDouble size = VectorDoubleSubtract(VectorDoubleSet(RoundMeasuredBound(right, true), RoundMeasuredBound(bottom, true), 0.0, 0.0), VectorDoubleSet(x, y, 0.0, 0.0));
    const f64 width = VectorDoubleGetX(size);
    const f64 height = VectorDoubleGetY(size);
    if(width > Limit<f32>::s_Max || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    const f32 roundedWidth = RoundMeasuredBound(width, true);
    const f32 roundedHeight = RoundMeasuredBound(height, true);
    if(
        !IsFinite(roundedWidth) || roundedWidth < 0.0f || !IsFinite(roundedHeight) || roundedHeight < 0.0f
        || !IsFinite(VectorGetX(VectorAdd(VectorReplicate(x), VectorReplicate(roundedWidth)))) || !IsFinite(VectorGetX(VectorAdd(VectorReplicate(y), VectorReplicate(roundedHeight))))
    )
        return MakeUnexpected(Failure{});
    return VectorDoubleSet(x, y, roundedWidth, roundedHeight);
}

[[nodiscard]] static Expected<SIMDVectorDouble> LoadSdfSupport(const PlacedGlyph& placed, const BakedFontAtlas& atlas, const f32 fontSize, const DisplayMetrics& display)noexcept{
    const auto plane = TextGlyphVisibility::AtlasRectangle(placed, atlas, fontSize, {});
    if(!plane)
        return MakeUnexpected(Failure{});
    if(plane->width <= 0.0f || plane->height <= 0.0f)
        return VectorDoubleSet(plane->x, plane->y, plane->width, plane->height);
    const FontAtlasGlyph& glyph = *atlas.glyph(placed.glyphId);
    const SdfAtlasPage& page = *atlas.page(glyph.group);
    const SdfAtlasPageBinding& binding = page.binding();
    const auto& pixels = page.pixels();
    const SIMDVectorDouble texelSize = VectorDoubleDivide(VectorDoubleSet(plane->width, plane->height, 1.0, 1.0), VectorDoubleSet(glyph.width, glyph.height, 1.0, 1.0));
    const SIMDVectorDouble physicalStep = VectorDoubleDivide(VectorDoubleReplicate(1.0), VectorDoubleMultiply(texelSize, VectorDoubleSet(display.pixelScaleX, display.pixelScaleY, 1.0, 1.0)));
    const f64 physicalStepX = VectorDoubleGetX(physicalStep);
    const f64 physicalStepY = VectorDoubleGetY(physicalStep);
    if(!IsFinite(physicalStepX) || !IsFinite(physicalStepY))
        return MakeUnexpected(Failure{});
    const auto sample = [&](const u32 x, const u32 y)noexcept -> i32{
        return pixels[(static_cast<usize>(y) * binding.width + x) * binding.channelCount + glyph.channel];
    };
    // A derivative lane can sample one physical pixel beyond the quad; one additional texel covers both bilinear neighbors there.
    const SIMDVectorDouble haloLimit = VectorDoubleSet(binding.width, binding.height, 0.0, 0.0);
    const SIMDVectorDouble haloCandidate = VectorDoubleAdd(VectorDoubleCeiling(physicalStep), VectorDoubleReplicate(1.0));
    const SIMDVectorDouble halo = VectorDoubleSelect(haloCandidate, haloLimit, VectorDoubleLess(haloLimit, haloCandidate));
    const u32 haloX = static_cast<u32>(VectorDoubleGetX(halo));
    const u32 haloY = static_cast<u32>(VectorDoubleGetY(halo));
    const u32 firstX = glyph.x > haloX ? glyph.x - haloX : 0u;
    const u32 firstY = glyph.y > haloY ? glyph.y - haloY : 0u;
    const u32 lastX = Min(binding.width - 1u, glyph.x + glyph.width + haloX);
    const u32 lastY = Min(binding.height - 1u, glyph.y + glyph.height + haloY);
    i32 gradientX = 0;
    i32 gradientY = 0;
    for(u32 y = firstY; y <= lastY; ++y){
        for(u32 x = firstX; x <= lastX; ++x){
            const i32 value = sample(x, y);
            if(x != lastX)
                gradientX = Max(gradientX, Abs(sample(x + 1u, y) - value));
            if(y != lastY)
                gradientY = Max(gradientY, Abs(sample(x, y + 1u) - value));
        }
    }
    // Bilinear directional slopes are bounded by adjacent-texel differences, including helper lanes outside the visible quad.
    // The shader decodes distance = (encoded - 128) * spread / 128, then uses max(fwidth(distance), 0.001).
    const SIMDVectorDouble horizontalStep = VectorDoubleSelect(VectorDoubleZero(), physicalStep, s_DoubleSIMDMaskX);
    const SIMDVectorDouble verticalStep = VectorDoubleSwizzle<1, 2, 3, 3>(physicalStep);
    const SIMDVectorDouble verticalDerivative = VectorDoubleMultiply(VectorDoubleSet(gradientY, 0.0, 0.0, 0.0), verticalStep);
    const SIMDVectorDouble derivative = VectorDoubleMultiplyAddExpression(VectorDoubleSet(gradientX, 0.0, 0.0, 0.0), horizontalStep, verticalDerivative);
    const SIMDVectorDouble minimumDerivative = VectorDoubleDivide(VectorDoubleReplicate(0.001 * 128.0), VectorDoubleReplicate(binding.spreadPixels));
    const SIMDVectorDouble halfWidth = VectorDoubleMultiply(VectorDoubleReplicate(0.5), VectorDoubleMax(derivative, minimumDerivative));
    const SIMDVectorDouble glyphSize = VectorDoubleSet(glyph.width, glyph.height, 0.0, 0.0);
    const SIMDVectorDouble halfTexel = VectorDoubleSet(0.5, 0.5, 0.0, 0.0);
    SIMDVectorDouble supportMinimum = glyphSize;
    SIMDVectorDouble supportMaximum = VectorDoubleZero();
    bool visible = false;
    for(u32 y = 0u; y <= glyph.height; ++y){
        const u32 y0 = glyph.y + y == 0u ? 0u : glyph.y + y - 1u;
        const u32 y1 = Min(binding.height - 1u, glyph.y + y);
        for(u32 x = 0u; x <= glyph.width; ++x){
            const u32 x0 = glyph.x + x == 0u ? 0u : glyph.x + x - 1u;
            const u32 x1 = Min(binding.width - 1u, glyph.x + x);
            const i32 maximum = Max(Max(sample(x0, y0), sample(x1, y0)), Max(sample(x0, y1), sample(x1, y1)));
            if(VectorDoubleGetIntX(VectorDoubleLess(VectorDoubleAdd(VectorDoubleReplicate(static_cast<f64>(maximum)), halfWidth), VectorDoubleReplicate(128.0))) != 0u)
                continue;
            // Texel centers lie at n + 0.5; retain exactly the candidate cell's intersection with the drawn plane.
            const SIMDVectorDouble position = VectorDoubleSet(x, y, 0.0, 0.0);
            const SIMDVectorDouble first = VectorDoubleMax(VectorDoubleZero(), VectorDoubleSubtract(position, halfTexel));
            const SIMDVectorDouble last = VectorDoubleMin(glyphSize, VectorDoubleAdd(position, halfTexel));
            supportMinimum = VectorDoubleSelect(first, supportMinimum, VectorDoubleLess(supportMinimum, first));
            supportMaximum = VectorDoubleSelect(last, supportMaximum, VectorDoubleGreater(supportMaximum, last));
            visible = true;
        }
    }
    if(!visible)
        return VectorDoubleZero();
    const SIMDVectorDouble origin = VectorDoubleSet(plane->x, plane->y, 0.0, 0.0);
    const SIMDVectorDouble scale = VectorDoubleSelect(VectorDoubleZero(), texelSize, s_DoubleSIMDMaskXY);
    const SIMDVectorDouble minimum = VectorDoubleMultiplyAddExpression(supportMinimum, scale, origin);
    const SIMDVectorDouble maximum = VectorDoubleMultiplyAddExpression(supportMaximum, scale, origin);
    return MeasuredRectangle(minimum, maximum);
}

struct PreparedGlyph{
    Rect rectangle;
    Rect uv;
    const SharedSdfAtlasPage* sdfPage = nullptr;
    u32 pageIndex = 0u;
    u32 channel = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Rect> TextService::measure(const TextLayout& layout, const DisplayMetrics& display){
    using namespace __hidden_ui_text_paint;
    if(layout.lines().empty() || !IsFinite(layout.fontSize()) || layout.fontSize() < 1.0f / 64.0f || layout.fontSize() > 2048.0f || !IsFinite(display.pixelScaleX) || !IsFinite(display.pixelScaleY) || display.pixelScaleX <= 0.0f || display.pixelScaleY <= 0.0f)
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle physicalSizes = VectorMultiply(VectorReplicate(layout.fontSize()), VectorMax(VectorSet(display.pixelScaleX, display.pixelScaleY, 1.0f, 1.0f), VectorSet(display.pixelScaleY, display.pixelScaleX, 1.0f, 1.0f)));
    const f32 physicalSize = VectorGetX(physicalSizes);
    const f32 rasterPpem = VectorGetX(VectorCeiling(physicalSizes));
    if(!IsFinite(rasterPpem) || rasterPpem < 1.0f || rasterPpem > 4096.0f)
        return MakeUnexpected(Failure{});
    const f32 rasterScale = VectorGetX(VectorDivide(VectorReplicate(rasterPpem), VectorReplicate(layout.fontSize())));
    const u32 pixelSize = static_cast<u32>(rasterPpem);
    const Point logical = layout.measure();
    const Rect ink = layout.inkBounds();
    const SIMDVectorDouble inkOrigin = VectorDoubleSet(ink.x, ink.y, 0.0, 0.0);
    const SIMDVectorDouble inkEnd = VectorDoubleAdd(inkOrigin, VectorDoubleSet(ink.width, ink.height, 0.0, 0.0));
    const SIMDVectorDouble logicalSize = VectorDoubleSet(logical.x, logical.y, 0.0, 0.0);
    SIMDVectorDouble minimum = VectorDoubleSelect(inkOrigin, VectorDoubleZero(), VectorDoubleLess(VectorDoubleZero(), inkOrigin));
    SIMDVectorDouble maximum = VectorDoubleSelect(inkEnd, logicalSize, VectorDoubleGreater(logicalSize, inkEnd));
    for(const PlacedGlyph& glyph : layout.glyphs()){
        SIMDVectorDouble first;
        SIMDVectorDouble last;
        if(const BakedFontAtlas* atlas = TextGlyphVisibility::SelectAtlas(glyph, physicalSize)){
            const auto support = LoadSdfSupport(glyph, *atlas, layout.fontSize(), display);
            if(!support)
                return MakeUnexpected(Failure{});
            if(VectorDoubleGetZ(*support) <= 0.0 || VectorDoubleGetW(*support) <= 0.0)
                continue;
            first = VectorDoubleSelect(VectorDoubleZero(), *support, s_DoubleSIMDMaskXY);
            last = VectorDoubleAdd(first, VectorDoubleSwizzle<2, 3, 2, 3>(*support));
        }
        else{
            if(!glyph.face || !glyph.face->valid())
                return MakeUnexpected(Failure{});
            const AtlasGlyph* cached = m_atlas->find(glyph.face, glyph.glyphId, pixelSize);
            const Expected<Rect> raster = cached
                ? Expected<Rect>(Rect{ static_cast<f32>(cached->bearingX), -static_cast<f32>(cached->bearingY), cached->pixels.width, cached->pixels.height })
                : glyph.face->rasterBounds(glyph.glyphId, pixelSize);
            if(!raster)
                return MakeUnexpected(Failure{});
            if(raster->width <= 0.0f || raster->height <= 0.0f)
                continue;
            const SIMDVectorDouble scale = VectorDoubleReplicate(static_cast<f64>(rasterScale));
            const SIMDVectorDouble origin = VectorDoubleSet(glyph.position.x, glyph.position.y, 0.0, 0.0);
            const SIMDVectorDouble pixelOrigin = VectorDoubleSet(raster->x, raster->y, 0.0, 0.0);
            const SIMDVectorDouble pixelEnd = VectorDoubleAdd(pixelOrigin, VectorDoubleSet(raster->width, raster->height, 0.0, 0.0));
            // CoverageRectangle snaps the bitmap origin to the nearest physical pixel; every translation phase differs by at most half a pixel on each axis.
            const SIMDVectorDouble snap = VectorDoubleDivide(VectorDoubleSet(0.5, 0.5, 0.0, 0.0), VectorDoubleSet(display.pixelScaleX, display.pixelScaleY, 1.0, 1.0));
            first = VectorDoubleSubtract(VectorDoubleAdd(origin, VectorDoubleDivide(pixelOrigin, scale)), snap);
            last = VectorDoubleAdd(VectorDoubleAdd(origin, VectorDoubleDivide(pixelEnd, scale)), snap);
        }
        minimum = VectorDoubleSelect(minimum, first, VectorDoubleLess(first, minimum));
        maximum = VectorDoubleSelect(maximum, last, VectorDoubleGreater(last, maximum));
    }
    const auto rectangle = MeasuredRectangle(minimum, maximum);
    if(!rectangle)
        return MakeUnexpected(Failure{});
    return Rect{ static_cast<f32>(VectorDoubleGetX(*rectangle)), static_cast<f32>(VectorDoubleGetY(*rectangle)), static_cast<f32>(VectorDoubleGetZ(*rectangle)), static_cast<f32>(VectorDoubleGetW(*rectangle)) };
}

bool TextService::paint(PaintBuilder& paint, const TextLayout& layout, Point topLeft, const Color& color){
    const DisplayMetrics& metrics = paint.displayMetrics();
    const SIMDVectorSingle pixelScales = VectorMax(VectorSet(metrics.pixelScaleX, metrics.pixelScaleY, 1.0f, 1.0f), VectorSet(metrics.pixelScaleY, metrics.pixelScaleX, 1.0f, 1.0f));
    const f32 physicalSize = VectorGetX(VectorMultiply(VectorReplicate(layout.fontSize()), pixelScales));
    const f32 rasterPpem = VectorGetX(VectorCeiling(VectorReplicate(physicalSize)));
    if(
        layout.lines().empty() || !IsFinite(layout.fontSize())
        || layout.fontSize() < 1.0f / 64.0f || layout.fontSize() > 2048.0f
        || !IsFinite(topLeft.x) || !IsFinite(topLeft.y) || !IsFinite(rasterPpem)
        || rasterPpem < 1.0f || rasterPpem > 4096.0f || !IsFinite(color.r) || !IsFinite(color.g) || !IsFinite(color.b)
        || !IsFinite(color.a) || color.a < 0.0f || color.a > 1.0f
    )
        return false;
    const Rect clip = paint.currentClip();
    const TextGlyphIntersection::Enum clipStatus = TextGlyphVisibility::Intersect(clip, clip);
    if(clipStatus == TextGlyphIntersection::Invalid)
        return false;
    if(clipStatus == TextGlyphIntersection::Invisible || color.a == 0.0f)
        return true;
    const u32 pixelSize = static_cast<u32>(rasterPpem);
    const f32 rasterScale = VectorGetX(VectorDivide(VectorReplicate(rasterPpem), VectorReplicate(layout.fontSize())));
    const Point deviceScale{ metrics.pixelScaleX, metrics.pixelScaleY };
    Core::Alloc::ScratchArena scratchArena(Name("impl/ecs_ui/toolkit/text/paint_candidates"));
    Vector<usize, Core::Alloc::ScratchArena> candidates(scratchArena);
    candidates.reserve(layout.glyphs().size());
    // Validate every conservative candidate before raster preparation mutates the coverage cache.
    for(usize index = 0u; index < layout.glyphs().size(); ++index){
        const PlacedGlyph& glyph = layout.glyphs()[index];
        const BakedFontAtlas* atlas = TextGlyphVisibility::SelectAtlas(glyph, physicalSize);
        const TextGlyphIntersection::Enum status = TextGlyphVisibility::Candidate(glyph, atlas, layout.fontSize(), rasterPpem, topLeft, clip, deviceScale);
        if(status == TextGlyphIntersection::Invalid)
            return false;
        if(status == TextGlyphIntersection::Visible)
            candidates.push_back(index);
    }
    if(candidates.empty())
        return true;
    for(const usize index : candidates){
        const PlacedGlyph& glyph = layout.glyphs()[index];
        if(!TextGlyphVisibility::SelectAtlas(glyph, physicalSize) && !m_atlas->prepare(glyph.face, glyph.glyphId, pixelSize))
            return false;
    }
    // Exact rectangles remove conservative overinclusion before image admission; painter order remains unchanged.
    Vector<__hidden_ui_text_paint::PreparedGlyph, Core::Alloc::ScratchArena> prepared(scratchArena);
    prepared.reserve(candidates.size());
    for(const usize index : candidates){
        const PlacedGlyph& glyph = layout.glyphs()[index];
        __hidden_ui_text_paint::PreparedGlyph item;
        if(const BakedFontAtlas* atlas = TextGlyphVisibility::SelectAtlas(glyph, physicalSize)){
            const auto rectangle = TextGlyphVisibility::AtlasRectangle(glyph, *atlas, layout.fontSize(), topLeft);
            if(!rectangle)
                return false;
            item.rectangle = *rectangle;
            if(item.rectangle.width > 0.0f && item.rectangle.height > 0.0f){
                const FontAtlasGlyph& record = *atlas->glyph(glyph.glyphId);
                item.sdfPage = &atlas->page(record.group);
                item.channel = record.channel;
                const SdfAtlasPageBinding& binding = (*item.sdfPage)->binding();
                const SIMDVectorSingle coordinates = VectorSet(static_cast<f32>(record.x), static_cast<f32>(record.y), static_cast<f32>(record.width), static_cast<f32>(record.height));
                const SIMDVectorSingle atlasSize = VectorSet(static_cast<f32>(binding.width), static_cast<f32>(binding.height), static_cast<f32>(binding.width), static_cast<f32>(binding.height));
                const SIMDVectorSingle normalized = VectorDivide(coordinates, atlasSize);
                item.uv = { VectorGetX(normalized), VectorGetY(normalized), VectorGetZ(normalized), VectorGetW(normalized) };
            }
        }
        else{
            const AtlasGlyph* record = m_atlas->find(glyph.face, glyph.glyphId, pixelSize);
            if(!record){
                NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Prepared UI glyph must be present"));
                return false;
            }
            const auto rectangle = TextGlyphVisibility::CoverageRectangle(glyph, *record, rasterScale, topLeft, deviceScale);
            if(!rectangle)
                return false;
            item.rectangle = *rectangle;
            item.pageIndex = record->pageIndex;
            const SIMDVectorSingle coordinates = VectorSet(record->pixels.x, record->pixels.y, record->pixels.width, record->pixels.height);
            const SIMDVectorSingle normalized = VectorDivide(coordinates, VectorReplicate(static_cast<f32>(s_GlyphAtlasPageExtent)));
            item.uv = { VectorGetX(normalized), VectorGetY(normalized), VectorGetZ(normalized), VectorGetW(normalized) };
        }
        const TextGlyphIntersection::Enum status = TextGlyphVisibility::Intersect(item.rectangle, clip);
        if(status == TextGlyphIntersection::Invalid)
            return false;
        if(status == TextGlyphIntersection::Visible)
            prepared.push_back(item);
    }
    if(prepared.empty())
        return true;
    m_pages.clear();
    m_sdfPages.clear();
    for(auto& glyph : prepared){
        if(glyph.sdfPage){
            const SharedSdfAtlasPage& page = *glyph.sdfPage;
            const auto existing = FindIf(m_sdfPages.begin(), m_sdfPages.end(), [&page](const SharedSdfAtlasPage& item)noexcept{
                return item.get() == page.get();
            }
            );
            glyph.pageIndex = static_cast<u32>(existing - m_sdfPages.begin());
            if(existing == m_sdfPages.end())
                m_sdfPages.push_back(page);
        }
        else{
            auto pageResult = m_atlas->page(glyph.pageIndex);
            if(!pageResult)
                return false;
            SharedGlyphPage page = Move(*pageResult);
            const auto existing = FindIf(m_pages.begin(), m_pages.end(), [&page](const SharedGlyphPage& item)noexcept{
                return item.get() == page.get();
            }
            );
            glyph.pageIndex = static_cast<u32>(existing - m_pages.begin());
            if(existing == m_pages.end())
                m_pages.push_back(Move(page));
        }
    }
    if(!paint.prepareImages(m_pages.data(), m_pages.size(), m_sdfPages.data(), m_sdfPages.size()))
        return false;
    for(const auto& glyph : prepared){
        const bool drawn = glyph.sdfPage ? paint.drawSdfGlyph(m_sdfPages[glyph.pageIndex], glyph.channel, glyph.rectangle, glyph.uv, color) : paint.drawGlyph(m_pages[glyph.pageIndex], glyph.rectangle, glyph.uv, color);
        if(!drawn){
            NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Admitted UI glyph draw must succeed"));
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

