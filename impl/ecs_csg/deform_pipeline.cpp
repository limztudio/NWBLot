// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_pipeline.h"

#include "deform_cap_builder.h"
#include "deform_validator.h"
#include "deform_wall_builder.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

using ScratchArena = Core::Alloc::ScratchArena;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CsgDeformPipeline::RebuildSequentialCuts(
    ScratchArena& scratchArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    const usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<ScratchArena>& outVertices,
    CsgDeformTriangleVector<ScratchArena>& outTriangles,
    CsgDeformPipelineResult& outResult
){
    outResult = CsgDeformPipelineResult{};
    outVertices.clear();
    outTriangles.clear();
    outResult.stats.inputVertexCount = static_cast<u32>(inputVertexCount);
    outResult.stats.inputTriangleCount = static_cast<u32>(inputTriangleCount);
    if(inputVertexCount == 0u || inputTriangleCount == 0u){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::EmptyInput;
        return false;
    }
    if(inputVertexCount > s_MaxDeformVertices || inputTriangleCount > s_MaxDeformTriangles){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::TooLarge;
        return false;
    }
    if(!CsgDeformValidator::ValidOptions(options)){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::InvalidCutter;
        return false;
    }
    CsgDeformViabilityReason::Enum inputReason = CsgDeformViabilityReason::Ok;
    if(!CsgDeformValidator::FiniteInput(inputVertices, inputVertexCount, inputReason)){
        outResult.viability.viable = false;
        outResult.viability.reason = inputReason;
        return false;
    }
    if(!CsgDeformValidator::ValidTopology(inputVertices, inputVertexCount, inputTriangles, inputTriangleCount)){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::InvalidTopology;
        return false;
    }

    const f32 epsilon = CsgDeformValidator::ShapeEpsilon(options);
    outVertices.reserve(inputVertexCount + cutCount * 16u);
    outTriangles.reserve(inputTriangleCount * 2u + cutCount * 16u);
    for(usize vertexIndex = 0u; vertexIndex < inputVertexCount; ++vertexIndex)
        outVertices.push_back(inputVertices.get()[vertexIndex]);
    for(usize triangleIndex = 0u; triangleIndex < inputTriangleCount; ++triangleIndex)
        outTriangles.push_back(inputTriangles.get()[triangleIndex]);

    CsgDeformTriangleVector<ScratchArena> scratchKept(scratchArena);
    Vector<f32, ScratchArena> scratchDistances(scratchArena);
    Vector<CsgDeformCutLoopEdge, ScratchArena> scratchEdges(scratchArena);

    u32 appliedCuts = 0u;
    u32 capTriangles = 0u;
    if(cuts){
        for(usize cutIndex = 0u; cutIndex < cutCount; ++cutIndex){
            const CsgDeformCutDesc& cut = cuts[cutIndex];
            if(!cut.active)
                continue;
            CsgDeformViabilityReason::Enum cutReason = CsgDeformViabilityReason::Ok;
            if(!CsgDeformWallBuilder::ClipShell(scratchArena, cut.shape, epsilon, outVertices, outTriangles, scratchKept, scratchDistances, cutReason)){
                outResult.viability.viable = false;
                outResult.viability.reason = cutReason == CsgDeformViabilityReason::Ok
                    ? CsgDeformViabilityReason::NoKeptGeometry
                    : cutReason
                ;
                return false;
            }
            ++appliedCuts;
            if(outTriangles.empty() || outVertices.size() > s_MaxDeformVertices || outTriangles.size() > s_MaxDeformTriangles){
                outResult.viability.viable = false;
                outResult.viability.reason = outTriangles.empty()
                    ? CsgDeformViabilityReason::NoKeptGeometry
                    : CsgDeformViabilityReason::TooLarge
                ;
                return false;
            }
            if(options.fillCaps){
                u32 cutCaps = 0u;
                if(!CsgDeformCapBuilder::FillCutCaps(scratchArena, outVertices, outTriangles, scratchEdges, cutCaps)){
                    outResult.viability.viable = false;
                    outResult.viability.reason = CsgDeformViabilityReason::CapLoopFailed;
                    return false;
                }
                capTriangles += cutCaps;
            }
        }
    }

    if(outVertices.empty() || outTriangles.empty()){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::NoKeptGeometry;
        return false;
    }
    for(const CsgDeformVertex& vertex : outVertices){
        if(!CsgDeformValidator::FiniteVertex(vertex)){
            outResult.viability.viable = false;
            outResult.viability.reason = CsgDeformViabilityReason::NonFiniteInput;
            return false;
        }
    }
    outResult.viability.viable = true;
    outResult.viability.reason = CsgDeformViabilityReason::Ok;
    outResult.stats.outputVertexCount = static_cast<u32>(outVertices.size());
    outResult.stats.outputTriangleCount = static_cast<u32>(outTriangles.size());
    outResult.stats.appliedCutCount = appliedCuts;
    outResult.stats.capTriangleCount = capTriangles;
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

