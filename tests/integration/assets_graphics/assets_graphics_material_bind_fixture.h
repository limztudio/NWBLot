// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "assets_graphics_fixture.h"


NWB_BEGIN


namespace Tests::AssetsGraphicsFixture{


class MaterialBindFixture final{
public:
    static b8 CookMinimalMeshWithMaterialBind(AStringView bindText, const CookCase& cookCase, TestArena& testArena);
    [[nodiscard]] static Expected<NWB::Impl::MaterialBindEntry> ParseMaterialBindFromText(AStringView bindText, const Path& root, TestArena& testArena, NWB::Core::Alloc::ScratchArena& scratchArena);
#if defined(NWB_FINAL)
    [[nodiscard]] static Expected<Path> EmitDuplicateGeneratedMaterialBindIncludePath(TestArena& testArena, const CookCase& cookCase);
#endif
    static b8 WriteMaterialBindShaderProbeSource(TestArena& testArena, const Path& assetRoot, AStringView shaderAssetType, AStringView metaFilename, AStringView sourceFilename, AStringView sourceText);
    static b8 WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(TestArena& testArena, const Path& assetRoot, AStringView bindText, AStringView materialText, AStringView pixelSourceText);
    static b8 CookMaterialBindMaterialIntegrationWithPixelSource(AStringView bindText, AStringView materialText, AStringView pixelSourceText, const CookCase& cookCase, TestArena& testArena);
    static b8 CookMaterialBindMaterialIntegration(AStringView bindText, AStringView materialText, const CookCase& cookCase, TestArena& testArena){
        return CookMaterialBindMaterialIntegrationWithPixelSource(bindText, materialText, s_MaterialBindShaderProbeSource, cookCase, testArena);
    }
};


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
