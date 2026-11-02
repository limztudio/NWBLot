// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint_assertions.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiPaintAssertions{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ExpectRect(const Impl::Ui::Rect& actual, const Impl::Ui::Rect& expected){
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.width, expected.width);
    EXPECT_FLOAT_EQ(actual.height, expected.height);
}

void ExpectSameVertices(const Impl::Ui::DrawSnapshot& actual, const Impl::Ui::DrawSnapshot& expected){
    for(usize index = 0u; index < actual.vertices().size(); ++index){
        const Impl::Ui::Vertex& a = actual.vertices()[index];
        const Impl::Ui::Vertex& e = expected.vertices()[index];
        EXPECT_FLOAT_EQ(a.position.x, e.position.x);
        EXPECT_FLOAT_EQ(a.position.y, e.position.y);
        EXPECT_FLOAT_EQ(a.texCoord.x, e.texCoord.x);
        EXPECT_FLOAT_EQ(a.texCoord.y, e.texCoord.y);
        EXPECT_FLOAT_EQ(a.color.r, e.color.r);
        EXPECT_FLOAT_EQ(a.color.g, e.color.g);
        EXPECT_FLOAT_EQ(a.color.b, e.color.b);
        EXPECT_FLOAT_EQ(a.color.a, e.color.a);
    }
}

void ExpectSameCommandFields(const Impl::Ui::DrawCommand& actual, const Impl::Ui::DrawCommand& expected){
    EXPECT_EQ(actual.firstIndex, expected.firstIndex);
    EXPECT_EQ(actual.indexCount, expected.indexCount);
    EXPECT_EQ(actual.material, expected.material);
    EXPECT_EQ(actual.glyphPageIndex, expected.glyphPageIndex);
    EXPECT_EQ(actual.sdfPageIndex, expected.sdfPageIndex);
    EXPECT_EQ(actual.textureImageIndex, expected.textureImageIndex);
    EXPECT_EQ(actual.sdfChannel, expected.sdfChannel);
    EXPECT_EQ(actual.layer, expected.layer);
}

void ExpectSamePaint(const Impl::Ui::DrawSnapshot& actual, const Impl::Ui::DrawSnapshot& expected){
    ASSERT_EQ(actual.glyphPages().size(), expected.glyphPages().size());
    ASSERT_EQ(actual.sdfPages().size(), expected.sdfPages().size());
    ASSERT_EQ(actual.vertices().size(), expected.vertices().size());
    ASSERT_EQ(actual.commands().size(), expected.commands().size());
    EXPECT_EQ(actual.indices(), expected.indices());
    for(usize index = 0u; index < actual.glyphPages().size(); ++index){
        ASSERT_TRUE(actual.glyphPages()[index]);
        ASSERT_TRUE(expected.glyphPages()[index]);
        EXPECT_EQ(actual.glyphPages()[index].get(), expected.glyphPages()[index].get());
        EXPECT_EQ(actual.glyphPages()[index]->binding(), expected.glyphPages()[index]->binding());
        EXPECT_EQ(actual.glyphPages()[index]->pixels(), expected.glyphPages()[index]->pixels());
    }
    for(usize index = 0u; index < actual.sdfPages().size(); ++index){
        ASSERT_TRUE(actual.sdfPages()[index]);
        ASSERT_TRUE(expected.sdfPages()[index]);
        EXPECT_EQ(actual.sdfPages()[index].get(), expected.sdfPages()[index].get());
        EXPECT_EQ(actual.sdfPages()[index]->binding(), expected.sdfPages()[index]->binding());
        EXPECT_EQ(actual.sdfPages()[index]->pixels(), expected.sdfPages()[index]->pixels());
    }
    ExpectSameVertices(actual, expected);
    for(usize index = 0u; index < actual.commands().size(); ++index){
        const Impl::Ui::DrawCommand& a = actual.commands()[index];
        const Impl::Ui::DrawCommand& e = expected.commands()[index];
        ExpectSameCommandFields(a, e);
        ExpectRect(a.clip, e.clip);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

