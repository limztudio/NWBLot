// limztudio@gmail.com


#include <core/mesh/classification.h>
#include <gtest/gtest.h>


namespace __hidden_mesh_tests{


TEST(Mesh, MeshClassMetadata){
    using namespace NWB::Core::Mesh;

    struct Case{
        AStringView text;
        u32 meshClass;
        bool usesSkinning;
    };

    const Case cases[] = {
        { "static", MeshClass::Static, false },
        { "skinned", MeshClass::Skinned, true },
    };

    for(const Case& testCase : cases){
        u32 parsedClass = MeshClass::Invalid;
        EXPECT_TRUE(ParseMeshClassText(testCase.text, parsedClass));
        EXPECT_EQ(parsedClass, testCase.meshClass);
        EXPECT_TRUE(ValidMeshClass(testCase.meshClass));
        EXPECT_EQ(MeshClassText(testCase.meshClass), testCase.text);
        EXPECT_EQ(MeshClassUsesSkinning(testCase.meshClass), testCase.usesSkinning);
    }

    u32 parsedClass = MeshClass::Static;
    EXPECT_FALSE(ParseMeshClassText("STATIC", parsedClass));
    EXPECT_EQ(parsedClass, MeshClass::Invalid);
    EXPECT_EQ(MeshClassText(MeshClass::Invalid), AStringView("invalid"));
    EXPECT_EQ(MeshClassText(999u), AStringView("unknown"));
}


};


