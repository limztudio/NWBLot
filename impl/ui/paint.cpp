// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_paint{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool HasArea(const Rect& rectangle){
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y) && IsFinite(rectangle.width) && IsFinite(rectangle.height)
        && rectangle.width > 0.0f && rectangle.height > 0.0f
        && IsFinite(rectangle.x + rectangle.width) && IsFinite(rectangle.y + rectangle.height)
    ;
}

Rect Intersect(const Rect& lhs, const Rect& rhs){
    if(!HasArea(lhs) || !HasArea(rhs))
        return {};
    const f32 left = Max(lhs.x, rhs.x);
    const f32 top = Max(lhs.y, rhs.y);
    const f32 right = Min(lhs.x + lhs.width, rhs.x + rhs.width);
    const f32 bottom = Min(lhs.y + lhs.height, rhs.y + rhs.height);
    return { left, top, Max(0.0f, right - left), Max(0.0f, bottom - top) };
}

bool EqualClip(const Rect& lhs, const Rect& rhs){
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width && lhs.height == rhs.height;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DrawSnapshot::DrawSnapshot(Core::Alloc::GlobalArena& arena)
    : m_vertices(arena)
    , m_indices(arena)
    , m_commands(arena)
    , m_glyphPages(arena)
    , m_sdfPages(arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


PaintBuilder::PaintBuilder(Core::Alloc::GlobalArena& arena)
    : m_snapshot(arena)
    , m_regions(arena)
    , m_clips(arena)
{}

void PaintBuilder::begin(
    const DisplayMetrics& metrics,
    const u64 generation,
    const u64 skinGeneration,
    const Core::Assets::AssetRef<UiSkin>& skinRef,
    const UiSkin& skin){
    NWB_ASSERT(skinRef.valid() && skinRef.name() == skin.virtualPath());
    NWB_ASSERT(skin.atlasWidth() > 0u && skin.atlasHeight() > 0u && skin.referenceDensity() > 0.0f);
    NWB_ASSERT(IsFinite(skin.referenceDensity()));
    NWB_ASSERT(IsFinite(metrics.logicalWidth) && metrics.logicalWidth >= 0.0f);
    NWB_ASSERT(IsFinite(metrics.logicalHeight) && metrics.logicalHeight >= 0.0f);
    NWB_ASSERT(IsFinite(metrics.pixelScaleX) && metrics.pixelScaleX > 0.0f);
    NWB_ASSERT(IsFinite(metrics.pixelScaleY) && metrics.pixelScaleY > 0.0f);
    m_snapshot.m_generation = generation;
    m_snapshot.m_displayMetrics = metrics;
    m_snapshot.m_skinBinding = {
        skinRef,
        skin.texture(),
        skinGeneration,
        skin.atlasWidth(),
        skin.atlasHeight(),
        skin.referenceDensity()
    };
    m_snapshot.m_vertices.clear();
    m_snapshot.m_indices.clear();
    m_snapshot.m_commands.clear();
    m_snapshot.m_glyphPages.clear();
    m_snapshot.m_sdfPages.clear();
    m_regions.assign(skin.regions().begin(), skin.regions().end());
    m_clips.clear();
    m_clips.push_back({ 0.0f, 0.0f, metrics.logicalWidth, metrics.logicalHeight });
    m_recording = true;
}

void PaintBuilder::reserve(const usize quadCount, const usize clipDepth){
    if(quadCount > Limit<u32>::s_Max / 6u || clipDepth == Limit<usize>::s_Max){
        NWB_ASSERT(false);
        return;
    }
    m_snapshot.m_vertices.reserve(quadCount * 4u);
    m_snapshot.m_indices.reserve(quadCount * 6u);
    m_snapshot.m_commands.reserve(quadCount);
    m_clips.reserve(clipDepth + 1u);
}

void PaintBuilder::pushClip(const Rect& clip){
    NWB_ASSERT(m_recording);
    m_clips.push_back(__hidden_ui_paint::Intersect(m_clips.back(), clip));
}

bool PaintBuilder::popClip(){
    NWB_ASSERT(m_recording);
    if(m_clips.size() <= 1u)
        return false;
    m_clips.pop_back();
    return true;
}

void PaintBuilder::fillRect(const Rect& rectangle, const Color& color){
    emitQuad(rectangle, {}, color, PaintMaterial::Solid);
}

bool PaintBuilder::drawRegion(const Name& regionName, const Rect& rectangle, const Color& tint){
    NWB_ASSERT(m_recording);
    const auto region = FindIf(m_regions.begin(), m_regions.end(), [&regionName](const UiSkinRegion& value){
        return value.name == regionName;
    });
    if(region == m_regions.end())
        return false;
    if(region->drawMode == UiSkinDrawMode::NineSlice)
        emitNineSlice(*region, rectangle, tint);
    else{
        const f32 atlasWidth = static_cast<f32>(m_snapshot.m_skinBinding.atlasWidth);
        const f32 atlasHeight = static_cast<f32>(m_snapshot.m_skinBinding.atlasHeight);
        const auto& pixels = region->rectangle;
        const Rect uv{ static_cast<f32>(pixels.x) / atlasWidth, static_cast<f32>(pixels.y) / atlasHeight,
            static_cast<f32>(pixels.width) / atlasWidth, static_cast<f32>(pixels.height) / atlasHeight };
        emitQuad(rectangle, uv, tint, PaintMaterial::Skin);
    }
    return true;
}

DrawSnapshot PaintBuilder::freeze(){
    NWB_ASSERT(m_recording && m_clips.size() == 1u);
    m_recording = false;
    m_clips.clear();
    return Move(m_snapshot);
}

void PaintBuilder::emitQuad(
    const Rect& rectangle,
    const Rect& uv,
    const Color& color,
    const PaintMaterial::Enum material,
    const u32 glyphPageIndex,
    const u32 sdfPageIndex,
    const u32 sdfChannel){
    NWB_ASSERT(m_recording);
    NWB_ASSERT(IsFinite(color.r) && IsFinite(color.g) && IsFinite(color.b) && IsFinite(color.a));
    NWB_ASSERT(color.a >= 0.0f && color.a <= 1.0f);
    const Rect clip = m_clips.back();
    const Rect visible = __hidden_ui_paint::Intersect(rectangle, clip);
    if(!__hidden_ui_paint::HasArea(visible) || color.a <= 0.0f)
        return;
    if(m_snapshot.m_vertices.size() > Limit<u32>::s_Max - 4u || m_snapshot.m_indices.size() > Limit<u32>::s_Max - 6u){
        NWB_ASSERT(false);
        return;
    }
    const f32 left = uv.x + (visible.x - rectangle.x) / rectangle.width * uv.width;
    const f32 top = uv.y + (visible.y - rectangle.y) / rectangle.height * uv.height;
    const f32 right = uv.x + (visible.x + visible.width - rectangle.x) / rectangle.width * uv.width;
    const f32 bottom = uv.y + (visible.y + visible.height - rectangle.y) / rectangle.height * uv.height;
    const Color premultiplied{ color.r * color.a, color.g * color.a, color.b * color.a, color.a };
    const u32 vertexBase = static_cast<u32>(m_snapshot.m_vertices.size());
    const u32 firstIndex = static_cast<u32>(m_snapshot.m_indices.size());
    const f32 visibleRight = visible.x + visible.width;
    const f32 visibleBottom = visible.y + visible.height;
    m_snapshot.m_vertices.push_back({ { visible.x, visible.y }, { left, top }, premultiplied });
    m_snapshot.m_vertices.push_back({ { visibleRight, visible.y }, { right, top }, premultiplied });
    m_snapshot.m_vertices.push_back({ { visibleRight, visibleBottom }, { right, bottom }, premultiplied });
    m_snapshot.m_vertices.push_back({ { visible.x, visibleBottom }, { left, bottom }, premultiplied });
    const u32 indices[]{ vertexBase, vertexBase + 1u, vertexBase + 2u, vertexBase, vertexBase + 2u, vertexBase + 3u };
    m_snapshot.m_indices.insert(m_snapshot.m_indices.end(), indices, indices + 6u);
    if(!m_snapshot.m_commands.empty()){
        auto& previous = m_snapshot.m_commands.back();
        if(
            previous.material == material && previous.glyphPageIndex == glyphPageIndex
            && previous.sdfPageIndex == sdfPageIndex && previous.sdfChannel == sdfChannel
            && __hidden_ui_paint::EqualClip(previous.clip, clip)
        ){
            previous.indexCount += 6u;
            return;
        }
    }
    m_snapshot.m_commands.push_back({ firstIndex, 6u, clip, material, glyphPageIndex, sdfPageIndex, sdfChannel });
}

void PaintBuilder::emitNineSlice(const UiSkinRegion& region, const Rect& rectangle, const Color& tint){
    if(!__hidden_ui_paint::HasArea(rectangle))
        return;
    const auto& binding = m_snapshot.m_skinBinding;
    const auto& pixels = region.rectangle;
    const auto& borders = region.sliceInsets;
    const f32 left = static_cast<f32>(borders.left) / binding.referenceDensity;
    const f32 right = static_cast<f32>(borders.right) / binding.referenceDensity;
    const f32 top = static_cast<f32>(borders.top) / binding.referenceDensity;
    const f32 bottom = static_cast<f32>(borders.bottom) / binding.referenceDensity;
    const bool shrinkX = rectangle.width < left + right;
    const bool shrinkY = rectangle.height < top + bottom;
    const f32 x1 = rectangle.x + (shrinkX ? rectangle.width * (left / (left + right)) : left);
    const f32 y1 = rectangle.y + (shrinkY ? rectangle.height * (top / (top + bottom)) : top);
    const f32 x[]{ rectangle.x, x1, shrinkX ? x1 : rectangle.x + rectangle.width - right, rectangle.x + rectangle.width };
    const f32 y[]{ rectangle.y, y1, shrinkY ? y1 : rectangle.y + rectangle.height - bottom, rectangle.y + rectangle.height };
    const f32 u[]{ static_cast<f32>(pixels.x), static_cast<f32>(pixels.x + borders.left),
        static_cast<f32>(pixels.x + pixels.width - borders.right), static_cast<f32>(pixels.x + pixels.width) };
    const f32 v[]{ static_cast<f32>(pixels.y), static_cast<f32>(pixels.y + borders.top),
        static_cast<f32>(pixels.y + pixels.height - borders.bottom), static_cast<f32>(pixels.y + pixels.height) };
    const f32 atlasWidth = static_cast<f32>(binding.atlasWidth);
    const f32 atlasHeight = static_cast<f32>(binding.atlasHeight);
    for(u32 row = 0u; row < 3u; ++row){
        for(u32 column = 0u; column < 3u; ++column){
            const Rect target{ x[column], y[row], x[column + 1u] - x[column], y[row + 1u] - y[row] };
            const Rect uv{ u[column] / atlasWidth, v[row] / atlasHeight,
                (u[column + 1u] - u[column]) / atlasWidth, (v[row + 1u] - v[row]) / atlasHeight };
            emitQuad(target, uv, tint, PaintMaterial::Skin);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

