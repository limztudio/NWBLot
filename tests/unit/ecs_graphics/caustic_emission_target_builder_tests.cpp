// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/caustic/caustic_emission_target_builder.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_caustic_emission_target_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CausticEmissionTargetBuilderTests, SupportedVolumesDoNotRetireFirstUnsupportedIdentity){
    const CausticEmissionTargetBuilder builder;
    u32 first = 0u;
    u32 count = 0u;
    u64 hash = 14695981039346656037ull;
    const u64 initialHash = hash;
    builder.recordUnsupportedVolume(9u, OpticalBoundaryMode::ClosedNested, first, count, hash);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(hash, initialHash);
    builder.recordUnsupportedVolume(11u, OpticalBoundaryMode::Unspecified, first, count, hash);
    const u64 firstHash = hash;
    EXPECT_EQ(first, 11u);
    EXPECT_EQ(count, 1u);
    EXPECT_NE(hash, initialHash);
    builder.recordUnsupportedVolume(12u, OpticalBoundaryMode::ClosedPriority, first, count, hash);
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(hash, firstHash);
    builder.recordUnsupportedVolume(13u, OpticalBoundaryMode::Unspecified, first, count, hash);
    EXPECT_EQ(first, 11u);
    EXPECT_EQ(count, 2u);
    EXPECT_NE(hash, firstHash);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
