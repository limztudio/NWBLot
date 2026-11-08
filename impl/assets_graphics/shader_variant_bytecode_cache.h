// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


#pragma once


#include "shader_cook_plan.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderVariantBytecodeCache final{
public:
    using ScratchPath = ::Path<Core::Alloc::ScratchArena>;

    [[nodiscard]] static ScratchPath BuildPath(const Path& cacheDirectory, AStringView configurationSafeName, AStringView sourceChecksumHex, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] static b8 GetBytecode(const ShaderCook::ShaderEntry& entry, AStringView variantName, const ShaderCook::DefineCombo& defineCombo, Span<const Path> includeDirectories, Span<const Path> dependencies, const Path& sourcePath, const ScratchPath& bytecodeCachePath, b8 compilerInputsHaveBom, Core::Alloc::ScratchArena& scratchArena, Core::GraphicsBytes& outBytecode);

private:
    [[nodiscard]] static ShaderVariantCacheReadStatus::Enum ReadCached(const ShaderCook::ShaderEntry& entry, const ScratchPath& bytecodePath, Core::GraphicsBytes& outBytecode);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
