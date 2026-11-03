// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_container.adhd.h"

#include <tests/unit/metascript/adhd_compiler_test_common.h>
#include <core/metascript/value.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_container_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AdhdReflection, RejectsInvalidContainerEntriesWithoutPublishingEmptyNames){
    NWB::AdhdCompilerTestsDetail::AdhdTestArena arena;
    using NWB::Core::Metascript::MStringView;
    using NWB::Core::Metascript::Value;
    constexpr Array<char, 3u> invalidBytes = { 'b', '\0', 'd' };
    const MStringView invalid(invalidBytes.data(), invalidBytes.size());
    Value input(arena.arena);
    auto& names = input.field("names");
    names.append(Value(MStringView("first"), arena.arena));
    names.append(Value(invalid, arena.arena));
    names.append(Value(i64(7), arena.arena));
    names.append(Value(MStringView("last"), arena.arena));
    auto& fixed = input.field("fixedNames");
    fixed.append(Value(MStringView("fixed/first"), arena.arena));
    fixed.append(Value(invalid, arena.arena));
    fixed.append(Value(MStringView("fixed/last"), arena.arena));
    auto& bounded = input.field("boundedNames");
    bounded.append(Value(invalid, arena.arena));
    bounded.append(Value(MStringView("bounded/first"), arena.arena));
    bounded.append(Value(MStringView("bounded/last"), arena.arena));
    auto& counts = input.field("counts");
    counts.field("valid").setInteger(9);
    counts.field(invalid).setInteger(3);
    counts.field("wrong_type").setString("not a number");
    auto& assets = input.field("assets");
    assets.field("valid").setString("project/asset");
    assets.field(invalid).setString("project/ignored");
    assets.field("invalid_value").setString(invalid);
    assets.field("wrong_type").setInteger(3);
    auto& enumAssets = input.field("enumAssets");
    enumAssets.field("Valid").setString("project/enum_asset");
    enumAssets.field("unknown").setString("project/ignored_enum");

    NWB::Tests::ReflectionContainerFixture value;
    value.fixedNames[1u] = Name("fixed/retained");
    ASSERT_TRUE(value.deserializeNwb(input));
    ASSERT_EQ(value.names.size(), 2u);
    EXPECT_EQ(value.names[0u], Name("first"));
    EXPECT_EQ(value.names[1u], Name("last"));
    EXPECT_EQ(value.fixedNames[0u], Name("fixed/first"));
    EXPECT_EQ(value.fixedNames[1u], Name("fixed/retained"));
    EXPECT_EQ(value.fixedNames[2u], Name("fixed/last"));
    ASSERT_EQ(value.boundedNames.size(), 2u);
    EXPECT_EQ(value.boundedNames[0u], Name("bounded/first"));
    EXPECT_EQ(value.boundedNames[1u], Name("bounded/last"));
    ASSERT_EQ(value.counts.size(), 1u);
    const auto count = value.counts.find(Name("valid"));
    ASSERT_NE(count, value.counts.end());
    EXPECT_EQ(count.value(), 9u);
    ASSERT_EQ(value.assets.size(), 1u);
    const auto asset = value.assets.find(Name("valid"));
    ASSERT_NE(asset, value.assets.end());
    EXPECT_EQ(asset.value().name(), Name("project/asset"));
    ASSERT_EQ(value.enumAssets.size(), 1u);
    Vector<Name, NWB::Core::Metascript::MetaArena> dependencies(arena.arena);
    ASSERT_TRUE(NWB::Tests::ReflectionContainerFixture::extractNwbAssetDependencies(input, [&](const Name& name){ dependencies.push_back(name); }));
    ASSERT_EQ(dependencies.size(), 2u);
    EXPECT_NE(FindIf(dependencies.begin(), dependencies.end(), [](const Name& name)noexcept{ return name == Name("project/asset"); }), dependencies.end());
    EXPECT_NE(FindIf(dependencies.begin(), dependencies.end(), [](const Name& name)noexcept{ return name == Name("project/enum_asset"); }), dependencies.end());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

