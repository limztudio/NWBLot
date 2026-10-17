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
    const FontSource source{ Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), font, 1u };
    return text.setFonts(scratchArena, &source, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SkinPaintSample> WidgetFixture::sampleSkinPaint(const DrawSnapshot& snapshot, const Name& name)const noexcept{
    const UiSkinRegion* region = m_skin.findRegion(name);
    if(!region)
        return MakeUnexpected(Failure{});
    const f32 leftU = static_cast<f32>(region->rectangle.x) / m_skin.atlasWidth();
    const f32 rightU = static_cast<f32>(region->rectangle.x + region->rectangle.width) / m_skin.atlasWidth();
    const f32 topV = static_cast<f32>(region->rectangle.y) / m_skin.atlasHeight();
    const f32 bottomV = static_cast<f32>(region->rectangle.y + region->rectangle.height) / m_skin.atlasHeight();
    SkinPaintSample candidate;
    f32 right = 0.0f;
    f32 bottom = 0.0f;
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Skin)
            continue;
        for(u32 index = command.firstIndex; index + 5u < command.firstIndex + command.indexCount; index += 6u){
            const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
            const Vertex& opposite = snapshot.vertices()[snapshot.indices()[index + 2u]];
            if(first.texCoord.x < leftU || opposite.texCoord.x > rightU || first.texCoord.y < topV || opposite.texCoord.y > bottomV)
                continue;
            if(candidate.quads == 0u){
                candidate.bounds.x = first.position.x;
                candidate.bounds.y = first.position.y;
                candidate.color = first.color;
                right = opposite.position.x;
                bottom = opposite.position.y;
            }
            candidate.bounds.x = Min(candidate.bounds.x, first.position.x);
            candidate.bounds.y = Min(candidate.bounds.y, first.position.y);
            right = Max(right, opposite.position.x);
            bottom = Max(bottom, opposite.position.y);
            candidate.maximumU = Max(candidate.maximumU, opposite.texCoord.x);
            ++candidate.quads;
        }
    }
    if(candidate.quads == 0u)
        return MakeUnexpected(Failure{});
    candidate.bounds.width = right - candidate.bounds.x;
    candidate.bounds.height = bottom - candidate.bounds.y;
    return candidate;
}

bool WidgetFixture::installKoreanFallback(){
    Core::Alloc::ScratchArena fontScratchArena(Name("tests/ui/fonts/operation_scratch"));
    const auto path = ::Path<Core::Alloc::GlobalArena>(m_arena, NWB_TEST_FONT_DIRECTORY) / "korean.font";
    Core::Assets::AssetBytes bytes(m_arena);
    auto bytesResult = Tests::ReadBundledFontBytes(path, bytes.get_allocator().arena());
    if(!bytesResult)
        return false;
    bytes = Move(*bytesResult);
    Font korean(m_arena, Name("tests/ui/fonts/korean"));
    korean.setFontBytes(Move(bytes));
    if(!korean.validatePayload())
        return false;
    const FontSource sources[]{
        { Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_font, 1u },
        { Core::Assets::AssetRef<Font>("tests/ui/fonts/korean"), korean, 1u },
    };
    return m_text.setFonts(fontScratchArena, sources, LengthOf(sources));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

