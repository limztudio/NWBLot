// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderStageNames{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline const Name& ArchiveStageNameFromShaderType(const ShaderType::Enum shaderType){
    switch(shaderType){
        case Core::ShaderType::VertexStage: { static const Name s("vs"); return s; }
        case Core::ShaderType::HullStage: { static const Name s("hs"); return s; }
        case Core::ShaderType::DomainStage: { static const Name s("ds"); return s; }
        case Core::ShaderType::GeometryStage: { static const Name s("gs"); return s; }
        case Core::ShaderType::PixelStage: { static const Name s("ps"); return s; }
        case Core::ShaderType::ComputeStage: { static const Name s("cs"); return s; }
        case Core::ShaderType::AmplificationStage: { static const Name s("task"); return s; }
        case Core::ShaderType::MeshStage: { static const Name s("mesh"); return s; }
        case Core::ShaderType::RayGenerationStage: { static const Name s("rgen"); return s; }
        case Core::ShaderType::AnyHitStage: { static const Name s("rahit"); return s; }
        case Core::ShaderType::ClosestHitStage: { static const Name s("rchit"); return s; }
        case Core::ShaderType::MissStage: { static const Name s("rmiss"); return s; }
        case Core::ShaderType::IntersectionStage: { static const Name s("rint"); return s; }
        case Core::ShaderType::CallableStage: { static const Name s("rcall"); return s; }
        default: return NAME_NONE;
    }
}

inline const Name& ArchiveStageNameFromShaderType(const ShaderType::Mask shaderType){
    return ArchiveStageNameFromShaderType(ShaderType::ToEnum(shaderType));
}

inline ShaderType::Enum ShaderTypeFromArchiveStageName(const Name& stageName){
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::VertexStage))
        return ShaderType::VertexStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::HullStage))
        return ShaderType::HullStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::DomainStage))
        return ShaderType::DomainStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::GeometryStage))
        return ShaderType::GeometryStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::PixelStage))
        return ShaderType::PixelStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::ComputeStage))
        return ShaderType::ComputeStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::AmplificationStage))
        return ShaderType::AmplificationStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::MeshStage))
        return ShaderType::MeshStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::RayGenerationStage))
        return ShaderType::RayGenerationStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::AnyHitStage))
        return ShaderType::AnyHitStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::ClosestHitStage))
        return ShaderType::ClosestHitStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::MissStage))
        return ShaderType::MissStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::IntersectionStage))
        return ShaderType::IntersectionStage;
    if(stageName == ArchiveStageNameFromShaderType(ShaderType::CallableStage))
        return ShaderType::CallableStage;

    return ShaderType::Invalid;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

