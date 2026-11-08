// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_material_bind_fixture.h"


NWB_BEGIN


namespace Tests{


namespace __hidden_material_bind_fixture{

constexpr AStringView s_EngineDirectory = "impl";
#if defined(NWB_FINAL)
constexpr AStringView s_CacheDirectory = "__asset_cache";
constexpr AStringView s_TestsConfiguration = "tests";
#endif
constexpr AStringView s_GraphicsDirectory = "graphics";
constexpr AStringView s_AssetsDirectory = "assets";
constexpr AStringView s_MeshesDirectory = "meshes";
constexpr AStringView s_MaterialInterfacesDirectory = "material_interfaces";
constexpr AStringView s_SurfaceBindFilename = "test_surface.bind";
constexpr AStringView s_ShadersDirectory = "shaders";



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 WriteMaterialBindDiscoveryMetadata(const Path& assetRoot){
    if(!AssetsGraphicsFixture::WriteTextFile(assetRoot / __hidden_material_bind_fixture::s_MaterialInterfacesDirectory / "bind_discovery.nwb", "include asset;\r\n\r\nasset.defines = { \"NWB_TEST_MATERIAL_BIND_DISCOVERY\": [\"1\"] };\r\n"))
        return false;
    return AssetsGraphicsFixture::WriteTextFile(
        assetRoot / __hidden_material_bind_fixture::s_MaterialInterfacesDirectory / "bind_discovery.slangi",
        "// limztudio@gmail.com\r\n"
        "////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n"
        "#ifndef NWB_TEST_MATERIAL_BIND_DISCOVERY_SLANGI\r\n"
        "#define NWB_TEST_MATERIAL_BIND_DISCOVERY_SLANGI\r\n\r\n\r\n"
        "////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n"
        "#endif\r\n\r\n\r\n"
        "////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n"
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AssetsGraphicsFixture::MaterialBindFixture::CookMinimalMeshWithMaterialBind(const AStringView bindText, const AssetsGraphicsFixture::CookCase& cookCase, AssetsGraphicsFixture::TestArena& testArena){
    const Path assetRoot = cookCase.root / __hidden_material_bind_fixture::s_AssetsDirectory;
    if(!__hidden_material_bind_fixture::WriteMaterialBindDiscoveryMetadata(assetRoot))
        return false;
    if(!WriteTextFile(assetRoot / __hidden_material_bind_fixture::s_MeshesDirectory / "minimal_mesh.nwb", s_MinimalMeshMeta))
        return false;
    if(!WriteTextFile(assetRoot / __hidden_material_bind_fixture::s_MaterialInterfacesDirectory / __hidden_material_bind_fixture::s_SurfaceBindFilename, bindText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, cookCase.root, cookCase.outputDirectory, { assetRoot });
}

Expected<NWB::Impl::MaterialBindEntry> AssetsGraphicsFixture::MaterialBindFixture::ParseMaterialBindFromText(const AStringView bindText, const Path& root, AssetsGraphicsFixture::TestArena& testArena, NWB::Core::Alloc::ScratchArena& scratchArena){
    const Path bindPath = root / __hidden_material_bind_fixture::s_AssetsDirectory / __hidden_material_bind_fixture::s_MaterialInterfacesDirectory / __hidden_material_bind_fixture::s_SurfaceBindFilename;
    if(!WriteTextFile(bindPath, bindText))
        return MakeUnexpected(Failure{});

    return NWB::Impl::ParseMaterialBindSource(bindPath, testArena.arena, scratchArena);
}

#if defined(NWB_FINAL)
Expected<Path> AssetsGraphicsFixture::MaterialBindFixture::EmitDuplicateGeneratedMaterialBindIncludePath(AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::CookCase& cookCase){
    const Path firstAssetRoot = cookCase.root / "first" / __hidden_material_bind_fixture::s_AssetsDirectory;
    const Path secondAssetRoot = cookCase.root / "second" / __hidden_material_bind_fixture::s_AssetsDirectory;
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialScratchArena);
    NWB::Impl::MaterialCookVector<NWB::Impl::MaterialBindEntry> bindEntries(testArena.arena);
    for(const Path& assetRoot : InitializerList<Path>{ firstAssetRoot, secondAssetRoot }){
        const Path bindPath = assetRoot / __hidden_material_bind_fixture::s_MaterialInterfacesDirectory / __hidden_material_bind_fixture::s_SurfaceBindFilename;
        if(!WriteTextFile(bindPath, s_MinimalMaterialBindSource))
            return MakeUnexpected(Failure{});

        auto bindEntry = NWB::Impl::ParseMaterialBindSource(bindPath, testArena.arena, scratchArena);
        if(!bindEntry)
            return MakeUnexpected(bindEntry.error());
        auto virtualPath = NWB::Core::Assets::AssetPathsDetail::BuildDerivedAssetVirtualPathText(assetRoot, NWB::Core::Assets::s_ProjectVirtualRoot, bindPath, testArena.arena, scratchArena);
        if(!virtualPath)
            return MakeUnexpected(virtualPath.error());
        bindEntry->virtualPath = Move(*virtualPath);
        bindEntries.push_back(Move(*bindEntry));
    }

    return NWB::Impl::EmitMaterialBindIncludes(cookCase.root / __hidden_material_bind_fixture::s_CacheDirectory, __hidden_material_bind_fixture::s_TestsConfiguration, bindEntries, testArena.arena, scratchArena);
}
#endif

b8 AssetsGraphicsFixture::MaterialBindFixture::WriteMaterialBindShaderProbeSource(
    AssetsGraphicsFixture::TestArena& testArena,
    const Path& assetRoot,
    AStringView shaderAssetType,
    AStringView metaFilename,
    AStringView sourceFilename,
    const AStringView sourceText
){
    const Path engineGraphicsIncludeRoot = AssetsGraphicsTestRepoRoot(testArena) / __hidden_material_bind_fixture::s_EngineDirectory / __hidden_material_bind_fixture::s_AssetsDirectory / __hidden_material_bind_fixture::s_GraphicsDirectory;
    const NWB::Impl::ShaderCook::CookString engineGraphicsIncludeRootText = PathToString(testArena.arena, engineGraphicsIncludeRoot);
    NWB::Impl::ShaderCook::CookString shaderMeta(testArena.arena);
    shaderMeta += shaderAssetType;
    shaderMeta += " asset;\n\n" "asset.entry_point = \"main\";\n" "asset.include_roots = [\"";
    shaderMeta += engineGraphicsIncludeRootText;
    shaderMeta += "\"];\n";

    if(!WriteTextFile(assetRoot / __hidden_material_bind_fixture::s_ShadersDirectory / metaFilename, AStringView(shaderMeta.data(), shaderMeta.size())))
        return false;
    return WriteTextFile(assetRoot / __hidden_material_bind_fixture::s_ShadersDirectory / sourceFilename, sourceText);
}

b8 AssetsGraphicsFixture::MaterialBindFixture::WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
    AssetsGraphicsFixture::TestArena& testArena,
    const Path& assetRoot,
    const AStringView bindText,
    const AStringView materialText,
    const AStringView pixelSourceText
){
    if(!WriteTextFile(assetRoot / __hidden_material_bind_fixture::s_MaterialInterfacesDirectory / __hidden_material_bind_fixture::s_SurfaceBindFilename, bindText))
        return false;
    if(!WriteMaterialBindShaderProbeSource(testArena, assetRoot, "mesh_shader", "material_mesh.nwb", "material_mesh.slang", s_MaterialBindMeshSource))
        return false;
    if(!WriteMaterialBindShaderProbeSource(testArena, assetRoot, "pixel_shader", "material_ps.nwb", "material_ps.slang", pixelSourceText))
        return false;
    if(!WriteTextFile(assetRoot / __hidden_material_bind_fixture::s_ShadersDirectory / "material_bxdf.bxdf", s_MaterialBindBxdfSource))
        return false;
    return WriteTextFile(assetRoot / "materials" / "test_material.nwb", materialText);
}

b8 AssetsGraphicsFixture::MaterialBindFixture::CookMaterialBindMaterialIntegrationWithPixelSource(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView pixelSourceText,
    const AssetsGraphicsFixture::CookCase& cookCase,
    AssetsGraphicsFixture::TestArena& testArena
){
    const Path assetRoot = cookCase.root / __hidden_material_bind_fixture::s_AssetsDirectory;
    if(!WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(testArena, assetRoot, bindText, materialText, pixelSourceText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, cookCase.root, cookCase.outputDirectory, { assetRoot });
}



};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
