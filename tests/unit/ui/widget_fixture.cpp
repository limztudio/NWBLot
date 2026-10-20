// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiWidgetTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InstallLatinFixtureFont(Core::Alloc::ScratchArena& scratchArena, Core::Alloc::GlobalArena& arena, Font& font, TextService& text){
    const ::Path<Core::Alloc::GlobalArena> path = ::Path<Core::Alloc::GlobalArena>(arena, NWB_TEST_FONT_DIRECTORY) / "latin.font";
    Core::Assets::AssetBytes bytes(arena);
    auto bytesResult = Tests::ReadBundledFontBytes(path, bytes.get_allocator().arena());
    if(!bytesResult)
        return false;
    bytes = Move(*bytesResult);
    font.setFontBytes(Move(bytes));
    if(!font.validatePayload())
        return false;
    const FontSource source{ Core::Assets::AssetRef<Font>(s_TestLatinFontPath), font, 1u };
    return text.setFonts(scratchArena, &source, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SkinPaintSample> WidgetFixture::sampleSkinPaint(const DrawSnapshot& snapshot, const Name& name)const noexcept{
    const UiSkinRegion* region = m_skin.findRegion(name);
    if(!region)
        return MakeUnexpected(Failure{});
    const f32 atlasWidth = static_cast<f32>(m_skin.atlasWidth());
    const f32 atlasHeight = static_cast<f32>(m_skin.atlasHeight());
    const SIMDVectorSingle edges = VectorSet(static_cast<f32>(region->rectangle.x), static_cast<f32>(region->rectangle.x + region->rectangle.width), static_cast<f32>(region->rectangle.y), static_cast<f32>(region->rectangle.y + region->rectangle.height));
    const SIMDVectorSingle texCoordEdges = VectorDivide(edges, VectorSet(atlasWidth, atlasWidth, atlasHeight, atlasHeight));
    const f32 leftU = VectorGetX(texCoordEdges);
    const f32 rightU = VectorGetY(texCoordEdges);
    const f32 topV = VectorGetZ(texCoordEdges);
    const f32 bottomV = VectorGetW(texCoordEdges);
    SkinPaintSample candidate;
    // Lanes are x, y. The far corner also keeps the largest right U in z.
    SIMDVectorSingle origin = VectorZero();
    SIMDVectorSingle corner = VectorZero();
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Skin)
            continue;
        for(u32 index = command.firstIndex; index + 5u < command.firstIndex + command.indexCount; index += 6u){
            const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
            const Vertex& opposite = snapshot.vertices()[snapshot.indices()[index + 2u]];
            if(first.texCoord.x < leftU || opposite.texCoord.x > rightU || first.texCoord.y < topV || opposite.texCoord.y > bottomV)
                continue;
            const SIMDVectorSingle firstPosition = VectorSet(first.position.x, first.position.y, 0.0f, 0.0f);
            const SIMDVectorSingle oppositeCorner = VectorSet(opposite.position.x, opposite.position.y, opposite.texCoord.x, 0.0f);
            if(candidate.quads == 0u){
                origin = firstPosition;
                corner = VectorSet(opposite.position.x, opposite.position.y, candidate.maximumU, 0.0f);
                candidate.color = first.color;
            }
            origin = VectorMin(origin, firstPosition);
            corner = VectorMax(corner, oppositeCorner);
            ++candidate.quads;
        }
    }
    if(candidate.quads == 0u)
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle size = VectorSubtract(corner, origin);
    candidate.bounds = { VectorGetX(origin), VectorGetY(origin), VectorGetX(size), VectorGetY(size) };
    candidate.maximumU = VectorGetZ(corner);
    return candidate;
}

bool WidgetFixture::installKoreanFallback(){
    Core::Alloc::ScratchArena fontScratchArena(s_FontScratchArenaName);
    const auto path = ::Path<Core::Alloc::GlobalArena>(m_arena, NWB_TEST_FONT_DIRECTORY) / "korean.font";
    Core::Assets::AssetBytes bytes(m_arena);
    auto bytesResult = Tests::ReadBundledFontBytes(path, bytes.get_allocator().arena());
    if(!bytesResult)
        return false;
    bytes = Move(*bytesResult);
    Font korean(m_arena, Name(s_TestKoreanFontPath));
    korean.setFontBytes(Move(bytes));
    if(!korean.validatePayload())
        return false;
    const FontSource sources[]{
        { Core::Assets::AssetRef<Font>(s_TestLatinFontPath), m_font, 1u },
        { Core::Assets::AssetRef<Font>(s_TestKoreanFontPath), korean, 1u },
    };
    return m_text.setFonts(fontScratchArena, sources, LengthOf(sources));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

