// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendCsgShapeCookEntry(
    NWB::Impl::AssetsCsgCook::CsgShapeCookEntryVector& outEntries,
    NWB::Impl::AssetsCsgCook::CookArena& arena,
    const Name shapeName,
    const Name shaderModule,
    const AStringView moduleInclude
){
    NWB::Impl::AssetsCsgCook::CsgShapeCookEntry entry(arena);
    entry.shapeName = shapeName;
    entry.shaderModule = shaderModule;
    entry.evalInclude.assign("project/csg/tests/eval.slangi");
    entry.moduleInclude.assign(moduleInclude.data(), moduleInclude.size());
    outEntries.push_back(Move(entry));
}

[[nodiscard]] static NWB::Impl::CsgShapeTypeId FindCookedCsgShapeTypeId(
    const NWB::Impl::AssetsCsgCook::CsgShapeCookEntryVector& entries,
    const Name shapeName
){
    for(const NWB::Impl::AssetsCsgCook::CsgShapeCookEntry& entry : entries){
        if(entry.shapeName == shapeName)
            return entry.shapeTypeId;
    }
    return NWB::Impl::s_InvalidCsgShapeTypeId;
}

static bool CsgCookTestBounds(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds,
    bool& outFiniteBounds
){
    static_cast<void>(shapeToWorld);
    static_cast<void>(parameterBytes);
    static_cast<void>(parameterByteSize);
    outMinBounds = VectorSet(-1.0f, -1.0f, -1.0f, 0.0f);
    outMaxBounds = VectorSet(1.0f, 1.0f, 1.0f, 0.0f);
    outFiniteBounds = true;
    return true;
}

[[nodiscard]] static NWB::Impl::CsgShapeTypeDesc CsgCookTestShapeDesc(
    const Name shapeName,
    const Name shaderModule,
    const AStringView moduleInclude
){
    NWB::Impl::CsgShapeTypeDesc desc;
    desc.name = shapeName;
    desc.shaderModule = shaderModule;
    desc.shaderModuleInclude = ACompactString(moduleInclude);
    desc.boundsCallback = &CsgCookTestBounds;
    return desc;
}

TEST(AssetsGraphics, CsgShapeCookAndRuntimeUseCanonicalIdsRegardlessOfRegistrationOrder){
    TestArena testArena;
    using namespace NWB::Impl::AssetsCsgCook;

    const Name alphaShape("project/csg/alpha_shape");
    const Name zebraShape("project/csg/zebra_shape");
    const Name alphaModule("project/csg/alpha_module");
    const Name zebraModule("project/csg/zebra_module");
    const AStringView alphaInclude("project/csg/generated/alpha.slangi");
    const AStringView zebraInclude("project/csg/generated/zebra.slangi");

    CsgShapeCookEntryVector cookedEntries(testArena.arena);
    // The cooker normalizes its input by name, while the runtime deliberately receives the inverse order.
    AppendCsgShapeCookEntry(cookedEntries, testArena.arena, zebraShape, zebraModule, zebraInclude);
    AppendCsgShapeCookEntry(cookedEntries, testArena.arena, alphaShape, alphaModule, alphaInclude);
    ASSERT_TRUE(AssignCsgShapeCookIds(cookedEntries));

    const NWB::Impl::CsgShapeTypeId cookedAlphaId = FindCookedCsgShapeTypeId(cookedEntries, alphaShape);
    const NWB::Impl::CsgShapeTypeId cookedZebraId = FindCookedCsgShapeTypeId(cookedEntries, zebraShape);
    ASSERT_NE(cookedAlphaId, NWB::Impl::s_InvalidCsgShapeTypeId);
    ASSERT_NE(cookedZebraId, NWB::Impl::s_InvalidCsgShapeTypeId);
    EXPECT_NE(cookedAlphaId, cookedZebraId);
    EXPECT_EQ(cookedAlphaId, NWB::Impl::CsgShapeTypeIdFromName(alphaShape));
    EXPECT_EQ(cookedZebraId, NWB::Impl::CsgShapeTypeIdFromName(zebraShape));

    NWB::Impl::CsgShapeRegistry registry(testArena.arena);
    NWB::Impl::CsgShapeTypeId runtimeZebraId = NWB::Impl::s_InvalidCsgShapeTypeId;
    NWB::Impl::CsgShapeTypeId runtimeAlphaId = NWB::Impl::s_InvalidCsgShapeTypeId;
    ASSERT_TRUE(registry.registerShapeType(CsgCookTestShapeDesc(zebraShape, zebraModule, zebraInclude), runtimeZebraId));
    ASSERT_TRUE(registry.registerShapeType(CsgCookTestShapeDesc(alphaShape, alphaModule, alphaInclude), runtimeAlphaId));
    EXPECT_EQ(runtimeAlphaId, cookedAlphaId);
    EXPECT_EQ(runtimeZebraId, cookedZebraId);
}

#if defined(NWB_FINAL)
TEST(AssetsGraphics, CsgShapeCookRejectsGeneratedModuleIncludeCollisions){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    using namespace NWB::Impl::AssetsCsgCook;

    CsgShapeCookEntryVector entries(testArena.arena);
    AppendCsgShapeCookEntry(
        entries,
        testArena.arena,
        Name("project/csg/first_shape"),
        Name("project/csg/first_module"),
        "project/csg/generated/shared.slangi"
    );
    AppendCsgShapeCookEntry(
        entries,
        testArena.arena,
        Name("project/csg/second_shape"),
        Name("project/csg/second_module"),
        "project/csg/generated/SHARED.slangi"
    );

    Path root(testArena.arena);
    ASSERT_TRUE(PrepareAssetsGraphicsCaseRoot(testArena, "csg_module_include_collision", root));

    Path includeRoot(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(s_ShaderScratchArena);
    EXPECT_FALSE(EmitCsgShapeModuleIncludes(
        root / "cache",
        "tests",
        entries,
        includeRoot,
        scratchArena
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("generated include")));

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
