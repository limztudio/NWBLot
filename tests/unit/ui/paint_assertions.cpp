// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint_assertions.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiPaintAssertions{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    EXPECT_EQ(actual.sdfChannel, expected.sdfChannel);
    EXPECT_EQ(actual.layer, expected.layer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

