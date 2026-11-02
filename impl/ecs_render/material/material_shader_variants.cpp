// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/material/material_shader_variants_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderMaterialShaderVariants{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GraphicsString> BuildCsgClipShaderVariantName(Core::GraphicsArena& arena, const AStringView baseVariant, const Span<const ShaderVariantDefineAssignment> defineAssignments){
    Core::GraphicsString variant(arena);
    const usize defineAssignmentCount = defineAssignments.size();
    if(baseVariant.empty() || defineAssignmentCount == 0u)
        return MakeUnexpected(Failure{});
    if(defineAssignmentCount > s_MaxCsgClipShaderVariantDefineAssignments)
        return MakeUnexpected(Failure{});
    if(baseVariant == Core::ShaderArchive::s_DefaultVariant){
        Array<b8, s_MaxCsgClipShaderVariantDefineAssignments> insertedDefines = {};
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
                variant.push_back(';');
            variant += defineAssignments[selectedIndex].assignment;
            insertedDefines[selectedIndex] = true;
        }
        return variant;
    }

    usize reserveSize = baseVariant.size();
    for(usize i = 0u; i < defineAssignmentCount; ++i)
        reserveSize += 1u + defineAssignments[i].assignment.size();
    variant.reserve(reserveSize);

    Array<b8, s_MaxCsgClipShaderVariantDefineAssignments> insertedDefines = {};
    usize begin = 0u;
    while(begin < baseVariant.size()){
        usize segmentEnd = baseVariant.find(';', begin);
        if(segmentEnd == AStringView::npos)
            segmentEnd = baseVariant.size();

        const AStringView segment = baseVariant.substr(begin, segmentEnd - begin);
        const AStringView defineName = VariantSegmentDefineName(segment);
        if(defineName.empty())
            return MakeUnexpected(Failure{});

        for(;;){
            usize selectedIndex = defineAssignmentCount;
            for(usize i = 0u; i < defineAssignmentCount; ++i){
                if(insertedDefines[i])
                    continue;
                if(defineName > defineAssignments[i].name && (selectedIndex == defineAssignmentCount || defineAssignments[i].name < defineAssignments[selectedIndex].name))
                    selectedIndex = i;
            }
            if(selectedIndex == defineAssignmentCount)
                break;
            if(!variant.empty())
                variant.push_back(';');
            variant += defineAssignments[selectedIndex].assignment;
            insertedDefines[selectedIndex] = true;
        }

        b8 replacedExistingSegment = false;
        for(usize i = 0u; i < defineAssignmentCount; ++i){
            if(defineName != defineAssignments[i].name)
                continue;
            if(!variant.empty())
                variant.push_back(';');
            variant += defineAssignments[i].assignment;
            insertedDefines[i] = true;
            replacedExistingSegment = true;
            break;
        }
        if(!replacedExistingSegment){
            if(!variant.empty())
                variant.push_back(';');
            variant += segment;
        }

        begin = segmentEnd + 1u;
    }

    for(;;){
        usize selectedIndex = defineAssignmentCount;
        for(usize i = 0u; i < defineAssignmentCount; ++i){
            if(insertedDefines[i])
                continue;
            if(selectedIndex == defineAssignmentCount || defineAssignments[i].name < defineAssignments[selectedIndex].name)
                selectedIndex = i;
        }
        if(selectedIndex == defineAssignmentCount)
            break;
        if(!variant.empty())
            variant.push_back(';');
        variant += defineAssignments[selectedIndex].assignment;
        insertedDefines[selectedIndex] = true;
    }
    return variant;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

