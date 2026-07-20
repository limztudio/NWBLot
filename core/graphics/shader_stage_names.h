#pragma once


#include "api.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderStageNames{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SHADER_STAGE_NAME_ENTRIES(Entry)                                                                                  \
    Entry(VertexStage, "vs")                                                                                                  \
    Entry(HullStage, "hs")                                                                                                    \
    Entry(DomainStage, "ds")                                                                                                  \
    Entry(GeometryStage, "gs")                                                                                                \
    Entry(PixelStage, "ps")                                                                                                   \
    Entry(ComputeStage, "cs")                                                                                                 \
    Entry(AmplificationStage, "task")                                                                                         \
    Entry(MeshStage, "mesh")                                                                                                  \
    Entry(RayGenerationStage, "rgen")                                                                                         \
    Entry(AnyHitStage, "rahit")                                                                                               \
    Entry(ClosestHitStage, "rchit")                                                                                           \
    Entry(MissStage, "rmiss")                                                                                                 \
    Entry(IntersectionStage, "rint")                                                                                          \
    Entry(CallableStage, "rcall")

#define NWB_SHADER_STAGE_NAME_CONSTANT(Stage, Text) inline constexpr Name s_##Stage##Name(Text);
NWB_SHADER_STAGE_NAME_ENTRIES(NWB_SHADER_STAGE_NAME_CONSTANT)
#undef NWB_SHADER_STAGE_NAME_CONSTANT

inline const Name& ArchiveStageNameFromShaderType(const ShaderType::Enum shaderType){
    switch(shaderType){
#define NWB_SHADER_STAGE_NAME_CASE(Stage, Text) case Core::ShaderType::Stage: return s_##Stage##Name;
        NWB_SHADER_STAGE_NAME_ENTRIES(NWB_SHADER_STAGE_NAME_CASE)
#undef NWB_SHADER_STAGE_NAME_CASE
        default: return NAME_NONE;
    }
}

inline const Name& ArchiveStageNameFromShaderType(const ShaderType::Mask shaderType){
    return ArchiveStageNameFromShaderType(ShaderType::ToEnum(shaderType));
}

inline ShaderType::Enum ShaderTypeFromArchiveStageName(const Name& stageName){
#define NWB_SHADER_STAGE_NAME_MATCH(Stage, Text)                                                                              \
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::Stage))                                                        \
        return ShaderType::Stage;
    NWB_SHADER_STAGE_NAME_ENTRIES(NWB_SHADER_STAGE_NAME_MATCH)
#undef NWB_SHADER_STAGE_NAME_MATCH

    return ShaderType::Invalid;
}

#undef NWB_SHADER_STAGE_NAME_ENTRIES


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

