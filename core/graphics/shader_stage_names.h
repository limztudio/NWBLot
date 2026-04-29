// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderStageNames{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline const Name& ArchiveStageNameFromShaderType(const ShaderType::Mask shaderType){
    switch(shaderType){
        case Core::ShaderType::Vertex: { static const Name s("vs"); return s; }
        case Core::ShaderType::Hull: { static const Name s("hs"); return s; }
        case Core::ShaderType::Domain: { static const Name s("ds"); return s; }
        case Core::ShaderType::Geometry: { static const Name s("gs"); return s; }
        case Core::ShaderType::Pixel: { static const Name s("ps"); return s; }
        case Core::ShaderType::Compute: { static const Name s("cs"); return s; }
        case Core::ShaderType::Amplification: { static const Name s("task"); return s; }
        case Core::ShaderType::Mesh: { static const Name s("mesh"); return s; }
        case Core::ShaderType::RayGeneration: { static const Name s("rgen"); return s; }
        case Core::ShaderType::AnyHit: { static const Name s("rahit"); return s; }
        case Core::ShaderType::ClosestHit: { static const Name s("rchit"); return s; }
        case Core::ShaderType::Miss: { static const Name s("rmiss"); return s; }
        case Core::ShaderType::Intersection: { static const Name s("rint"); return s; }
        case Core::ShaderType::Callable: { static const Name s("rcall"); return s; }
        default: return NAME_NONE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

