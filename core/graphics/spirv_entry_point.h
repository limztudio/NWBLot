// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "rhi/shader.h"

#include <global/binary.h>
#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SpirvEntryPointLookupResult{
    enum Enum : u8{
        NotFound,
        InvalidSpirv,
    };
};


[[nodiscard]] b8 IsValidSpirvModuleWords(Span<const u32> words)noexcept;

// The result borrows unchanged module words and excludes the validated terminating null.
[[nodiscard]] Expected<AStringView, SpirvEntryPointLookupResult::Enum> ResolveSpirvEntryPointName(Span<const u32> words, AStringView entryName, ShaderType::Mask shaderType)noexcept;

// Byte backing may be unaligned; the result borrows the original module bytes.
[[nodiscard]] Expected<AStringView, SpirvEntryPointLookupResult::Enum> ResolveSpirvEntryPointName(BinaryByteView bytecode, AStringView entryName, ShaderType::Mask shaderType)noexcept;

[[nodiscard]] inline Expected<AStringView, SpirvEntryPointLookupResult::Enum> ResolveSpirvEntryPointName(const Span<const u8> bytecode, const AStringView entryName, const ShaderType::Mask shaderType)noexcept{
    return ResolveSpirvEntryPointName(BinaryByteView{ bytecode.data(), bytecode.size() }, entryName, shaderType);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

