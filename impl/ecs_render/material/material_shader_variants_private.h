// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderMaterialShaderVariants{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_CsgEnabledDefineName = "NWB_CSG_ENABLED";
inline constexpr AStringView s_CsgEnabledDefineAssignment = "NWB_CSG_ENABLED=1";
inline constexpr AStringView s_CsgIntervalSampleEnabledDefineName = "NWB_CSG_INTERVAL_SAMPLE_ENABLED";
inline constexpr AStringView s_CsgIntervalSampleEnabledDefineAssignment = "NWB_CSG_INTERVAL_SAMPLE_ENABLED=1";
inline constexpr AStringView s_CsgProjectEvaluatorModuleDefineName = "NWB_CSG_PROJECT_EVALUATOR_MODULE";
inline constexpr usize s_MaxCsgClipShaderVariantDefineAssignments = 3u;


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

[[nodiscard]] inline Expected<AStringView> FindVariantDefineAssignment(const AStringView variant, const AStringView defineName)noexcept{
    if(variant.empty() || variant == Core::ShaderArchive::s_DefaultVariant)
        return MakeUnexpected(Failure{});

    usize begin = 0u;
    while(begin < variant.size()){
        usize segmentEnd = variant.find(';', begin);
        if(segmentEnd == AStringView::npos)
            segmentEnd = variant.size();

        const AStringView segment = variant.substr(begin, segmentEnd - begin);
        if(VariantSegmentDefineName(segment) == defineName){
            return segment;
        }

        begin = segmentEnd + 1u;
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] inline Expected<Core::GraphicsString> BuildCsgClipShaderVariantName(
    Core::GraphicsArena& arena,
    const AStringView baseVariant,
    const ShaderVariantDefineAssignment* defineAssignments,
    const usize defineAssignmentCount
){
    Core::GraphicsString variant(arena);
    if(baseVariant.empty() || !defineAssignments || defineAssignmentCount == 0u)
        return MakeUnexpected(Failure{});
    if(defineAssignmentCount > s_MaxCsgClipShaderVariantDefineAssignments)
        return MakeUnexpected(Failure{});
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
                return MakeUnexpected(Failure{});

            if(!variant.empty())
                variant += ';';
            variant += defineAssignments[selectedIndex].assignment;
            insertedDefines[selectedIndex] = true;
        }
        return variant;
    }

    usize reserveSize = baseVariant.size();
    for(usize i = 0u; i < defineAssignmentCount; ++i)
        reserveSize += 1u + defineAssignments[i].assignment.size();
    variant.reserve(reserveSize);

    bool insertedDefines[s_MaxCsgClipShaderVariantDefineAssignments] = {};
    usize begin = 0u;
    while(begin < baseVariant.size()){
        usize segmentEnd = baseVariant.find(';', begin);
        if(segmentEnd == AStringView::npos)
            segmentEnd = baseVariant.size();

        const AStringView segment = baseVariant.substr(begin, segmentEnd - begin);
        const AStringView defineName = VariantSegmentDefineName(segment);
        if(defineName.empty())
            return MakeUnexpected(Failure{});

        for(usize i = 0u; i < defineAssignmentCount; ++i){
            if(insertedDefines[i] || !(defineAssignments[i].name < defineName))
                continue;

            if(!variant.empty())
                variant += ';';
            variant += defineAssignments[i].assignment;
            insertedDefines[i] = true;
        }
        for(usize i = 0u; i < defineAssignmentCount; ++i){
            if(defineName == defineAssignments[i].name)
                return MakeUnexpected(Failure{});
        }

        if(!variant.empty())
            variant += ';';
        variant += segment;
        begin = segmentEnd + 1u;
    }

    for(usize i = 0u; i < defineAssignmentCount; ++i){
        if(insertedDefines[i])
            continue;

        if(!variant.empty())
            variant += ';';
        variant += defineAssignments[i].assignment;
    }
    return variant;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

