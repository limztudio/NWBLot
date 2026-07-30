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
        Fragment = 4u,
        GLCompute = 5u,
        MeshNV = 5268u,
        RayGenerationKHR = 5313u,
        ClosestHitKHR = 5316u,
        MissKHR = 5317u,
        MeshEXT = 5365u,
    };
};

struct SpirvEntryPointInstruction{
    ShaderType::Mask shaderType = ShaderType::None;
    AStringView name;
};


inline ShaderType::Mask ConvertExecutionModel(const u32 executionModel){
    switch(executionModel){
    case SpirvExecutionModel::Vertex: return ShaderType::Vertex;
    case SpirvExecutionModel::Fragment: return ShaderType::Pixel;
    case SpirvExecutionModel::GLCompute: return ShaderType::Compute;
    case SpirvExecutionModel::MeshNV: return ShaderType::Mesh;
    case SpirvExecutionModel::RayGenerationKHR: return ShaderType::RayGeneration;
    case SpirvExecutionModel::ClosestHitKHR: return ShaderType::ClosestHit;
    case SpirvExecutionModel::MissKHR: return ShaderType::Miss;
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

    const auto* entryPointBytes = reinterpret_cast<const char*>(&instructionWords[s_SpirvEntryPointNameWordIndex]);
    const usize entryPointMaxBytes = (static_cast<usize>(instructionWordCount) - s_SpirvEntryPointFixedWordCount) * sizeof(u32);

    usize entryPointLength = 0;
    while(entryPointLength < entryPointMaxBytes && entryPointBytes[entryPointLength] != '\0')
        ++entryPointLength;

    if(entryPointLength == entryPointMaxBytes)
        return false;

    outEntryPoint.name = AStringView(entryPointBytes, entryPointLength);
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
    GraphicsString& outEntryPointName
){
    outEntryPointName.clear();

    if(entryName.empty() || shaderType == ShaderType::None)
        return SpirvEntryPointLookupResult::NotFound;

    bool found = false;
    const bool validModule = __hidden_spirv_entry_point::ScanSpirvEntryPoints(
        words,
        wordCount,
        [&](const __hidden_spirv_entry_point::SpirvEntryPointInstruction& entryPoint){
            if(found || entryPoint.shaderType == ShaderType::None || entryPoint.shaderType != shaderType || entryPoint.name != entryName)
                return;

            outEntryPointName.assign(entryPoint.name.data(), entryPoint.name.size());
            found = true;
        }
    );
    if(!validModule){
        outEntryPointName.clear();
        return SpirvEntryPointLookupResult::InvalidSpirv;
    }

    return found ? SpirvEntryPointLookupResult::Found : SpirvEntryPointLookupResult::NotFound;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

