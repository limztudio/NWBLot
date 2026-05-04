// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_operator_shape.h"

#include <core/alloc/scratch.h>
#include <impl/assets_geometry/geometry_asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_operator_shape{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_OperatorFootprintPlaneEpsilon = 0.0001f;
static constexpr f32 s_OperatorFootprintPointEpsilon = 0.00001f;
static constexpr f32 s_OperatorFootprintPointEpsilonSq = s_OperatorFootprintPointEpsilon * s_OperatorFootprintPointEpsilon;
static constexpr f32 s_OperatorFootprintAreaEpsilon = 0.000001f;
static constexpr f32 s_OperatorProfileScaleEpsilon = 0.000001f;
static constexpr f32 s_MaxOperatorProfileScale = 16.0f;

struct OperatorFootprintPoint{
    f32 x = 0.0f;
    f32 y = 0.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FiniteOperatorPosition(const Float3U& position){
    return IsFinite(position.x) && IsFinite(position.y) && IsFinite(position.z);
}

[[nodiscard]] f32 OperatorFootprintCross(
    const OperatorFootprintPoint& a,
    const OperatorFootprintPoint& b,
    const OperatorFootprintPoint& c
){
    return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

[[nodiscard]] f32 OperatorFootprintDistanceSq(
    const OperatorFootprintPoint& a,
    const OperatorFootprintPoint& b
){
    const f32 dx = b.x - a.x;
    const f32 dy = b.y - a.y;
    return (dx * dx) + (dy * dy);
}

[[nodiscard]] bool AppendOperatorFootprintPoint(
    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>>& points,
    const f32 x,
    const f32 y
){
    if(!IsFinite(x) || !IsFinite(y))
        return false;

    points.push_back(OperatorFootprintPoint{ x, y });
    return true;
}

template<typename PointAllocator>
void CompactSortedOperatorFootprintPoints(Vector<OperatorFootprintPoint, PointAllocator>& points){
    usize uniqueCount = 0u;
    for(const OperatorFootprintPoint& point : points){
        bool duplicate = false;
        for(usize uniqueIndex = uniqueCount; uniqueIndex > 0u; --uniqueIndex){
            const OperatorFootprintPoint& existing = points[uniqueIndex - 1u];
            if(point.x - existing.x > s_OperatorFootprintPointEpsilon)
                break;
            if(OperatorFootprintDistanceSq(existing, point) <= s_OperatorFootprintPointEpsilonSq){
                duplicate = true;
                break;
            }
        }
        if(duplicate)
            continue;

        points[uniqueCount] = point;
        ++uniqueCount;
    }
    points.resize(uniqueCount);
}

[[nodiscard]] bool AppendOperatorFootprintHullPoint(
    DeformableOperatorFootprint& footprint,
    const OperatorFootprintPoint& point
){
    if(footprint.vertexCount >= s_DeformableOperatorFootprintMaxVertexCount)
        return false;

    footprint.vertices[footprint.vertexCount] = Float2U(point.x, point.y);
    ++footprint.vertexCount;
    return true;
}

template<typename PointAllocator>
[[nodiscard]] const OperatorFootprintPoint& OperatorFootprintHullPointAt(
    const Vector<OperatorFootprintPoint, PointAllocator>& lower,
    const Vector<OperatorFootprintPoint, PointAllocator>& upper,
    const usize hullIndex
){
    return hullIndex < lower.size()
        ? lower[hullIndex]
        : upper[hullIndex - lower.size() + 1u]
    ;
}

[[nodiscard]] bool BuildConvexOperatorFootprint(
    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>>& points,
    DeformableOperatorFootprint& outFootprint,
    Core::Alloc::ScratchArena<>& scratchArena
){
    outFootprint = DeformableOperatorFootprint{};
    if(points.size() < 3u)
        return false;

    Sort(points.begin(), points.end(), [](const OperatorFootprintPoint& lhs, const OperatorFootprintPoint& rhs){
        if(lhs.x != rhs.x)
            return lhs.x < rhs.x;
        return lhs.y < rhs.y;
    });
    CompactSortedOperatorFootprintPoints(points);
    if(points.size() < 3u)
        return false;

    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>> lower{
        Core::Alloc::ScratchAllocator<OperatorFootprintPoint>(scratchArena)
    };
    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>> upper{
        Core::Alloc::ScratchAllocator<OperatorFootprintPoint>(scratchArena)
    };
    lower.reserve(points.size());
    upper.reserve(points.size());

    for(const OperatorFootprintPoint& point : points){
        while(
            lower.size() >= 2u
            && OperatorFootprintCross(lower[lower.size() - 2u], lower[lower.size() - 1u], point)
                <= s_OperatorFootprintAreaEpsilon
        )
            lower.pop_back();
        lower.push_back(point);
    }

    for(usize i = points.size(); i > 0u; --i){
        const OperatorFootprintPoint& point = points[i - 1u];
        while(
            upper.size() >= 2u
            && OperatorFootprintCross(upper[upper.size() - 2u], upper[upper.size() - 1u], point)
                <= s_OperatorFootprintAreaEpsilon
        )
            upper.pop_back();
        upper.push_back(point);
    }

    if(lower.size() < 2u || upper.size() < 2u)
        return false;

    const usize hullSize = lower.size() + upper.size() - 2u;
    if(hullSize < 3u)
        return false;

    const usize footprintMaxVertexCount = static_cast<usize>(s_DeformableOperatorFootprintMaxVertexCount);
    if(hullSize <= footprintMaxVertexCount){
        for(usize hullIndex = 0u; hullIndex < hullSize; ++hullIndex){
            if(!AppendOperatorFootprintHullPoint(
                outFootprint,
                OperatorFootprintHullPointAt(lower, upper, hullIndex)
            ))
                return false;
        }
    }
    else{
        for(u32 i = 0u; i < s_DeformableOperatorFootprintMaxVertexCount; ++i){
            const usize hullIndex = (static_cast<usize>(i) * hullSize) / footprintMaxVertexCount;
            if(hullIndex >= hullSize)
                return false;
            if(!AppendOperatorFootprintHullPoint(
                outFootprint,
                OperatorFootprintHullPointAt(lower, upper, hullIndex)
            ))
                return false;
        }
    }

    return ValidateOperatorFootprint(outFootprint);
}

[[nodiscard]] bool BuildOperatorFootprintFromGeometryImpl(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorFootprint& outFootprint,
    Core::Alloc::ScratchArena<>& scratchArena
){
    outFootprint = DeformableOperatorFootprint{};
    if(vertices.empty())
        return false;

    f32 maxZ = 0.0f;
    bool foundPosition = false;
    for(const GeometryVertex& vertex : vertices){
        if(!FiniteOperatorPosition(vertex.position))
            return false;

        if(!foundPosition || vertex.position.z > maxZ){
            maxZ = vertex.position.z;
            foundPosition = true;
        }
    }
    if(!foundPosition)
        return false;

    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>> points{
        Core::Alloc::ScratchAllocator<OperatorFootprintPoint>(scratchArena)
    };
    points.reserve(vertices.size());

    const f32 planeEpsilon = Max(s_OperatorFootprintPlaneEpsilon, Abs(maxZ) * 0.00001f);
    for(const GeometryVertex& vertex : vertices){
        if(maxZ - vertex.position.z > planeEpsilon)
            continue;
        if(!AppendOperatorFootprintPoint(points, vertex.position.x, vertex.position.y))
            return false;
    }

    if(points.size() < 3u){
        points.clear();
        for(const GeometryVertex& vertex : vertices){
            if(!AppendOperatorFootprintPoint(points, vertex.position.x, vertex.position.y))
                return false;
        }
    }

    return BuildConvexOperatorFootprint(points, outFootprint, scratchArena);
}

[[nodiscard]] bool BuildOperatorProfilePlaneSample(
    const Vector<GeometryVertex>& vertices,
    const f32 z,
    const f32 minZ,
    const f32 maxZ,
    const f32 topRadius,
    DeformableOperatorProfileSample& outSample,
    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>>& scratchPoints
){
    scratchPoints.clear();

    const f32 planeEpsilon = Max(s_OperatorFootprintPlaneEpsilon, Abs(maxZ - minZ) * 0.00001f);
    for(const GeometryVertex& vertex : vertices){
        if(Abs(vertex.position.z - z) > planeEpsilon)
            continue;
        if(!AppendOperatorFootprintPoint(scratchPoints, vertex.position.x, vertex.position.y))
            return false;
    }
    if(scratchPoints.empty())
        return false;

    Sort(scratchPoints.begin(), scratchPoints.end(), [](const OperatorFootprintPoint& lhs, const OperatorFootprintPoint& rhs){
        if(lhs.x != rhs.x)
            return lhs.x < rhs.x;
        return lhs.y < rhs.y;
    });
    CompactSortedOperatorFootprintPoints(scratchPoints);
    if(scratchPoints.empty())
        return false;

    f32 centerX = 0.0f;
    f32 centerY = 0.0f;
    for(const OperatorFootprintPoint& point : scratchPoints){
        centerX += point.x;
        centerY += point.y;
    }
    const f32 invPointCount = 1.0f / static_cast<f32>(scratchPoints.size());
    centerX *= invPointCount;
    centerY *= invPointCount;

    f32 radiusSquared = 0.0f;
    for(const OperatorFootprintPoint& point : scratchPoints){
        const f32 dx = point.x - centerX;
        const f32 dy = point.y - centerY;
        radiusSquared = Max(radiusSquared, (dx * dx) + (dy * dy));
    }

    const f32 depthSpan = maxZ - minZ;
    if(!IsFinite(depthSpan) || depthSpan <= s_DeformableOperatorProfileDepthEpsilon || topRadius <= s_OperatorProfileScaleEpsilon)
        return false;

    outSample.depth = (maxZ - z) / depthSpan;
    outSample.scale = Sqrt(radiusSquared) / topRadius;
    outSample.center = Float2U(centerX, centerY);
    return
        IsFinite(outSample.depth)
        && IsFinite(outSample.scale)
        && IsFinite(outSample.center.x)
        && IsFinite(outSample.center.y)
    ;
}

[[nodiscard]] bool BuildOperatorProfileFromGeometryImpl(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorProfile& outProfile,
    Core::Alloc::ScratchArena<>& scratchArena
){
    outProfile = DeformableOperatorProfile{};
    if(vertices.empty())
        return false;

    f32 minZ = 0.0f;
    f32 maxZ = 0.0f;
    bool foundPosition = false;
    Vector<f32, Core::Alloc::ScratchAllocator<f32>> zPlanes{ Core::Alloc::ScratchAllocator<f32>(scratchArena) };
    zPlanes.reserve(vertices.size());
    for(const GeometryVertex& vertex : vertices){
        if(!FiniteOperatorPosition(vertex.position))
            return false;
        if(!foundPosition){
            minZ = vertex.position.z;
            maxZ = vertex.position.z;
            foundPosition = true;
        }
        else{
            minZ = Min(minZ, vertex.position.z);
            maxZ = Max(maxZ, vertex.position.z);
        }
        zPlanes.push_back(vertex.position.z);
    }
    if(!foundPosition)
        return false;

    if(maxZ - minZ <= s_DeformableOperatorProfileDepthEpsilon)
        return false;
    if(zPlanes.size() < 2u)
        return false;

    Sort(zPlanes.begin(), zPlanes.end(), [](const f32 lhs, const f32 rhs){
        return lhs > rhs;
    });
    usize uniquePlaneCount = 0u;
    for(const f32 z : zPlanes){
        if(
            uniquePlaneCount == 0u
            || Abs(zPlanes[uniquePlaneCount - 1u] - z) > s_OperatorFootprintPlaneEpsilon
        ){
            zPlanes[uniquePlaneCount] = z;
            ++uniquePlaneCount;
        }
    }
    zPlanes.resize(uniquePlaneCount);

    Vector<OperatorFootprintPoint, Core::Alloc::ScratchAllocator<OperatorFootprintPoint>> samplePoints{
        Core::Alloc::ScratchAllocator<OperatorFootprintPoint>(scratchArena)
    };
    samplePoints.reserve(vertices.size());

    DeformableOperatorProfileSample topSample;
    if(
        !BuildOperatorProfilePlaneSample(
            vertices,
            maxZ,
            minZ,
            maxZ,
            1.0f,
            topSample,
            samplePoints
        )
        || topSample.scale <= s_OperatorProfileScaleEpsilon
    )
        return false;
    const f32 topRadius = topSample.scale;

    const usize sourcePlaneCount = zPlanes.size();
    const usize sampleCount = Min(
        sourcePlaneCount,
        static_cast<usize>(s_DeformableOperatorProfileMaxSampleCount)
    );
    if(sampleCount < 2u)
        return false;

    for(usize sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex){
        const usize planeIndex = sampleIndex + 1u == sampleCount
            ? sourcePlaneCount - 1u
            : (sampleIndex * (sourcePlaneCount - 1u)) / (sampleCount - 1u)
        ;
        if(planeIndex >= zPlanes.size())
            return false;

        DeformableOperatorProfileSample sample;
        if(sampleIndex == 0u){
            sample = topSample;
        }
        else if(!BuildOperatorProfilePlaneSample(
            vertices,
            zPlanes[planeIndex],
            minZ,
            maxZ,
            topRadius,
            sample,
            samplePoints
        ))
            return false;

        if(sampleIndex == 0u){
            sample.depth = 0.0f;
            sample.scale = 1.0f;
        }
        else if(sampleIndex + 1u == sampleCount)
            sample.depth = 1.0f;

        outProfile.samples[outProfile.sampleCount] = sample;
        ++outProfile.sampleCount;
    }

    return ValidateOperatorProfile(outProfile);
}

[[nodiscard]] bool PointOnOperatorFootprintEdge(
    const f32 x,
    const f32 y,
    const Float2U& a,
    const Float2U& b
){
    constexpr f32 epsilon = 0.000001f;
    const f32 edgeX = b.x - a.x;
    const f32 edgeY = b.y - a.y;
    const f32 pointX = x - a.x;
    const f32 pointY = y - a.y;
    const f32 cross = (pointX * edgeY) - (pointY * edgeX);
    if(Abs(cross) > epsilon)
        return false;

    const f32 dot = (pointX * edgeX) + (pointY * edgeY);
    if(dot < -epsilon)
        return false;

    const f32 edgeLengthSq = (edgeX * edgeX) + (edgeY * edgeY);
    return dot <= edgeLengthSq + epsilon;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


f32 OperatorFootprintSignedArea(const DeformableOperatorFootprint& footprint){
    if(footprint.vertexCount < 3u)
        return 0.0f;

    f32 area = 0.0f;
    for(u32 i = 1u; i < footprint.vertexCount; ++i){
        const u32 previous = i - 1u;
        area +=
            (footprint.vertices[previous].x * footprint.vertices[i].y)
            - (footprint.vertices[previous].y * footprint.vertices[i].x)
        ;
    }
    area +=
        (footprint.vertices[footprint.vertexCount - 1u].x * footprint.vertices[0u].y)
        - (footprint.vertices[footprint.vertexCount - 1u].y * footprint.vertices[0u].x)
    ;
    return area * 0.5f;
}

bool ValidateOperatorFootprint(const DeformableOperatorFootprint& footprint){
    if(
        footprint.vertexCount < 3u
        || footprint.vertexCount > s_DeformableOperatorFootprintMaxVertexCount
    )
        return false;

    for(u32 i = 0u; i < footprint.vertexCount; ++i){
        const Float2U& point = footprint.vertices[i];
        if(!IsFinite(point.x) || !IsFinite(point.y))
            return false;
    }

    const f32 area = OperatorFootprintSignedArea(footprint);
    return IsFinite(area) && Abs(area) > __hidden_deformable_operator_shape::s_OperatorFootprintAreaEpsilon;
}

bool ValidateOperatorProfile(const DeformableOperatorProfile& profile){
    if(
        profile.sampleCount < 2u
        || profile.sampleCount > s_DeformableOperatorProfileMaxSampleCount
    )
        return false;

    f32 previousDepth = -1.0f;
    for(u32 i = 0u; i < profile.sampleCount; ++i){
        const DeformableOperatorProfileSample& sample = profile.samples[i];
        if(
            !IsFinite(sample.depth)
            || !IsFinite(sample.scale)
            || !IsFinite(sample.center.x)
            || !IsFinite(sample.center.y)
            || sample.depth < -s_DeformableOperatorProfileDepthEpsilon
            || sample.depth > 1.0f + s_DeformableOperatorProfileDepthEpsilon
            || sample.scale < 0.0f
            || sample.scale > __hidden_deformable_operator_shape::s_MaxOperatorProfileScale
        )
            return false;
        if(i == 0u){
            if(
                Abs(sample.depth) > s_DeformableOperatorProfileDepthEpsilon
                || sample.scale <= __hidden_deformable_operator_shape::s_OperatorProfileScaleEpsilon
            )
                return false;
        }
        else if(sample.depth <= previousDepth + s_DeformableOperatorProfileDepthEpsilon)
            return false;
        previousDepth = sample.depth;
    }

    return Abs(profile.samples[profile.sampleCount - 1u].depth - 1.0f) <= s_DeformableOperatorProfileDepthEpsilon;
}

bool ValidateHoleOperatorShape(
    const DeformableOperatorFootprint& footprint,
    const DeformableOperatorProfile& profile
){
    return ValidateOperatorFootprint(footprint) && ValidateOperatorProfile(profile);
}

bool PointInsideOperatorFootprint(
    const DeformableOperatorFootprint& footprint,
    const f32 x,
    const f32 y
){
    bool inside = false;
    u32 previous = footprint.vertexCount - 1u;
    for(u32 current = 0u; current < footprint.vertexCount; ++current){
        const Float2U& a = footprint.vertices[current];
        const Float2U& b = footprint.vertices[previous];
        if(__hidden_deformable_operator_shape::PointOnOperatorFootprintEdge(x, y, a, b))
            return true;

        const bool crossesY = (a.y > y) != (b.y > y);
        if(crossesY){
            const f32 intersectX = a.x + ((y - a.y) * (b.x - a.x) / (b.y - a.y));
            if(x <= intersectX + 0.000001f)
                inside = !inside;
        }
        previous = current;
    }
    return inside;
}

bool SampleOperatorProfile(
    const DeformableOperatorProfile& profile,
    const f32 rawDepth,
    Float2U& outCenter,
    f32& outScale
){
    outCenter = Float2U(0.0f, 0.0f);
    outScale = 1.0f;
    if(!ValidateOperatorProfile(profile) || !IsFinite(rawDepth))
        return false;

    const f32 depth = Min(Max(rawDepth, 0.0f), 1.0f);
    if(depth <= profile.samples[0u].depth + s_DeformableOperatorProfileDepthEpsilon){
        outCenter = profile.samples[0u].center;
        outScale = profile.samples[0u].scale;
        return true;
    }

    for(u32 i = 1u; i < profile.sampleCount; ++i){
        const DeformableOperatorProfileSample& previous = profile.samples[i - 1u];
        const DeformableOperatorProfileSample& next = profile.samples[i];
        if(depth > next.depth + s_DeformableOperatorProfileDepthEpsilon)
            continue;

        const f32 depthSpan = next.depth - previous.depth;
        if(depthSpan <= s_DeformableOperatorProfileDepthEpsilon)
            return false;

        const f32 t = (depth - previous.depth) / depthSpan;
        outCenter = Float2U(
            previous.center.x + ((next.center.x - previous.center.x) * t),
            previous.center.y + ((next.center.y - previous.center.y) * t)
        );
        outScale = previous.scale + ((next.scale - previous.scale) * t);
        return IsFinite(outCenter.x) && IsFinite(outCenter.y) && IsFinite(outScale);
    }

    const DeformableOperatorProfileSample& last = profile.samples[profile.sampleCount - 1u];
    outCenter = last.center;
    outScale = last.scale;
    return true;
}

bool BuildOperatorFootprintFromGeometry(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorFootprint& outFootprint
){
    Core::Alloc::ScratchArena<> scratchArena;
    return __hidden_deformable_operator_shape::BuildOperatorFootprintFromGeometryImpl(
        vertices,
        outFootprint,
        scratchArena
    );
}

bool BuildOperatorProfileFromGeometry(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorProfile& outProfile
){
    Core::Alloc::ScratchArena<> scratchArena;
    return __hidden_deformable_operator_shape::BuildOperatorProfileFromGeometryImpl(
        vertices,
        outProfile,
        scratchArena
    );
}

bool BuildOperatorShapeFromGeometry(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorFootprint& outFootprint,
    DeformableOperatorProfile& outProfile
){
    outFootprint = DeformableOperatorFootprint{};
    outProfile = DeformableOperatorProfile{};
    Core::Alloc::ScratchArena<> scratchArena;
    return
        __hidden_deformable_operator_shape::BuildOperatorFootprintFromGeometryImpl(
            vertices,
            outFootprint,
            scratchArena
        )
        && __hidden_deformable_operator_shape::BuildOperatorProfileFromGeometryImpl(
            vertices,
            outProfile,
            scratchArena
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

