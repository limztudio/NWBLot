// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


#pragma once


#include "cook.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


class MaterialAvboitShaderGenerator{
public:
    [[nodiscard]] static Expected<MaterialCookVector<GeneratedMaterialPixelShader>> emitAccumulate(const Path& cacheDirectory, AStringView configurationSafeName, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena, Span<MaterialCookEntry> materialEntries);
    [[nodiscard]] static Expected<MaterialCookVector<GeneratedMaterialPixelShader>> emitOccupancy(const Path& cacheDirectory, AStringView configurationSafeName, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena, Span<MaterialCookEntry> materialEntries);
    [[nodiscard]] static Expected<MaterialCookVector<GeneratedMaterialPixelShader>> emitExtinction(const Path& cacheDirectory, AStringView configurationSafeName, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena, Span<MaterialCookEntry> materialEntries);
};


NWB_IMPL_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
