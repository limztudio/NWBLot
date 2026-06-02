// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_cap_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderCsgCapDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_cap_triangulation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void BuildProjectionBasis(const SIMDVector capNormal, SIMDVector& outU, SIMDVector& outV){
    const f32 normalY = Abs(VectorGetY(capNormal));
    const SIMDVector reference = normalY < 0.9f
        ? VectorSet(0.0f, 1.0f, 0.0f, 0.0f)
        : VectorSet(1.0f, 0.0f, 0.0f, 0.0f)
    ;
    outU = NormalizeVector3Or(Vector3Cross(reference, capNormal), VectorSet(1.0f, 0.0f, 0.0f, 0.0f));
    outV = NormalizeVector3Or(Vector3Cross(capNormal, outU), VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
}

[[nodiscard]] static f32 PolygonSignedArea(const CapProjectedPointVector& points, const CapIndexVector& polygon){
    f32 area = 0.0f;
    for(usize index = 0u; index < polygon.size(); ++index){
        const CapProjectedPoint& a = points[polygon[index]];
        const CapProjectedPoint& b = points[polygon[(index + 1u) % polygon.size()]];
        area += a.u * b.v - b.u * a.v;
    }
    return area * 0.5f;
}

static void ReversePolygon(CapIndexVector& polygon){
    if(polygon.size() < 2u)
        return;

    usize left = 0u;
    usize right = polygon.size() - 1u;
    while(left < right){
        Swap(polygon[left], polygon[right]);
        ++left;
        --right;
    }
}

[[nodiscard]] static f32 Cross2(const CapProjectedPoint& a, const CapProjectedPoint& b, const CapProjectedPoint& c){
    return (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
}

[[nodiscard]] static bool PointInTriangle2(
    const CapProjectedPoint& p,
    const CapProjectedPoint& a,
    const CapProjectedPoint& b,
    const CapProjectedPoint& c
){
    const f32 ab = Cross2(a, b, p);
    const f32 bc = Cross2(b, c, p);
    const f32 ca = Cross2(c, a, p);
    return ab >= -s_PointMergeEpsilon && bc >= -s_PointMergeEpsilon && ca >= -s_PointMergeEpsilon;
}

[[nodiscard]] static bool AppendFanTriangulation(
    CsgPlaneCapVertexGpuDataVector& vertices,
    const CapPointVector& points,
    const CapProjectedPointVector& projected,
    const CapIndexVector& polygon,
    const CapCutterEval& cutterEval,
    const u32 receiverIndex,
    const u32 cutterIndex
){
    if(polygon.size() < 3u)
        return true;

    for(usize index = 1u; index + 1u < polygon.size(); ++index){
        if(!AppendCapTriangle(
            vertices,
            points,
            cutterEval,
            projected[polygon[0u]].pointIndex,
            projected[polygon[index]].pointIndex,
            projected[polygon[index + 1u]].pointIndex,
            receiverIndex,
            cutterIndex
        ))
            return false;
    }
    return true;
}

[[nodiscard]] static bool BuildLoopProjection(
    const CapPointVector& points,
    const CapIndexVector& loop,
    const CapCutterEval& cutterEval,
    CapProjectedPointVector& outProjected
){
    outProjected.clear();
    outProjected.reserve(loop.size());
    if(loop.empty())
        return true;

    const CapPoint& firstPoint = points[loop[0u]];
    const SIMDVector firstPosition = LoadFloat(firstPoint.position);
    const SIMDVector firstNormal = EvaluateWorldCapNormal(cutterEval, firstPosition, LoadFloat(firstPoint.normal));

    SIMDVector polygonNormal = VectorZero();
    for(usize index = 0u; index < loop.size(); ++index){
        const SIMDVector a = LoadFloat(points[loop[index]].position);
        const SIMDVector b = LoadFloat(points[loop[(index + 1u) % loop.size()]].position);
        polygonNormal = VectorAdd(polygonNormal, Vector3Cross(a, b));
    }
    polygonNormal = NormalizeVector3Or(polygonNormal, firstNormal);
    if(VectorGetX(Vector3Dot(polygonNormal, firstNormal)) < 0.0f)
        polygonNormal = VectorNegate(polygonNormal);

    SIMDVector basisU;
    SIMDVector basisV;
    BuildProjectionBasis(polygonNormal, basisU, basisV);

    for(const u32 pointIndex : loop){
        const SIMDVector position = LoadFloat(points[pointIndex].position);
        outProjected.push_back(CapProjectedPoint{
            pointIndex,
            VectorGetX(Vector3Dot(position, basisU)),
            VectorGetX(Vector3Dot(position, basisV)),
        });
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AppendEarClippedTriangulation(
    CsgPlaneCapVertexGpuDataVector& vertices,
    const CapPointVector& points,
    const CapIndexVector& loop,
    const CapCutterEval& cutterEval,
    const u32 receiverIndex,
    const u32 cutterIndex,
    Core::Alloc::ScratchArena& scratchArena
){
    if(loop.size() < 3u)
        return true;

    CapProjectedPointVector projected(scratchArena);
    if(!__hidden_csg_cap_triangulation::BuildLoopProjection(points, loop, cutterEval, projected))
        return false;

    CapIndexVector polygon(scratchArena);
    polygon.reserve(projected.size());
    for(usize index = 0u; index < projected.size(); ++index)
        polygon.push_back(static_cast<u32>(index));
    if(__hidden_csg_cap_triangulation::PolygonSignedArea(projected, polygon) < 0.0f)
        __hidden_csg_cap_triangulation::ReversePolygon(polygon);

    usize guard = 0u;
    while(polygon.size() > 3u && guard < loop.size() * loop.size()){
        bool clippedEar = false;
        for(usize index = 0u; index < polygon.size(); ++index){
            const u32 previousIndex = polygon[(index + polygon.size() - 1u) % polygon.size()];
            const u32 currentIndex = polygon[index];
            const u32 nextIndex = polygon[(index + 1u) % polygon.size()];
            const CapProjectedPoint& previous = projected[previousIndex];
            const CapProjectedPoint& current = projected[currentIndex];
            const CapProjectedPoint& next = projected[nextIndex];
            if(__hidden_csg_cap_triangulation::Cross2(previous, current, next) <= s_PointMergeEpsilon)
                continue;

            bool containsPoint = false;
            for(const u32 testIndex : polygon){
                if(testIndex == previousIndex || testIndex == currentIndex || testIndex == nextIndex)
                    continue;
                if(__hidden_csg_cap_triangulation::PointInTriangle2(projected[testIndex], previous, current, next)){
                    containsPoint = true;
                    break;
                }
            }
            if(containsPoint)
                continue;

            if(!AppendCapTriangle(
                vertices,
                points,
                cutterEval,
                previous.pointIndex,
                current.pointIndex,
                next.pointIndex,
                receiverIndex,
                cutterIndex
            ))
                return false;

            polygon.erase(polygon.begin() + static_cast<isize>(index));
            clippedEar = true;
            break;
        }

        if(!clippedEar)
            return __hidden_csg_cap_triangulation::AppendFanTriangulation(vertices, points, projected, polygon, cutterEval, receiverIndex, cutterIndex);
        ++guard;
    }

    if(polygon.size() == 3u)
        return AppendCapTriangle(
            vertices,
            points,
            cutterEval,
            projected[polygon[0u]].pointIndex,
            projected[polygon[1u]].pointIndex,
            projected[polygon[2u]].pointIndex,
            receiverIndex,
            cutterIndex
        );

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

