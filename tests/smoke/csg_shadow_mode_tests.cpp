// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_shadow_mode.h"

#include <gtest/gtest.h>

#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_shadow_mode_tests{


using namespace NWB::Tests::Smoke;


static void SetModeEnvironment(const char* name, const char* value){
#if defined(_MSC_VER)
    ASSERT_EQ(::_putenv_s(name, value ? value : ""), 0);
#else
    ASSERT_EQ(value ? ::setenv(name, value, 1) : ::unsetenv(name), 0);
#endif
}


TEST(CsgShadowMode, RejectsIncompatibleAtlasCombinations){
    SetModeEnvironment("NWB_CSG_SHADOW_ARM", nullptr);
    SetModeEnvironment("NWB_CSG_SHADOW_LIGHT", nullptr);
    SetModeEnvironment("NWB_CSG_SHADOW_LIGHT_SOURCE", nullptr);
    SetModeEnvironment("NWB_CSG_SHADOW_ATLAS", "open");
    SetModeEnvironment("NWB_CSG_SHADOW_OPEN_CASE", nullptr);
    EXPECT_FALSE(CsgShadowMode{}.read(false));

    SetModeEnvironment("NWB_CSG_SHADOW_OPEN_CASE", "boundary64");
    CsgShadowMode open;
    ASSERT_TRUE(open.read(false));
    EXPECT_EQ(open.openCase, CsgShadowOpenCase::Boundary64);
    EXPECT_EQ(open.openCaseName(), "boundary64");
    EXPECT_FALSE(CsgShadowMode{}.read(true));

    SetModeEnvironment("NWB_CSG_SHADOW_LIGHT", "point");
    EXPECT_FALSE(CsgShadowMode{}.read(false));
    SetModeEnvironment("NWB_CSG_SHADOW_LIGHT", "directional");
    SetModeEnvironment("NWB_CSG_SHADOW_LIGHT_SOURCE", "finite");
    EXPECT_FALSE(CsgShadowMode{}.read(false));
    SetModeEnvironment("NWB_CSG_SHADOW_LIGHT_SOURCE", "hard");
    SetModeEnvironment("NWB_CSG_SHADOW_OPEN_CASE", "unknown");
    EXPECT_FALSE(CsgShadowMode{}.read(false));

    SetModeEnvironment("NWB_CSG_SHADOW_ATLAS", "analytic");
    SetModeEnvironment("NWB_CSG_SHADOW_ARM", "uncut");
    EXPECT_TRUE(CsgShadowMode{}.read(false));
    SetModeEnvironment("NWB_CSG_SHADOW_ARM", "moved");
    EXPECT_FALSE(CsgShadowMode{}.read(false));
    SetModeEnvironment("NWB_CSG_SHADOW_ARM", "invalid");
    EXPECT_FALSE(CsgShadowMode{}.read(false));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

