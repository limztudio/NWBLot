// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint.h"
#include "rect_math.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_paint{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_QuadVertexCount = 4u;
inline constexpr u32 s_QuadIndexCount = 6u;
inline constexpr u32 s_NineSliceAxisCells = 3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool HasArea(const Rect& rectangle)noexcept{
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y) && IsFinite(rectangle.width) && IsFinite(rectangle.height) && rectangle.width > 0.0f && rectangle.height > 0.0f
        && VectorIsFinite(VectorAdd(VectorSet(rectangle.x, rectangle.y, 0.0f, 0.0f), VectorSet(rectangle.width, rectangle.height, 0.0f, 0.0f)), VectorComponentMask::s_XY)
    ;
}

bool EqualClip(const Rect& lhs, const Rect& rhs)noexcept{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width && lhs.height == rhs.height;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DrawSnapshot::DrawSnapshot(Core::Alloc::GlobalArena& arena)noexcept
    : m_vertices(arena)
    , m_indices(arena)
    , m_commands(arena)
    , m_glyphPages(arena)
    , m_sdfPages(arena)
    , m_textureImages(arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


PaintBuilder::PaintBuilder(Core::Alloc::GlobalArena& arena)noexcept
    : m_snapshot(arena)
    , m_regions(arena)
    , m_clips(arena)
{}

void PaintBuilder::begin(const DisplayMetrics& metrics, const u64 generation, const u64 skinGeneration, const Core::Assets::AssetRef<UiSkin>& skinRef, const UiSkin& skin){
    NWB_ASSERT(skinRef.valid() && skinRef.name() == skin.virtualPath());
    NWB_ASSERT(skin.atlasWidth() > 0u && skin.atlasHeight() > 0u && skin.referenceDensity() > 0.0f);
    NWB_ASSERT(IsFinite(skin.referenceDensity()));
    NWB_ASSERT(IsFinite(metrics.logicalWidth) && metrics.logicalWidth >= 0.0f);
    NWB_ASSERT(IsFinite(metrics.logicalHeight) && metrics.logicalHeight >= 0.0f);
    NWB_ASSERT(IsFinite(metrics.pixelScaleX) && metrics.pixelScaleX > 0.0f);
    NWB_ASSERT(IsFinite(metrics.pixelScaleY) && metrics.pixelScaleY > 0.0f);
    m_snapshot.m_generation = generation;
    m_snapshot.m_displayMetrics = metrics;
    m_snapshot.m_skinBinding = { skinRef, skin.texture(), skinGeneration, skin.atlasWidth(), skin.atlasHeight(), skin.referenceDensity() };
    m_snapshot.m_vertices.clear();
    m_snapshot.m_indices.clear();
    m_snapshot.m_commands.clear();
    m_snapshot.m_glyphPages.clear();
    m_snapshot.m_sdfPages.clear();
    m_snapshot.m_textureImages.clear();
    m_regions.assign(skin.regions().begin(), skin.regions().end());
    m_clips.clear();
    m_clips.push_back({ 0.0f, 0.0f, metrics.logicalWidth, metrics.logicalHeight });
    m_overlayDepth = 0u;
    m_layer = 0u;
    m_commandsOrdered = true;
    m_recording = true;
}

void PaintBuilder::reserve(const usize quadCount, const usize clipDepth){
    if(quadCount > Limit<u32>::s_Max / __hidden_ui_paint::s_QuadIndexCount || clipDepth == Limit<usize>::s_Max){
        NWB_ASSERT(false);
        return;
    }
    m_snapshot.m_vertices.reserve(quadCount * __hidden_ui_paint::s_QuadVertexCount);
    m_snapshot.m_indices.reserve(quadCount * __hidden_ui_paint::s_QuadIndexCount);
    m_snapshot.m_commands.reserve(quadCount);
    m_clips.reserve(clipDepth + 1u);
}

void PaintBuilder::pushClip(const Rect& clip){
    NWB_ASSERT(m_recording);
    const Rect& parent = m_clips.back();
    Rect next;
    if(__hidden_ui_paint::HasArea(parent) && __hidden_ui_paint::HasArea(clip)){
        const SIMDVectorSingle intersection = IntersectRectValue(VectorSet(parent.x, parent.y, parent.width, parent.height), VectorSet(clip.x, clip.y, clip.width, clip.height));
        next = { VectorGetX(intersection), VectorGetY(intersection), VectorGetZ(intersection), VectorGetW(intersection) };
    }
    m_clips.push_back(next);
}

bool PaintBuilder::popClip()noexcept{
    NWB_ASSERT(m_recording);
    if(m_clips.size() <= 1u || (m_overlayDepth != 0u && m_clips.size() <= m_overlays[m_overlayDepth - 1u].clipDepth + 1u))
        return false;
    m_clips.pop_back();
    return true;
}

void PaintBuilder::fillRect(const Rect& rectangle, const Color& color){
    emitQuad(rectangle, {}, color, PaintMaterial::Solid);
}

bool PaintBuilder::drawRegion(const Name& regionName, const Rect& rectangle, const Color& tint){
    NWB_ASSERT(m_recording);
    const auto region = FindIf(m_regions.begin(), m_regions.end(), [&regionName](const UiSkinRegion& value)noexcept{
        return value.name == regionName;
    }
    );
    if(region == m_regions.end())
        return false;
    if(region->drawMode == UiSkinDrawMode::NineSlice)
        emitNineSlice(*region, rectangle, tint);
    else{
        const f32 atlasWidth = static_cast<f32>(m_snapshot.m_skinBinding.atlasWidth);
        const f32 atlasHeight = static_cast<f32>(m_snapshot.m_skinBinding.atlasHeight);
        const auto& pixels = region->rectangle;
        const SIMDVectorSingle normalized = VectorDivide(VectorSet(static_cast<f32>(pixels.x), static_cast<f32>(pixels.y), static_cast<f32>(pixels.width), static_cast<f32>(pixels.height)), VectorSet(atlasWidth, atlasHeight, atlasWidth, atlasHeight));
        const Rect uv{ VectorGetX(normalized), VectorGetY(normalized), VectorGetZ(normalized), VectorGetW(normalized) };
        emitQuad(rectangle, uv, tint, PaintMaterial::Skin);
    }
    return true;
}

DrawSnapshot PaintBuilder::freeze(){
    NWB_ASSERT(m_recording && m_clips.size() == 1u && m_overlayDepth == 0u);
    if(!m_commandsOrdered){
        Sort(m_snapshot.m_commands.begin(), m_snapshot.m_commands.end(), [](const DrawCommand& lhs, const DrawCommand& rhs)noexcept{
            return lhs.layer != rhs.layer ? lhs.layer < rhs.layer : lhs.firstIndex < rhs.firstIndex;
        }
        );
    }
    m_recording = false;
    m_clips.clear();
    return Move(m_snapshot);
}

Rect PaintBuilder::currentClip()const noexcept{
    NWB_ASSERT(m_recording);
    return m_clips.back();
}

void PaintBuilder::emitQuad(const Rect& rectangle, const Rect& uv, const Color& color, const PaintMaterial::Enum material, const u32 glyphPageIndex, const u32 sdfPageIndex, const u32 sdfChannel, const u32 textureImageIndex){
    NWB_ASSERT(m_recording);
    NWB_ASSERT(IsFinite(color.r) && IsFinite(color.g) && IsFinite(color.b) && IsFinite(color.a));
    NWB_ASSERT(color.a >= 0.0f && color.a <= 1.0f);
    const Rect clip = m_clips.back();
    if(!__hidden_ui_paint::HasArea(rectangle) || !__hidden_ui_paint::HasArea(clip))
        return;
    const SIMDVectorSingle intersection = IntersectRectValue(VectorSet(rectangle.x, rectangle.y, rectangle.width, rectangle.height), VectorSet(clip.x, clip.y, clip.width, clip.height));
    const Rect visible{ VectorGetX(intersection), VectorGetY(intersection), VectorGetZ(intersection), VectorGetW(intersection) };
    if(!__hidden_ui_paint::HasArea(visible) || color.a <= 0.0f)
        return;
    if(m_snapshot.m_vertices.size() > Limit<u32>::s_Max - __hidden_ui_paint::s_QuadVertexCount || m_snapshot.m_indices.size() > Limit<u32>::s_Max - __hidden_ui_paint::s_QuadIndexCount){
        NWB_ASSERT(false);
        return;
    }
    const SIMDVectorSingle visibleOrigin = VectorSet(visible.x, visible.y, visible.x, visible.y);
    const SIMDVectorSingle visibleEnd = VectorAdd(visibleOrigin, VectorSet(visible.width, visible.height, visible.width, visible.height));
    const SIMDVectorSingle coordinates = VectorPermute<0, 1, 6, 7>(visibleOrigin, visibleEnd);
    const SIMDVectorSingle delta = VectorSubtract(coordinates, VectorSet(rectangle.x, rectangle.y, rectangle.x, rectangle.y));
    const SIMDVectorSingle fraction = VectorDivide(delta, VectorSet(rectangle.width, rectangle.height, rectangle.width, rectangle.height));
    const SIMDVectorSingle textureScale = VectorSet(uv.width, uv.height, uv.width, uv.height);
    const SIMDVectorSingle textureOrigin = VectorSet(uv.x, uv.y, uv.x, uv.y);
    const SIMDVectorSingle textureCoordinates = VectorMultiplyAddExpression(fraction, textureScale, textureOrigin);
    const f32 left = VectorGetX(textureCoordinates);
    const f32 top = VectorGetY(textureCoordinates);
    const f32 right = VectorGetZ(textureCoordinates);
    const f32 bottom = VectorGetW(textureCoordinates);
    const SIMDVectorSingle rgb = VectorScale(VectorSet(color.r, color.g, color.b, color.b), color.a);
    const Color premultiplied{ VectorGetX(rgb), VectorGetY(rgb), VectorGetZ(rgb), color.a };
    const u32 vertexBase = static_cast<u32>(m_snapshot.m_vertices.size());
    const u32 firstIndex = static_cast<u32>(m_snapshot.m_indices.size());
    const f32 visibleRight = VectorGetX(visibleEnd);
    const f32 visibleBottom = VectorGetY(visibleEnd);
    m_snapshot.m_vertices.push_back({ { visible.x, visible.y }, { left, top }, premultiplied });
    m_snapshot.m_vertices.push_back({ { visibleRight, visible.y }, { right, top }, premultiplied });
    m_snapshot.m_vertices.push_back({ { visibleRight, visibleBottom }, { right, bottom }, premultiplied });
    m_snapshot.m_vertices.push_back({ { visible.x, visibleBottom }, { left, bottom }, premultiplied });
    const u32 indices[]{ vertexBase, vertexBase + 1u, vertexBase + 2u, vertexBase, vertexBase + 2u, vertexBase + 3u };
    m_snapshot.m_indices.insert(m_snapshot.m_indices.end(), indices, indices + __hidden_ui_paint::s_QuadIndexCount);
    if(!m_snapshot.m_commands.empty()){
        auto& previous = m_snapshot.m_commands.back();
        if(
            previous.layer == m_layer && previous.material == material && previous.glyphPageIndex == glyphPageIndex
            && previous.sdfPageIndex == sdfPageIndex && previous.sdfChannel == sdfChannel
            && previous.textureImageIndex == textureImageIndex
            && __hidden_ui_paint::EqualClip(previous.clip, clip)
        ){
            previous.indexCount += __hidden_ui_paint::s_QuadIndexCount;
            return;
        }
        // Distinct commands already have increasing firstIndex; only a layer descent needs ordering at freeze.
        if(previous.layer > m_layer)
            m_commandsOrdered = false;
    }
    m_snapshot.m_commands.push_back({ firstIndex, __hidden_ui_paint::s_QuadIndexCount, clip, material, glyphPageIndex, sdfPageIndex, sdfChannel, m_layer, textureImageIndex });
}

void PaintBuilder::emitNineSlice(const UiSkinRegion& region, const Rect& rectangle, const Color& tint){
    if(!__hidden_ui_paint::HasArea(rectangle))
        return;
    const auto& binding = m_snapshot.m_skinBinding;
    const auto& pixels = region.rectangle;
    const auto& borders = region.sliceInsets;
    const SIMDVectorSingle borderSize = VectorDivide(VectorSet(static_cast<f32>(borders.left), static_cast<f32>(borders.right), static_cast<f32>(borders.top), static_cast<f32>(borders.bottom)), VectorReplicate(binding.referenceDensity));
    const SIMDVectorSingle origin = VectorSet(rectangle.x, rectangle.y, rectangle.x, rectangle.y);
    const SIMDVectorSingle size = VectorSet(rectangle.width, rectangle.height, rectangle.width, rectangle.height);
    const SIMDVectorSingle firstBorder = VectorSwizzle<0, 2, 0, 2>(borderSize);
    const SIMDVectorSingle lastBorder = VectorSwizzle<1, 3, 1, 3>(borderSize);
    const SIMDVectorSingle borderExtent = VectorAdd(firstBorder, lastBorder);
    const SIMDVectorSingle shrink = VectorLess(size, borderExtent);
    // Nonshrinking axes must not evaluate the conditional ratio, including a zero-width border pair.
    const SIMDVectorSingle numerator = VectorSelect(VectorZero(), firstBorder, shrink);
    const SIMDVectorSingle denominator = VectorSelect(VectorReplicate(1.0f), borderExtent, shrink);
    const SIMDVectorSingle scaledBorder = VectorMultiply(VectorSelect(VectorZero(), size, shrink), VectorDivide(numerator, denominator));
    const SIMDVectorSingle first = VectorAdd(origin, VectorSelect(firstBorder, scaledBorder, shrink));
    const SIMDVectorSingle end = VectorAdd(origin, size);
    const SIMDVectorSingle second = VectorSelect(VectorSubtract(end, VectorSelect(lastBorder, VectorZero(), shrink)), first, shrink);
    const f32 x[]{ rectangle.x, VectorGetX(first), VectorGetX(second), VectorGetX(end) };
    const f32 y[]{ rectangle.y, VectorGetY(first), VectorGetY(second), VectorGetY(end) };
    const f32 u[]{ static_cast<f32>(pixels.x), static_cast<f32>(pixels.x + borders.left), static_cast<f32>(pixels.x + pixels.width - borders.right), static_cast<f32>(pixels.x + pixels.width) };
    const f32 v[]{ static_cast<f32>(pixels.y), static_cast<f32>(pixels.y + borders.top), static_cast<f32>(pixels.y + pixels.height - borders.bottom), static_cast<f32>(pixels.y + pixels.height) };
    const f32 atlasWidth = static_cast<f32>(binding.atlasWidth);
    const f32 atlasHeight = static_cast<f32>(binding.atlasHeight);
    for(u32 row = 0u; row < __hidden_ui_paint::s_NineSliceAxisCells; ++row){
        for(u32 column = 0u; column < __hidden_ui_paint::s_NineSliceAxisCells; ++column){
            const SIMDVectorSingle targetSize = VectorSubtract(VectorSet(x[column + 1u], y[row + 1u], x[column + 1u], y[row + 1u]), VectorSet(x[column], y[row], x[column], y[row]));
            const Rect target{ x[column], y[row], VectorGetX(targetSize), VectorGetY(targetSize) };
            const SIMDVectorSingle textureSize = VectorSubtract(VectorSet(u[column + 1u], v[row + 1u], u[column + 1u], v[row + 1u]), VectorSet(u[column], v[row], u[column], v[row]));
            const SIMDVectorSingle textureCoordinates = VectorPermute<0, 1, 4, 5>(VectorSet(u[column], v[row], 0.0f, 0.0f), textureSize);
            const SIMDVectorSingle normalized = VectorDivide(textureCoordinates, VectorSet(atlasWidth, atlasHeight, atlasWidth, atlasHeight));
            const Rect uv{ VectorGetX(normalized), VectorGetY(normalized), VectorGetZ(normalized), VectorGetW(normalized) };
            emitQuad(target, uv, tint, PaintMaterial::Skin);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

