// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "spirv_entry_point.h"


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


inline ShaderType::Mask ConvertExecutionModel(const u32 executionModel){
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

[[nodiscard]] inline bool DecodeEntryPointInstruction(
    const u32* instructionWords,
    const u16 instructionWordCount,
    SpirvEntryPointInstruction& outEntryPoint
){
    outEntryPoint = SpirvEntryPointInstruction();

    if(instructionWordCount <= s_SpirvEntryPointFixedWordCount)
        return false;

    outEntryPoint.shaderType = ConvertExecutionModel(instructionWords[s_SpirvEntryPointExecutionModelWordIndex]);

    const AStringView entryPointBytes(
        reinterpret_cast<const char*>(&instructionWords[s_SpirvEntryPointNameWordIndex]),
        (static_cast<usize>(instructionWordCount) - s_SpirvEntryPointFixedWordCount) * sizeof(u32)
    );
    const usize entryPointLength = entryPointBytes.find('\0');
    if(entryPointLength == AStringView::npos)
        return false;

    outEntryPoint.name = entryPointBytes.substr(0u, entryPointLength);
    return true;
}

template<typename EntryPointCallback>
[[nodiscard]] bool ScanSpirvEntryPoints(
    const u32* words,
    const usize wordCount,
    EntryPointCallback entryPointCallback
){
    if(!words || wordCount < s_SpirvHeaderWords)
        return false;

    if(words[s_SpirvMagicWordIndex] != s_SpirvMagic)
        return false;

    for(usize instructionIndex = s_SpirvHeaderWords; instructionIndex < wordCount; ){
        const u32 instruction = words[instructionIndex];
        const u16 opcode = static_cast<u16>(instruction & s_SpirvInstructionOpcodeBitMask);
        const u16 instructionWordCount = static_cast<u16>(instruction >> s_SpirvInstructionWordCountBitShift);
        if(instructionWordCount == 0)
            return false;

        if(static_cast<usize>(instructionWordCount) > wordCount - instructionIndex)
            return false;

        if(opcode == s_OpEntryPoint){
            SpirvEntryPointInstruction entryPoint;
            if(!DecodeEntryPointInstruction(words + instructionIndex, instructionWordCount, entryPoint))
                return false;

            entryPointCallback(entryPoint);
        }

        instructionIndex += instructionWordCount;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsValidSpirvModuleWords(
    const u32* words,
    const usize wordCount
){
    return __hidden_spirv_entry_point::ScanSpirvEntryPoints(
        words,
        wordCount,
        [](const __hidden_spirv_entry_point::SpirvEntryPointInstruction&){}
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SpirvEntryPointLookupResult::Enum ResolveSpirvEntryPointName(
    const u32* words,
    const usize wordCount,
    const AStringView entryName,
    const ShaderType::Mask shaderType,
    AStringView& outEntryPointName
){
    outEntryPointName = {};

    if(entryName.empty() || shaderType == ShaderType::None)
        return SpirvEntryPointLookupResult::NotFound;

    bool found = false;
    const bool validModule = __hidden_spirv_entry_point::ScanSpirvEntryPoints(
        words,
        wordCount,
        [&](const __hidden_spirv_entry_point::SpirvEntryPointInstruction& entryPoint){
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


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

