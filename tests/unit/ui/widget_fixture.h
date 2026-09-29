// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ui/builder.h>

#include <global/filesystem.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiWidgetTests{

using namespace Impl;
using namespace Impl::Ui;

class WidgetFixture : public testing::Test{
public:
    WidgetFixture()
        : m_arena(Name("tests/ui/widgets"))
        , m_font(m_arena, Name("tests/ui/fonts/latin"))
        , m_text(m_arena)
        , m_skin(m_arena, Name("tests/ui/skin"))
        , m_paint(m_arena)
        , m_context(m_arena)
        , m_builder(m_arena, m_context, m_paint, m_text)
    {}


protected:
    virtual void SetUp()override{
        const ::Path<Core::Alloc::GlobalArena> path = ::Path<Core::Alloc::GlobalArena>(m_arena, NWB_TEST_FONT_DIRECTORY)
            / "NotoSans-Regular.ttf";
        Core::Assets::AssetBytes bytes(m_arena);
        ErrorCode error;
        ASSERT_TRUE(ReadBinaryFile(path, bytes, error));
        m_font.setFontBytes(Move(bytes));
        ASSERT_TRUE(m_font.validatePayload());
        const FontSource source{ Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_font, 1u };
        ASSERT_TRUE(m_text.setFonts(&source, 1u));
        configureSkin();
        ASSERT_TRUE(m_skin.validatePayload());
        m_builder.setSkin(m_skin);
        // The synthetic skin's icon minimum determines title height independently of font line metrics.
        m_builder.style().fontSize = 14.0f;
    }

    void configureSkin(const AStringView omitted = {}){
        UiSkin::RegionVector regions(m_arena);
        const AStringView names[]{ "window.normal", "window.title", "window.collapse", "white", "separator",
            "panel.normal", "button.normal", "button.hover", "button.pressed", "button.disabled", "focus.overlay" };
        for(u32 index = 0u; index < sizeof(names) / sizeof(names[0u]); ++index){
            if(names[index] == omitted)
                continue;
            UiSkinRegion region;
            region.name = Name(names[index]);
            region.rectangle = { index * 8u, 0u, 8u, 8u };
            if(index == 0u || index == 5u)
                region.padding = { 3.0f, 4.0f, 5.0f, 6.0f };
            else if(index == 1u)
                region.padding = { 5.0f, 6.0f, 7.0f, 8.0f };
            else if(index == 4u){
                region.minimumWidth = 2.0f;
                region.minimumHeight = 3.0f;
            }
            else if(index >= 6u && index <= 9u){
                region.padding = { 2.0f, 3.0f, 4.0f, 5.0f };
                region.minimumWidth = 30.0f;
                region.minimumHeight = 24.0f;
            }
            regions.push_back(Move(region));
        }
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 128u, 16u, 1.0f, Move(regions));
    }

    [[nodiscard]] bool begin(const u64 generation, const DisplayMetrics& display = { 800.0f, 600.0f, 1.0f, 1.0f }){
        if(!m_context.beginFrame(generation))
            return false;
        m_paint.begin(display, generation, m_skinGeneration, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
        return m_context.beginRoot(m_root);
    }

    [[nodiscard]] bool finishWindow(){
        return m_builder.endWindow() && m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] bool finishPanel(){
        return m_builder.endPanel() && m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] WidgetId id(const AStringView child, const AStringView parent = "window")const{
        return MakeWidgetId(MakeWidgetId(MakeRootId(m_root), parent), child);
    }

    [[nodiscard]] const HitTarget* target(const WidgetId widget)const{
        for(const HitTarget& entry : m_context.input().targets()){
            if(entry.id == widget)
                return &entry;
        }
        return nullptr;
    }

    [[nodiscard]] InputRoutingResult send(const InputEvent& event){
        if(!m_context.input().queue(event)){
            ADD_FAILURE() << "input admission failed";
            return {};
        }
        return m_context.input().process();
    }

    void click(const Point& point){
        EXPECT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
        EXPECT_TRUE(send({ InputEventType::PrimaryUp, point }).pointerConsumed);
    }

    void drag(const Point& origin, const Point& destination){
        EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
        EXPECT_TRUE(send({ InputEventType::PointerMove, destination }).pointerConsumed);
        EXPECT_TRUE(send({ InputEventType::PrimaryUp, destination }).pointerConsumed);
    }

    [[nodiscard]] bool skinQuad(const DrawSnapshot& snapshot, const u32 slot, Rect& rectangle)const{
        const f32 left = static_cast<f32>(slot) / 16.0f;
        const f32 right = static_cast<f32>(slot + 1u) / 16.0f;
        for(const DrawCommand& command : snapshot.commands()){
            if(command.material != PaintMaterial::Skin)
                continue;
            for(u32 index = command.firstIndex; index + 5u < command.firstIndex + command.indexCount; index += 6u){
                const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
                const Vertex& opposite = snapshot.vertices()[snapshot.indices()[index + 2u]];
                if(first.texCoord.x == left && opposite.texCoord.x == right){
                    rectangle = { first.position.x, first.position.y,
                        opposite.position.x - first.position.x, opposite.position.y - first.position.y };
                    return true;
                }
            }
        }
        return false;
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    Font m_font;
    TextService m_text;
    UiSkin m_skin;
    PaintBuilder m_paint;
    Context m_context;
    Builder m_builder;
    WidgetRoot m_root{ 86u, 2u };
    u64 m_skinGeneration = 1u;
};

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

