// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <global/basic_string.h>
#include <global/name.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialShaderStageNames{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MeshComputeArchiveStageName("mesh_compute");
inline constexpr Name s_MeshObjectVertexArchiveStageName("mesh_object_vertex");
inline constexpr Name s_MeshArchiveStageName("mesh");


inline constexpr AStringView s_MeshArchiveStageText = "mesh";
inline constexpr AStringView s_VertexArchiveStageText = "vs";
inline constexpr AStringView s_PixelArchiveStageText = "ps";
inline constexpr AStringView s_ComputeArchiveStageText = "cs";
inline constexpr AStringView s_RayGenerationArchiveStageText = "rgen";
inline constexpr AStringView s_RayAnyHitArchiveStageText = "rahit";
inline constexpr AStringView s_RayClosestHitArchiveStageText = "rchit";
inline constexpr AStringView s_RayMissArchiveStageText = "rmiss";
inline constexpr AStringView s_RayIntersectionArchiveStageText = "rint";
inline constexpr AStringView s_RayCallableArchiveStageText = "rcall";
inline constexpr AStringView s_Spirv15TargetProfileText = "spirv_1_5";
inline constexpr AStringView s_Spirv15RayQueryTargetProfileText = "spirv_1_5+spvrayquerykhr";
inline constexpr AStringView s_SpvRayQueryCapabilityText = "spvRayQueryKHR";
inline constexpr TStringView s_MeshArchiveStageLabel = NWB_TEXT("mesh");

inline AStringView MeshComputeArchiveStageText(){
    static constexpr AStringView s_StageText = "mesh_compute";
    return s_StageText;
}

inline AStringView MeshComputeImplicitDefineText(){
    static constexpr AStringView s_DefineText = "NWB_MESH_SHADER_EMULATION_COMPUTE";
    return s_DefineText;
}

inline AStringView MeshObjectVertexArchiveStageText(){
    static constexpr AStringView s_StageText = "mesh_object_vertex";
    return s_StageText;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

