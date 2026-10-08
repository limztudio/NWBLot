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


Expected<CsgDeformStats, CsgDeformFailure> CsgDeformPipeline::RebuildSequentialCuts(
    ScratchArena& scratchArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    const usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<ScratchArena>& outVertices,
    CsgDeformTriangleVector<ScratchArena>& outTriangles
){
    CsgDeformStats stats;
    outVertices.clear();
    outTriangles.clear();
    stats.inputVertexCount = static_cast<u32>(inputVertexCount);
    stats.inputTriangleCount = static_cast<u32>(inputTriangleCount);
    if(inputVertexCount == 0u || inputTriangleCount == 0u){
        return MakeUnexpected(CsgDeformFailure{ CsgDeformViabilityReason::EmptyInput, stats });
    }
    if(inputVertexCount > s_MaxDeformVertices || inputTriangleCount > s_MaxDeformTriangles){
        return MakeUnexpected(CsgDeformFailure{ CsgDeformViabilityReason::TooLarge, stats });
    }
    if(!CsgDeformValidator::ValidOptions(options)){
        return MakeUnexpected(CsgDeformFailure{ CsgDeformViabilityReason::InvalidCutter, stats });
    }
    const auto finiteInput = CsgDeformValidator::FiniteInput(inputVertices, inputVertexCount);
    if(!finiteInput){
        return MakeUnexpected(CsgDeformFailure{ finiteInput.error(), stats });
    }
    if(!CsgDeformValidator::ValidTopology(inputTriangles, inputTriangleCount, inputVertexCount)){
        return MakeUnexpected(CsgDeformFailure{ CsgDeformViabilityReason::InvalidTopology, stats });
    }

    const f32 epsilon = CsgDeformValidator::ShapeEpsilon(options);
    outVertices.reserve(inputVertexCount + cutCount * s_RebuildReservePerCut);
    outTriangles.reserve(inputTriangleCount * s_KeptReserveMultiplier + cutCount * s_RebuildReservePerCut);
    for(usize vertexIndex = 0u; vertexIndex < inputVertexCount; ++vertexIndex)
        outVertices.push_back(inputVertices.get()[vertexIndex]);
    for(usize triangleIndex = 0u; triangleIndex < inputTriangleCount; ++triangleIndex)
        outTriangles.push_back(inputTriangles.get()[triangleIndex]);

    CsgDeformTriangleVector<ScratchArena> scratchKept(scratchArena);
    Vector<f32, ScratchArena> scratchDistances(scratchArena);
    Vector<CsgDeformCutLoopEdge, ScratchArena> scratchEdges(scratchArena);

    u32 appliedCuts = 0u;
    u32 capTriangles = 0u;
    NWB_ASSERT(cuts != nullptr || cutCount == 0u);
    for(usize cutIndex = 0u; cutIndex < cutCount; ++cutIndex){
        const CsgDeformCutDesc& cut = cuts[cutIndex];
        if(!cut.active)
            continue;
        const auto clipped = CsgDeformWallBuilder::ClipShell(scratchArena, cut.shape, epsilon, outVertices, outTriangles, scratchKept, scratchDistances);
        if(!clipped){
            return MakeUnexpected(CsgDeformFailure{ clipped.error(), stats });
        }
        ++appliedCuts;
        if(outTriangles.empty() || outVertices.size() > s_MaxDeformVertices || outTriangles.size() > s_MaxDeformTriangles){
            const auto reason = outTriangles.empty() ? CsgDeformViabilityReason::NoKeptGeometry : CsgDeformViabilityReason::TooLarge;
            return MakeUnexpected(CsgDeformFailure{ reason, stats });
        }
        if(options.fillCaps){
            const auto cutCaps = CsgDeformCapBuilder::FillCutCaps(scratchArena, outVertices, outTriangles, scratchEdges);
            if(!cutCaps){
                return MakeUnexpected(CsgDeformFailure{ CsgDeformViabilityReason::CapLoopFailed, stats });
            }
            capTriangles += *cutCaps;
        }
    }

    if(outVertices.empty() || outTriangles.empty()){
        return MakeUnexpected(CsgDeformFailure{ CsgDeformViabilityReason::NoKeptGeometry, stats });
    }
    for(const CsgDeformVertex& vertex : outVertices){
        if(!CsgDeformValidator::FiniteVertex(vertex)){
            return MakeUnexpected(CsgDeformFailure{ CsgDeformViabilityReason::NonFiniteInput, stats });
        }
    }
    stats.outputVertexCount = static_cast<u32>(outVertices.size());
    stats.outputTriangleCount = static_cast<u32>(outTriangles.size());
    stats.appliedCutCount = appliedCuts;
    stats.capTriangleCount = capTriangles;
    return stats;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

