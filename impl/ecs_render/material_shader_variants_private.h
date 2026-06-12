// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_types.h"

#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderMaterialShaderVariants{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_CsgEnabledDefineName = "NWB_CSG_ENABLED";
inline constexpr AStringView s_CsgEnabledDefineAssignment = "NWB_CSG_ENABLED=1";
inline constexpr AStringView s_CsgClipSetDefineName = "NWB_CSG_CLIP_SET";
inline constexpr AStringView s_CsgAvboitClipSetDefineAssignment = "NWB_CSG_CLIP_SET=2";
inline constexpr AStringView s_CsgIntervalSampleEnabledDefineName = "NWB_CSG_INTERVAL_SAMPLE_ENABLED";
inline constexpr AStringView s_CsgIntervalSampleEnabledDefineAssignment = "NWB_CSG_INTERVAL_SAMPLE_ENABLED=1";
inline constexpr AStringView s_CsgIntervalSampleSetDefineName = "NWB_CSG_INTERVAL_SAMPLE_SET";
inline constexpr AStringView s_CsgAvboitIntervalSampleSetDefineAssignment = "NWB_CSG_INTERVAL_SAMPLE_SET=3";
inline constexpr AStringView s_CsgProjectEvaluatorModuleDefineName = "NWB_CSG_PROJECT_EVALUATOR_MODULE";
inline constexpr usize s_MaxCsgClipShaderVariantDefineAssignments = 5u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShaderVariantDefineAssignment{
    AStringView name;
    AStringView assignment;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline AStringView VariantSegmentDefineName(const AStringView segment){
    const usize equalPos = segment.find('=');
    return equalPos == AStringView::npos ? AStringView{} : segment.substr(0u, equalPos);
}

[[nodiscard]] inline bool FindVariantDefineAssignment(const AStringView variant, const AStringView defineName, AStringView& outAssignment){
    outAssignment = AStringView{};
    if(variant.empty() || variant == Core::ShaderArchive::s_DefaultVariant)
        return false;

    usize begin = 0u;
    while(begin < variant.size()){
        usize segmentEnd = variant.find(';', begin);
        if(segmentEnd == AStringView::npos)
            segmentEnd = variant.size();

        const AStringView segment = variant.substr(begin, segmentEnd - begin);
        if(VariantSegmentDefineName(segment) == defineName){
            outAssignment = segment;
            return true;
        }

        begin = segmentEnd + 1u;
    }
    return false;
}

[[nodiscard]] inline bool BuildCsgClipShaderVariantName(
    const AStringView baseVariant,
    const ShaderVariantDefineAssignment* defineAssignments,
    const usize defineAssignmentCount,
    Core::GraphicsString& outVariant
){
    outVariant.clear();
    if(baseVariant.empty() || !defineAssignments || defineAssignmentCount == 0u)
        return false;
    if(defineAssignmentCount > s_MaxCsgClipShaderVariantDefineAssignments)
        return false;
    if(baseVariant == Core::ShaderArchive::s_DefaultVariant){
        bool insertedDefines[s_MaxCsgClipShaderVariantDefineAssignments] = {};
        for(usize outputIndex = 0u; outputIndex < defineAssignmentCount; ++outputIndex){
            usize selectedIndex = defineAssignmentCount;
            for(usize i = 0u; i < defineAssignmentCount; ++i){
                if(insertedDefines[i])
                    continue;
                if(selectedIndex == defineAssignmentCount || defineAssignments[i].name < defineAssignments[selectedIndex].name)
                    selectedIndex = i;
            }
            if(selectedIndex == defineAssignmentCount)
                return false;

            if(!outVariant.empty())
                outVariant += ';';
            outVariant += defineAssignments[selectedIndex].assignment;
            insertedDefines[selectedIndex] = true;
        }
        return true;
    }

    usize reserveSize = baseVariant.size();
    for(usize i = 0u; i < defineAssignmentCount; ++i)
        reserveSize += 1u + defineAssignments[i].assignment.size();
    outVariant.reserve(reserveSize);

    bool insertedDefines[s_MaxCsgClipShaderVariantDefineAssignments] = {};
    usize begin = 0u;
    while(begin < baseVariant.size()){
        usize segmentEnd = baseVariant.find(';', begin);
        if(segmentEnd == AStringView::npos)
            segmentEnd = baseVariant.size();

        const AStringView segment = baseVariant.substr(begin, segmentEnd - begin);
        const AStringView defineName = VariantSegmentDefineName(segment);
        if(defineName.empty())
            return false;

        for(usize i = 0u; i < defineAssignmentCount; ++i){
            if(insertedDefines[i] || !(defineAssignments[i].name < defineName))
                continue;

            if(!outVariant.empty())
                outVariant += ';';
            outVariant += defineAssignments[i].assignment;
            insertedDefines[i] = true;
        }
        for(usize i = 0u; i < defineAssignmentCount; ++i){
            if(defineName == defineAssignments[i].name)
                return false;
        }

        if(!outVariant.empty())
            outVariant += ';';
        outVariant += segment;
        begin = segmentEnd + 1u;
    }

    for(usize i = 0u; i < defineAssignmentCount; ++i){
        if(insertedDefines[i])
            continue;

        if(!outVariant.empty())
            outVariant += ';';
        outVariant += defineAssignments[i].assignment;
    }
    return true;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

