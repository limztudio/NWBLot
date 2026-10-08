// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "spirv_entry_point.h"

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_spirv_entry_point{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_OpEntryPoint = 15u;
inline constexpr u32 s_SpirvMagic = 0x07230203u;
inline constexpr usize s_SpirvHeaderWords = 5u;
inline constexpr usize s_SpirvMagicWordIndex = 0u;
inline constexpr usize s_SpirvEntryPointExecutionModelWordIndex = 1u;
inline constexpr usize s_SpirvEntryPointFixedWordCount = 3u;
inline constexpr usize s_SpirvEntryPointNameWordIndex = s_SpirvEntryPointFixedWordCount;
inline constexpr u32 s_SpirvInstructionOpcodeBitMask = 0xffffu;
inline constexpr u32 s_SpirvInstructionWordCountBitShift = 16u;

namespace SpirvExecutionModel{
    enum Enum : u32{
        Vertex = 0u,
        TessellationControl = 1u,
        TessellationEvaluation = 2u,
        Geometry = 3u,
        Fragment = 4u,
        GLCompute = 5u,
        TaskNV = 5267u,
        MeshNV = 5268u,
        RayGenerationKHR = 5313u,
        IntersectionKHR = 5314u,
        AnyHitKHR = 5315u,
        ClosestHitKHR = 5316u,
        MissKHR = 5317u,
        CallableKHR = 5318u,
        TaskEXT = 5364u,
        MeshEXT = 5365u,
    };
};

struct SpirvEntryPointInstruction{
    AStringView name;
    ShaderType::Mask shaderType = ShaderType::None;
};

struct SpirvWordSource{
    const u32* words;
    usize wordCount;

    [[nodiscard]] bool valid()const noexcept{ return words != nullptr; }
    [[nodiscard]] usize size()const noexcept{ return wordCount; }
    [[nodiscard]] u32 word(const usize index)const noexcept{ return words[index]; }
    [[nodiscard]] AStringView nameBytes(const usize index, const usize count)const noexcept{
        return AStringView(reinterpret_cast<const char*>(words + index), count * sizeof(u32));
    }
};

struct SpirvByteSource{
    BinaryByteView bytecode;

    [[nodiscard]] bool valid()const noexcept{ return bytecode.data() != nullptr && bytecode.size() % sizeof(u32) == 0u; }
    [[nodiscard]] usize size()const noexcept{ return bytecode.size() / sizeof(u32); }
    [[nodiscard]] u32 word(const usize index)const noexcept{
        const u8* const wordBytes = bytecode.data() + index * sizeof(u32);
        const u8 bytes[sizeof(u32)]{ wordBytes[0u], wordBytes[1u], wordBytes[2u], wordBytes[3u] };
        return BitCast<u32>(bytes);
    }
    [[nodiscard]] AStringView nameBytes(const usize index, const usize count)const noexcept{
        return AStringView(reinterpret_cast<const char*>(bytecode.data() + index * sizeof(u32)), count * sizeof(u32));
    }
};


inline ShaderType::Mask ConvertExecutionModel(const u32 executionModel)noexcept{
    switch(executionModel){
    case SpirvExecutionModel::Vertex: return ShaderType::Vertex;
    case SpirvExecutionModel::TessellationControl: return ShaderType::Hull;
    case SpirvExecutionModel::TessellationEvaluation: return ShaderType::Domain;
    case SpirvExecutionModel::Geometry: return ShaderType::Geometry;
    case SpirvExecutionModel::Fragment: return ShaderType::Pixel;
    case SpirvExecutionModel::GLCompute: return ShaderType::Compute;
    case SpirvExecutionModel::TaskNV: return ShaderType::Amplification;
    case SpirvExecutionModel::MeshNV: return ShaderType::Mesh;
    case SpirvExecutionModel::RayGenerationKHR: return ShaderType::RayGeneration;
    case SpirvExecutionModel::IntersectionKHR: return ShaderType::Intersection;
    case SpirvExecutionModel::AnyHitKHR: return ShaderType::AnyHit;
    case SpirvExecutionModel::ClosestHitKHR: return ShaderType::ClosestHit;
    case SpirvExecutionModel::MissKHR: return ShaderType::Miss;
    case SpirvExecutionModel::CallableKHR: return ShaderType::Callable;
    case SpirvExecutionModel::TaskEXT: return ShaderType::Amplification;
    case SpirvExecutionModel::MeshEXT: return ShaderType::Mesh;
    default: return ShaderType::None;
    }
}

template<typename Source>
[[nodiscard]] NWB_INLINE bool DecodeEntryPointInstruction(
    const Source& source,
    const usize instructionIndex,
    const u16 instructionWordCount,
    SpirvEntryPointInstruction& outEntryPoint
)noexcept{
    outEntryPoint = SpirvEntryPointInstruction();

    if(instructionWordCount <= s_SpirvEntryPointFixedWordCount)
        return false;

    outEntryPoint.shaderType = ConvertExecutionModel(source.word(instructionIndex + s_SpirvEntryPointExecutionModelWordIndex));

    const AStringView entryPointBytes = source.nameBytes(
        instructionIndex + s_SpirvEntryPointNameWordIndex,
        static_cast<usize>(instructionWordCount) - s_SpirvEntryPointFixedWordCount
    );
    const usize entryPointLength = entryPointBytes.find('\0');
    if(entryPointLength == AStringView::npos)
        return false;

    outEntryPoint.name = entryPointBytes.substr(0u, entryPointLength);
    return true;
}

template<typename Source, typename EntryPointCallback>
[[nodiscard]] NWB_INLINE bool ScanSpirvEntryPoints(
    const Source& source,
    EntryPointCallback entryPointCallback
)noexcept(noexcept(entryPointCallback(*static_cast<SpirvEntryPointInstruction*>(nullptr))) && IsNothrowDestructible_V<EntryPointCallback>){
    const usize wordCount = source.size();
    if(!source.valid() || wordCount < s_SpirvHeaderWords)
        return false;

    if(source.word(s_SpirvMagicWordIndex) != s_SpirvMagic)
        return false;

    for(usize instructionIndex = s_SpirvHeaderWords; instructionIndex < wordCount; ){
        const u32 instruction = source.word(instructionIndex);
        const u16 opcode = static_cast<u16>(instruction & s_SpirvInstructionOpcodeBitMask);
        const u16 instructionWordCount = static_cast<u16>(instruction >> s_SpirvInstructionWordCountBitShift);
        if(instructionWordCount == 0)
            return false;

        if(static_cast<usize>(instructionWordCount) > wordCount - instructionIndex)
            return false;

        if(opcode == s_OpEntryPoint){
            SpirvEntryPointInstruction entryPoint;
            if(!DecodeEntryPointInstruction(source, instructionIndex, instructionWordCount, entryPoint))
                return false;

            entryPointCallback(entryPoint);
        }

        instructionIndex += instructionWordCount;
    }

    return true;
}

template<typename Source>
[[nodiscard]] NWB_INLINE SpirvEntryPointLookupResult::Enum ResolveEntryPointName(
    const Source& source,
    const AStringView entryName,
    const ShaderType::Mask shaderType,
    AStringView& outEntryPointName
)noexcept{
    outEntryPointName = {};

    if(entryName.empty() || shaderType == ShaderType::None)
        return SpirvEntryPointLookupResult::NotFound;

    bool found = false;
    const bool validModule = ScanSpirvEntryPoints(
        source,
        [&](const SpirvEntryPointInstruction& entryPoint)noexcept{
            if(found || entryPoint.shaderType == ShaderType::None || entryPoint.shaderType != shaderType || entryPoint.name != entryName)
                return;

            outEntryPointName = entryPoint.name;
            found = true;
        }
    );
    if(!validModule){
        outEntryPointName = {};
        return SpirvEntryPointLookupResult::InvalidSpirv;
    }

    return found ? SpirvEntryPointLookupResult::Found : SpirvEntryPointLookupResult::NotFound;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsValidSpirvModuleWords(
    const u32* words,
    const usize wordCount
)noexcept{
    return __hidden_spirv_entry_point::ScanSpirvEntryPoints(
        __hidden_spirv_entry_point::SpirvWordSource{ words, wordCount },
        [](const __hidden_spirv_entry_point::SpirvEntryPointInstruction&)noexcept{}
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SpirvEntryPointLookupResult::Enum ResolveSpirvEntryPointName(
    const u32* words,
    const usize wordCount,
    const AStringView entryName,
    const ShaderType::Mask shaderType,
    AStringView& outEntryPointName
)noexcept{
    return __hidden_spirv_entry_point::ResolveEntryPointName(
        __hidden_spirv_entry_point::SpirvWordSource{ words, wordCount }, entryName, shaderType, outEntryPointName
    );
}

SpirvEntryPointLookupResult::Enum ResolveSpirvEntryPointName(
    const BinaryByteView bytecode,
    const AStringView entryName,
    const ShaderType::Mask shaderType,
    AStringView& outEntryPointName
)noexcept{
    return __hidden_spirv_entry_point::ResolveEntryPointName(
        __hidden_spirv_entry_point::SpirvByteSource{ bytecode }, entryName, shaderType, outEntryPointName
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

