// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "widget_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/edit_box.h>

#include "fixture_text_shaping.h"

#include <tests/common/font_fixture.h>

#include <global/filesystem.h>
#include <global/simdmath.h>
#include <global/simplemath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiMultilineViewTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;
using namespace Impl::Ui;
using namespace UiWidgetTests;

inline constexpr AStringView s_MultilineViewFontPath = "tests/ui/widgets/multiline_view/font";
inline constexpr AStringView s_MultilineViewSkinPath = "tests/ui/widgets/multiline_view/skin";
inline constexpr Rect s_MultilineViewViewport{ 0.0f, 0.0f, 500.0f, 500.0f };

using NWB::Tests::UiFixtureText::FixtureShaper;

void ExpectPlacement(const EditBoxPlacement& actual, const EditBoxPlacement& expected);
void CollectSolidRects(const DrawSnapshot& snapshot, const Color& color, PaintVector<Rect>& output);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MultilineViewFixture : public testing::Test{
public:
    MultilineViewFixture();


protected:
    [[nodiscard]] b8 shapeView();
    [[nodiscard]] b8 place(f32 width = 100.0f, f32 height = 100.0f, Point previousScroll = {})noexcept;
    [[nodiscard]] b8 loadFont();
    [[nodiscard]] Expected<DrawSnapshot> paintView(const EditBoxStyle& style, const EditBoxPaintFlags& flags);


protected:
    Core::Alloc::GlobalArena m_arena;
    EditModel m_model;
    FixtureShaper m_shaper;
    TextLayoutBuilder m_layoutBuilder;
    EditBoxView m_view;
    EditBoxPlacement m_placement;
    Font m_font;
    TextService m_text;
    UiSkin m_skin;
    PaintBuilder m_paint;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

