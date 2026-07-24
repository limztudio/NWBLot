// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(NWB_MATH_COLLISION_INCLUDE_INLINE)
#error "Do not include collision.inl directly. Include collision.h from global/math or simdmath.h elsewhere."
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CollisionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_RayEpsilon = 1.0e-20f;
inline constexpr f32 s_PlaneEpsilon = 1.192092896e-7f;
inline constexpr usize s_TriangleVertexCount = 3u;
inline constexpr u32 s_FrustumPlaneCount = 6u;

[[nodiscard]] NWB_INLINE bool Vector3AnyTrue(const SIMDVector value)noexcept{
    return (VectorMoveMask(value) & 0x7u) != 0u;
}

[[nodiscard]] NWB_INLINE bool Vector4AllTrue(const SIMDVector value)noexcept{
    return (VectorMoveMask(value) & 0xFu) == 0xFu;
}

[[nodiscard]] NWB_INLINE bool Vector4AnyTrue(const SIMDVector value)noexcept{
    return (VectorMoveMask(value) & 0xFu) != 0u;
}

[[nodiscard]] NWB_INLINE SIMDVector Vector3MaxComponent(const SIMDVector value)noexcept{
    return VectorMax(VectorSplatX(value), VectorMax(VectorSplatY(value), VectorSplatZ(value)));
}

[[nodiscard]] NWB_INLINE SIMDVector Vector3SignedUnitMask(const SIMDVector position, const SIMDVector componentMask)noexcept{
    const SIMDVector sign = VectorSelect(s_SIMDNegativeOne, s_SIMDOne, VectorGreaterOrEqual(position, VectorZero()));
    return VectorAndInt(sign, componentMask);
}

[[nodiscard]] NWB_INLINE SIMDVector CapsuleYSegmentPoint(const SIMDVector position, const SIMDVector radiusHalfHeight)noexcept{
    const SIMDVector halfHeight = VectorSplatY(radiusHalfHeight);
    const SIMDVector clampedY = VectorClamp(VectorSplatY(position), VectorNegate(halfHeight), halfHeight);
    return VectorAndInt(clampedY, s_SIMDMaskY);
}

[[nodiscard]] NWB_INLINE const Float3U* StrideFloat3Pointer(const Float3U* points, const usize stride, const usize index)noexcept{
    return reinterpret_cast<const Float3U*>(reinterpret_cast<const u8*>(points) + stride * index);
}

[[nodiscard]] NWB_INLINE SIMDVector SphereCenter(const SIMDVector centerRadius)noexcept{
    return VectorSetW(centerRadius, 0.0f);
}

[[nodiscard]] NWB_INLINE f32 SphereRadius(const SIMDVector centerRadius)noexcept{
    return VectorGetW(centerRadius);
}

[[nodiscard]] NWB_INLINE SIMDVector SphereCenterRadius(const SIMDVector center, const f32 radius)noexcept{
    return VectorSetW(center, radius);
}

[[nodiscard]] inline SIMDVector CreateSphereFromVectorPoints(const SIMDVector* points, const usize count)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(count > 0u);

    SIMDVector centerVector = VectorZero();
    for(usize pointIndex = 0u; pointIndex < count; ++pointIndex)
        centerVector = VectorAdd(centerVector, points[pointIndex]);
    centerVector = VectorDivide(centerVector, VectorReplicate(static_cast<f32>(count)));

    SIMDVector radiusSq = VectorZero();
    for(usize pointIndex = 0u; pointIndex < count; ++pointIndex){
        const SIMDVector delta = VectorSubtract(points[pointIndex], centerVector);
        radiusSq = VectorMax(radiusSq, Vector3LengthSq(delta));
    }

    return SphereCenterRadius(centerVector, VectorGetX(VectorSqrt(radiusSq)));
}

[[nodiscard]] NWB_INLINE SIMDVector BoxCornerOffset(const u32 index)noexcept{
    return VectorSet(
        (index & 1u) ? 1.0f : -1.0f,
        (index & 2u) ? 1.0f : -1.0f,
        (index & 4u) ? 1.0f : -1.0f,
        0.0f
    );
}

[[nodiscard]] NWB_INLINE SIMDVector PlaneNormalizeSafe(const SIMDVector plane)noexcept{
    const SIMDVector length = Vector3Length(plane);
    if(VectorGetX(length) <= s_PlaneEpsilon)
        return plane;
    return VectorDivide(plane, length);
}

[[nodiscard]] NWB_INLINE SIMDVector TransformPlane(const SIMDVector plane, const SIMDVector rotation, const SIMDVector translation)noexcept{
    const SIMDVector normal = Vector3Rotate(plane, rotation);
    const SIMDVector distance = VectorSubtract(VectorSplatW(plane), Vector3Dot(normal, translation));
    return VectorSelect(normal, distance, s_SIMDMaskW);
}

[[nodiscard]] NWB_INLINE SIMDVector PlaneDistance(const SIMDVector plane, const SIMDVector point)noexcept{
    return VectorAdd(Vector3Dot(plane, point), VectorSplatW(plane));
}

[[nodiscard]] inline bool PointsInsideMinMax(
    const SIMDVector* points,
    const usize pointCount,
    const SIMDVector minBounds,
    const SIMDVector maxBounds
)noexcept{
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        const SIMDVector outside = VectorOrInt(VectorLess(points[pointIndex], minBounds), VectorGreater(points[pointIndex], maxBounds));
        if(Vector3AnyTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool PointsInsideSphere(
    const SIMDVector* points,
    const usize pointCount,
    const SIMDVector center,
    const SIMDVector radius
)noexcept{
    const SIMDVector radiusSq = VectorMultiply(radius, radius);
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        const SIMDVector delta = VectorSubtract(points[pointIndex], center);
        if(Vector4AnyTrue(VectorGreater(Vector3LengthSq(delta), radiusSq)))
            return false;
    }
    return true;
}

NWB_INLINE void MinMaxFromCenterExtents(
    const SIMDVector center,
    const SIMDVector extents,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds
)noexcept{
    outMinBounds = VectorSubtract(center, extents);
    outMaxBounds = VectorAdd(center, extents);
}

NWB_INLINE void CenterExtentsFromMinMax(
    const SIMDVector minBounds,
    const SIMDVector maxBounds,
    SIMDVector& outCenter,
    SIMDVector& outExtents
)noexcept{
    outCenter = VectorSetW(VectorMultiply(VectorAdd(minBounds, maxBounds), VectorReplicate(0.5f)), 0.0f);
    outExtents = VectorSetW(VectorMultiply(VectorSubtract(maxBounds, minBounds), VectorReplicate(0.5f)), 0.0f);
}

NWB_INLINE void ExpandMinMax(const SIMDVector point, SIMDVector& inOutMinBounds, SIMDVector& inOutMaxBounds)noexcept{
    inOutMinBounds = VectorMin(inOutMinBounds, point);
    inOutMaxBounds = VectorMax(inOutMaxBounds, point);
}

[[nodiscard]] NWB_INLINE SIMDVector ClosestPointOnMinMax(
    const SIMDVector point,
    const SIMDVector minBounds,
    const SIMDVector maxBounds
)noexcept{
    return VectorMin(VectorMax(point, minBounds), maxBounds);
}

[[nodiscard]] NWB_INLINE bool MinMaxIntersects(
    const SIMDVector lhsMinBounds,
    const SIMDVector lhsMaxBounds,
    const SIMDVector rhsMinBounds,
    const SIMDVector rhsMaxBounds
)noexcept{
    const SIMDVector disjoint = VectorOrInt(VectorGreater(lhsMinBounds, rhsMaxBounds), VectorGreater(rhsMinBounds, lhsMaxBounds));
    return !Vector3AnyTrue(disjoint);
}

NWB_INLINE void FastIntersectSpherePlane(
    const SIMDVector center,
    const SIMDVector radius,
    const SIMDVector plane,
    SIMDVector& outOutside,
    SIMDVector& outInside
)noexcept{
    const SIMDVector distance = PlaneDistance(plane, center);
    outOutside = VectorLess(distance, VectorNegate(radius));
    outInside = VectorGreaterOrEqual(distance, radius);
}

NWB_INLINE void FastIntersectAxisAlignedBoxPlane(
    const SIMDVector center,
    const SIMDVector extents,
    const SIMDVector plane,
    SIMDVector& outOutside,
    SIMDVector& outInside
)noexcept{
    const SIMDVector distance = PlaneDistance(plane, center);
    const SIMDVector radius = Vector3Dot(extents, VectorAbs(plane));
    outOutside = VectorLess(distance, VectorNegate(radius));
    outInside = VectorGreaterOrEqual(distance, radius);
}

NWB_INLINE void FastIntersectOrientedBoxPlane(
    const SIMDVector center,
    const SIMDVector extents,
    const SIMDVector axis0,
    const SIMDVector axis1,
    const SIMDVector axis2,
    const SIMDVector plane,
    SIMDVector& outOutside,
    SIMDVector& outInside
)noexcept{
    const SIMDVector distance = PlaneDistance(plane, center);
    SIMDVector radiusAxes = Vector3Dot(plane, axis0);
    radiusAxes = VectorSelect(radiusAxes, Vector3Dot(plane, axis1), VectorSelectControl(0u, 1u, 0u, 0u));
    radiusAxes = VectorSelect(radiusAxes, Vector3Dot(plane, axis2), VectorSelectControl(0u, 0u, 1u, 0u));
    const SIMDVector radius = Vector3Dot(extents, VectorAbs(radiusAxes));
    outOutside = VectorLess(distance, VectorNegate(radius));
    outInside = VectorGreaterOrEqual(distance, radius);
}

inline void FastIntersectPointsPlane(
    const SIMDVector* points,
    const usize pointCount,
    const SIMDVector plane,
    SIMDVector& outOutside,
    SIMDVector& outInside
)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(pointCount > 0u);

    SIMDVector minDistance = PlaneDistance(plane, points[0]);
    SIMDVector maxDistance = minDistance;
    for(usize pointIndex = 1u; pointIndex < pointCount; ++pointIndex){
        const SIMDVector distance = PlaneDistance(plane, points[pointIndex]);
        minDistance = VectorMin(minDistance, distance);
        maxDistance = VectorMax(maxDistance, distance);
    }

    outOutside = VectorLess(maxDistance, VectorZero());
    outInside = VectorGreaterOrEqual(minDistance, VectorZero());
}

[[nodiscard]] inline bool RayIntersectsMinMax(
    const SIMDVector origin,
    const SIMDVector direction,
    const SIMDVector minBounds,
    const SIMDVector maxBounds,
    f32& outDistance
)noexcept{
    const SIMDVector center = VectorScale(VectorAdd(minBounds, maxBounds), 0.5f);
    const SIMDVector extents = VectorScale(VectorSubtract(maxBounds, minBounds), 0.5f);
    const SIMDVector axisOrigin = VectorSubtract(center, origin);
    const SIMDVector isParallel = VectorLessOrEqual(VectorAbs(direction), VectorReplicate(s_RayEpsilon));
    const SIMDVector inverseDirection = VectorReciprocal(direction);
    const SIMDVector t1 = VectorMultiply(VectorSubtract(axisOrigin, extents), inverseDirection);
    const SIMDVector t2 = VectorMultiply(VectorAdd(axisOrigin, extents), inverseDirection);
    const SIMDVector negativeMax = VectorReplicate(-s_MaxF32);
    const SIMDVector positiveMax = VectorReplicate(s_MaxF32);

    SIMDVector tMin = VectorSelect(VectorMin(t1, t2), negativeMax, isParallel);
    SIMDVector tMax = VectorSelect(VectorMax(t1, t2), positiveMax, isParallel);
    tMin = VectorMax(tMin, VectorSplatY(tMin));
    tMin = VectorMax(tMin, VectorSplatZ(tMin));
    tMax = VectorMin(tMax, VectorSplatY(tMax));
    tMax = VectorMin(tMax, VectorSplatZ(tMax));

    SIMDVector noIntersection = VectorGreater(VectorSplatX(tMin), VectorSplatX(tMax));
    noIntersection = VectorOrInt(noIntersection, VectorLess(VectorSplatX(tMax), VectorZero()));
    noIntersection = VectorOrInt(noIntersection, VectorAndCInt(isParallel, VectorInBounds(axisOrigin, extents)));

    if(Vector3AnyTrue(noIntersection)){
        outDistance = 0.0f;
        return false;
    }

    outDistance = VectorGetX(tMin);
    return true;
}

[[nodiscard]] inline SIMDVector ClosestPointOnTriangle(
    const SIMDVector point,
    const SIMDVector a,
    const SIMDVector b,
    const SIMDVector c
)noexcept{
    const SIMDVector ab = VectorSubtract(b, a);
    const SIMDVector ac = VectorSubtract(c, a);
    const SIMDVector ap = VectorSubtract(point, a);
    const f32 d1 = VectorGetX(Vector3Dot(ab, ap));
    const f32 d2 = VectorGetX(Vector3Dot(ac, ap));
    if(d1 <= 0.0f && d2 <= 0.0f)
        return a;

    const SIMDVector bp = VectorSubtract(point, b);
    const f32 d3 = VectorGetX(Vector3Dot(ab, bp));
    const f32 d4 = VectorGetX(Vector3Dot(ac, bp));
    if(d3 >= 0.0f && d4 <= d3)
        return b;

    const f32 vc = d1 * d4 - d3 * d2;
    if(vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        return VectorMultiplyAdd(ab, VectorReplicate(d1 / (d1 - d3)), a);

    const SIMDVector cp = VectorSubtract(point, c);
    const f32 d5 = VectorGetX(Vector3Dot(ab, cp));
    const f32 d6 = VectorGetX(Vector3Dot(ac, cp));
    if(d6 >= 0.0f && d5 <= d6)
        return c;

    const f32 vb = d5 * d2 - d1 * d6;
    if(vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        return VectorMultiplyAdd(ac, VectorReplicate(d2 / (d2 - d6)), a);

    const f32 va = d3 * d6 - d5 * d4;
    if(va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f){
        const SIMDVector bc = VectorSubtract(c, b);
        return VectorMultiplyAdd(bc, VectorReplicate((d4 - d3) / ((d4 - d3) + (d5 - d6))), b);
    }

    const f32 denom = 1.0f / (va + vb + vc);
    return VectorMultiplyAdd(
        ac,
        VectorReplicate(vc * denom),
        VectorMultiplyAdd(ab, VectorReplicate(vb * denom), a)
    );
}

[[nodiscard]] inline bool RayIntersectsTriangle(
    const SIMDVector origin,
    const SIMDVector direction,
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2,
    f32& outDistance
)noexcept{
    const SIMDVector edge1 = VectorSubtract(v1, v0);
    const SIMDVector edge2 = VectorSubtract(v2, v0);
    const SIMDVector p = Vector3Cross(direction, edge2);
    const f32 determinant = VectorGetX(Vector3Dot(edge1, p));
    if(Abs(determinant) <= s_RayEpsilon)
        return false;

    const f32 inverseDeterminant = 1.0f / determinant;
    const SIMDVector s = VectorSubtract(origin, v0);
    const f32 u = VectorGetX(Vector3Dot(s, p)) * inverseDeterminant;
    if(u < 0.0f || u > 1.0f)
        return false;

    const SIMDVector q = Vector3Cross(s, edge1);
    const f32 v = VectorGetX(Vector3Dot(direction, q)) * inverseDeterminant;
    if(v < 0.0f || (u + v) > 1.0f)
        return false;

    const f32 t = VectorGetX(Vector3Dot(edge2, q)) * inverseDeterminant;
    if(t < 0.0f)
        return false;

    outDistance = t;
    return true;
}

NWB_INLINE void ObbAxes(
    const SIMDVector orientation,
    SIMDVector& outAxis0,
    SIMDVector& outAxis1,
    SIMDVector& outAxis2
)noexcept{
    outAxis0 = Vector3Rotate(s_SIMDIdentityR0, orientation);
    outAxis1 = Vector3Rotate(s_SIMDIdentityR1, orientation);
    outAxis2 = Vector3Rotate(s_SIMDIdentityR2, orientation);
}

[[nodiscard]] NWB_INLINE SIMDVector PointToObbLocal(
    const SIMDVector point,
    const SIMDVector center,
    const SIMDVector orientation
)noexcept{
    return Vector3InverseRotate(VectorSubtract(point, center), orientation);
}

[[nodiscard]] NWB_INLINE bool PointInsideObb(
    const SIMDVector point,
    const SIMDVector center,
    const SIMDVector extents,
    const SIMDVector orientation
)noexcept{
    return Vector3LessOrEqual(VectorAbs(PointToObbLocal(point, center, orientation)), extents);
}

[[nodiscard]] inline bool PointsInsideObb(
    const SIMDVector* points,
    const usize pointCount,
    const SIMDVector center,
    const SIMDVector extents,
    const SIMDVector orientation
)noexcept{
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        if(Vector3AnyTrue(VectorGreater(VectorAbs(PointToObbLocal(points[pointIndex], center, orientation)), extents)))
            return false;
    }
    return true;
}

inline void AabbCorners(const SIMDVector center, const SIMDVector extents, SIMDVector* outCorners)noexcept{
    for(u32 i = 0u; i < BoundingBox::s_CornerCount; ++i)
        outCorners[i] = VectorMultiplyAdd(extents, BoxCornerOffset(i), center);
}

inline void ObbCorners(const SIMDVector center, const SIMDVector extents, const SIMDVector orientation, SIMDVector* outCorners)noexcept{
    SIMDVector axis0{};
    SIMDVector axis1{};
    SIMDVector axis2{};
    ObbAxes(orientation, axis0, axis1, axis2);
    axis0 = VectorMultiply(axis0, VectorSplatX(extents));
    axis1 = VectorMultiply(axis1, VectorSplatY(extents));
    axis2 = VectorMultiply(axis2, VectorSplatZ(extents));
    for(u32 i = 0u; i < BoundingOrientedBox::s_CornerCount; ++i){
        const SIMDVector offset = BoxCornerOffset(i);
        SIMDVector corner = VectorMultiply(axis0, VectorSplatX(offset));
        corner = VectorMultiplyAdd(axis1, VectorSplatY(offset), corner);
        corner = VectorMultiplyAdd(axis2, VectorSplatZ(offset), corner);
        outCorners[i] = VectorAdd(corner, center);
    }
}

inline void FrustumCorners(
    const SIMDVector origin,
    const SIMDVector orientation,
    const f32 rightSlope,
    const f32 leftSlope,
    const f32 topSlope,
    const f32 bottomSlope,
    const f32 nearPlane,
    const f32 farPlane,
    SIMDVector* outCorners
)noexcept{
    const Float2U depths(nearPlane, farPlane);
    u32 corner = 0u;
    for(const f32 depth : depths.raw){
        const f32 left = leftSlope * depth;
        const f32 right = rightSlope * depth;
        const f32 bottom = bottomSlope * depth;
        const f32 top = topSlope * depth;
        const SIMDVector localCorners[4] = {
            VectorSet(right, top, depth, 0.0f),
            VectorSet(left, top, depth, 0.0f),
            VectorSet(left, bottom, depth, 0.0f),
            VectorSet(right, bottom, depth, 0.0f),
        };
        for(const SIMDVector localCorner : localCorners){
            outCorners[corner] = VectorAdd(Vector3Rotate(localCorner, orientation), origin);
            ++corner;
        }
    }
}

[[nodiscard]] inline ContainmentType::Enum ContainmentFromPlaneTests(
    const SIMDVector* points,
    const usize pointCount,
    const SIMDVector* planes,
    const usize planeCount
)noexcept{
    SIMDVector anyIntersecting = VectorFalseInt();
    for(usize planeIndex = 0u; planeIndex < planeCount; ++planeIndex){
        SIMDVector outside{};
        SIMDVector inside{};
        FastIntersectPointsPlane(points, pointCount, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return ContainmentType::Disjoint;
        anyIntersecting = VectorOrInt(anyIntersecting, VectorEqualInt(inside, VectorFalseInt()));
    }
    return Vector4AllTrue(anyIntersecting) ? ContainmentType::Intersects : ContainmentType::Contains;
}

inline void FrustumPlanes(
    const SIMDVector origin,
    const SIMDVector orientation,
    const f32 rightSlope,
    const f32 leftSlope,
    const f32 topSlope,
    const f32 bottomSlope,
    const f32 nearPlane,
    const f32 farPlane,
    SIMDVector* outPlanes
)noexcept{
    const SIMDVector localPlanes[s_FrustumPlaneCount] = {
        VectorSet(0.0f, 0.0f, 1.0f, -nearPlane),
        VectorSet(0.0f, 0.0f, -1.0f, farPlane),
        VectorSet(-1.0f, 0.0f, rightSlope, 0.0f),
        VectorSet(1.0f, 0.0f, -leftSlope, 0.0f),
        VectorSet(0.0f, -1.0f, topSlope, 0.0f),
        VectorSet(0.0f, 1.0f, -bottomSlope, 0.0f),
    };
    for(u32 i = 0u; i < s_FrustumPlaneCount; ++i)
        outPlanes[i] = PlaneNormalizeSafe(TransformPlane(localPlanes[i], orientation, origin));
}

[[nodiscard]] inline bool FrustumPlanesIntersectAxisAlignedBox(
    const SIMDVector* planes,
    const SIMDVector center,
    const SIMDVector extents
)noexcept{
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVector outside{};
        SIMDVector inside{};
        FastIntersectAxisAlignedBoxPlane(center, extents, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectSphere(const SIMDVector* planes, const SIMDVector centerRadius)noexcept{
    const SIMDVector center = SphereCenter(centerRadius);
    const SIMDVector radius = VectorReplicate(SphereRadius(centerRadius));
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVector outside{};
        SIMDVector inside{};
        FastIntersectSpherePlane(center, radius, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectOrientedBox(
    const SIMDVector* planes,
    const SIMDVector center,
    const SIMDVector extents,
    const SIMDVector orientation
)noexcept{
    SIMDVector axis0{};
    SIMDVector axis1{};
    SIMDVector axis2{};
    ObbAxes(orientation, axis0, axis1, axis2);
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVector outside{};
        SIMDVector inside{};
        FastIntersectOrientedBoxPlane(center, extents, axis0, axis1, axis2, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectPoints(
    const SIMDVector* planes,
    const SIMDVector* points,
    const usize pointCount
)noexcept{
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVector outside{};
        SIMDVector inside{};
        FastIntersectPointsPlane(points, pointCount, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool ObbIntersectsObb(
    const SIMDVector lhsCenter,
    const SIMDVector lhsExtents,
    const SIMDVector lhsOrientation,
    const SIMDVector rhsCenter,
    const SIMDVector rhsExtents,
    const SIMDVector rhsOrientation
)noexcept{
    const SIMDVector relativeOrientation = QuaternionMultiply(lhsOrientation, QuaternionConjugate(rhsOrientation));
    SIMDMatrix rotation = MatrixRotationQuaternion(relativeOrientation);
    const SIMDVector translation = Vector3InverseRotate(VectorSubtract(rhsCenter, lhsCenter), lhsOrientation);

    const SIMDVector r0 = rotation.v[0];
    const SIMDVector r1 = rotation.v[1];
    const SIMDVector r2 = rotation.v[2];
    rotation = MatrixTranspose(rotation);
    const SIMDVector c0 = rotation.v[0];
    const SIMDVector c1 = rotation.v[1];
    const SIMDVector c2 = rotation.v[2];

    const SIMDVector ar0 = VectorAbs(r0);
    const SIMDVector ar1 = VectorAbs(r1);
    const SIMDVector ar2 = VectorAbs(r2);
    const SIMDVector ac0 = VectorAbs(c0);
    const SIMDVector ac1 = VectorAbs(c1);
    const SIMDVector ac2 = VectorAbs(c2);

    SIMDVector distance = VectorSplatX(translation);
    SIMDVector lhsRadius = VectorSplatX(lhsExtents);
    SIMDVector rhsRadius = Vector3Dot(rhsExtents, ar0);
    SIMDVector noIntersection = VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius));

    distance = VectorSplatY(translation);
    lhsRadius = VectorSplatY(lhsExtents);
    rhsRadius = Vector3Dot(rhsExtents, ar1);
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = VectorSplatZ(translation);
    lhsRadius = VectorSplatZ(lhsExtents);
    rhsRadius = Vector3Dot(rhsExtents, ar2);
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, c0);
    lhsRadius = Vector3Dot(lhsExtents, ac0);
    rhsRadius = VectorSplatX(rhsExtents);
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, c1);
    lhsRadius = Vector3Dot(lhsExtents, ac1);
    rhsRadius = VectorSplatY(rhsExtents);
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, c2);
    lhsRadius = Vector3Dot(lhsExtents, ac2);
    rhsRadius = VectorSplatZ(rhsExtents);
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<3, 6, 1, 0>(c0, VectorNegate(c0)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<3, 2, 1, 0>(ac0));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<3, 2, 1, 0>(ar0));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<3, 6, 1, 0>(c1, VectorNegate(c1)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<3, 2, 1, 0>(ac1));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<2, 3, 0, 1>(ar0));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<3, 6, 1, 0>(c2, VectorNegate(c2)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<3, 2, 1, 0>(ac2));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<1, 0, 3, 2>(ar0));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<2, 3, 4, 1>(c0, VectorNegate(c0)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<2, 3, 0, 1>(ac0));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<3, 2, 1, 0>(ar1));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<2, 3, 4, 1>(c1, VectorNegate(c1)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<2, 3, 0, 1>(ac1));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<2, 3, 0, 1>(ar1));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<2, 3, 4, 1>(c2, VectorNegate(c2)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<2, 3, 0, 1>(ac2));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<1, 0, 3, 2>(ar1));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<5, 0, 3, 2>(c0, VectorNegate(c0)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<1, 0, 3, 2>(ac0));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<3, 2, 1, 0>(ar2));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<5, 0, 3, 2>(c1, VectorNegate(c1)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<1, 0, 3, 2>(ac1));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<2, 3, 0, 1>(ar2));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<5, 0, 3, 2>(c2, VectorNegate(c2)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<1, 0, 3, 2>(ac2));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<1, 0, 3, 2>(ar2));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    return Vector4NotEqualInt(noIntersection, VectorTrueInt());
}

[[nodiscard]] inline bool TriangleAabbOverlap(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2,
    const SIMDVector minBounds,
    const SIMDVector maxBounds
)noexcept{
    const SIMDVector triangleMin = VectorMin(v0, VectorMin(v1, v2));
    const SIMDVector triangleMax = VectorMax(v0, VectorMax(v1, v2));
    return MinMaxIntersects(triangleMin, triangleMax, minBounds, maxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL PlaneTests::Distance(const SIMDVector plane, const SIMDVector point)noexcept{
    return CollisionDetail::PlaneDistance(plane, point);
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL PlaneTests::FromPointNormal(
    const SIMDVector normal,
    const SIMDVector point,
    const SIMDVector fallbackNormal
)noexcept{
    const SIMDVector lengthSquared = Vector3LengthSq(normal);
    const f32 scalarLengthSquared = VectorGetX(lengthSquared);
    SIMDVector unitNormal = VectorSetW(fallbackNormal, 0.0f);
    if(IsFinite(scalarLengthSquared) && scalarLengthSquared > 0.0f)
        unitNormal = VectorSetW(VectorMultiply(normal, VectorReciprocalSqrt(lengthSquared)), 0.0f);
    return VectorSetW(unitNormal, -VectorGetX(Vector3Dot(unitNormal, point)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::Plane(
    const SIMDVector position,
    const SIMDVector normalDistance
)noexcept{
    return PlaneTests::Distance(normalDistance, position);
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::Box(
    const SIMDVector position,
    const SIMDVector halfExtents
)noexcept{
    const SIMDVector q = VectorSubtract(VectorAbs(position), halfExtents);
    const SIMDVector outside = VectorMax(q, VectorZero());
    const SIMDVector insideDistance = VectorMin(CollisionDetail::Vector3MaxComponent(q), VectorZero());
    return VectorAdd(Vector3Length(outside), insideDistance);
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::Sphere(
    const SIMDVector position,
    const SIMDVector radius
)noexcept{
    return VectorSubtract(Vector3Length(position), VectorSplatX(radius));
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::CapsuleY(
    const SIMDVector position,
    const SIMDVector radiusHalfHeight
)noexcept{
    const SIMDVector segmentPoint = CollisionDetail::CapsuleYSegmentPoint(position, radiusHalfHeight);
    return VectorSubtract(Vector3Length(VectorSubtract(position, segmentPoint)), VectorSplatX(radiusHalfHeight));
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::PlaneNormal(
    const SIMDVector normalDistance,
    const SIMDVector fallback,
    const f32 minLengthSquared
)noexcept{
    return Vector3NormalizeOr(VectorSetW(normalDistance, 0.0f), fallback, minLengthSquared);
}

[[nodiscard]] inline SIMDVector SIMDCALL SdfTests::BoxNormal(
    const SIMDVector position,
    const SIMDVector halfExtents,
    const SIMDVector fallback,
    const f32 minLengthSquared
)noexcept{
    const SIMDVector q = VectorSubtract(VectorAbs(position), halfExtents);
    const SIMDVector outside = VectorMax(q, VectorZero());
    const SIMDVector signedOutside = VectorSelect(VectorNegate(outside), outside, VectorGreaterOrEqual(position, VectorZero()));
    const SIMDVector outsideNormal = Vector3NormalizeOr(signedOutside, fallback, minLengthSquared);
    const SIMDVector useOutside = VectorGreater(Vector3LengthSq(outside), VectorReplicate(minLengthSquared));

    const SIMDVector qX = VectorSplatX(q);
    const SIMDVector qY = VectorSplatY(q);
    const SIMDVector qZ = VectorSplatZ(q);
    const SIMDVector xIsMax = VectorAndInt(VectorGreaterOrEqual(qX, qY), VectorGreaterOrEqual(qX, qZ));
    const SIMDVector yIsMax = VectorGreaterOrEqual(qY, qZ);
    const SIMDVector xNormal = CollisionDetail::Vector3SignedUnitMask(position, s_SIMDMaskX);
    const SIMDVector yNormal = CollisionDetail::Vector3SignedUnitMask(position, s_SIMDMaskY);
    const SIMDVector zNormal = CollisionDetail::Vector3SignedUnitMask(position, s_SIMDMaskZ);
    SIMDVector faceNormal = VectorSelect(zNormal, yNormal, yIsMax);
    faceNormal = VectorSelect(faceNormal, xNormal, xIsMax);
    return VectorSelect(faceNormal, outsideNormal, useOutside);
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::SphereNormal(
    const SIMDVector position,
    const SIMDVector fallback,
    const f32 minLengthSquared
)noexcept{
    return Vector3NormalizeOr(position, fallback, minLengthSquared);
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::CapsuleYNormal(
    const SIMDVector position,
    const SIMDVector radiusHalfHeight,
    const f32 minLengthSquared
)noexcept{
    const SIMDVector segmentPoint = CollisionDetail::CapsuleYSegmentPoint(position, radiusHalfHeight);
    const SIMDVector fallback = VectorSelect(
        VectorSet(0.0f, -1.0f, 0.0f, 0.0f),
        VectorSet(0.0f, 1.0f, 0.0f, 0.0f),
        VectorGreaterOrEqual(VectorSplatY(position), VectorZero())
    );
    return Vector3NormalizeOr(
        VectorSubtract(position, segmentPoint),
        fallback,
        minLengthSquared
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool SIMDCALL AabbTests::Valid(const SIMDVector minBounds, const SIMDVector maxBounds)noexcept{
    return
        !Vector3IsNaN(minBounds)
        && !Vector3IsInfinite(minBounds)
        && !Vector3IsNaN(maxBounds)
        && !Vector3IsInfinite(maxBounds)
        && Vector3LessOrEqual(minBounds, maxBounds)
    ;
}

NWB_INLINE void SIMDCALL AabbTests::Reset(SIMDVector& outMinBounds, SIMDVector& outMaxBounds)noexcept{
    outMinBounds = VectorReplicate(s_MaxF32);
    outMaxBounds = VectorReplicate(-s_MaxF32);
}

NWB_INLINE void SIMDCALL AabbTests::Expand(const SIMDVector point, SIMDVector& inOutMinBounds, SIMDVector& inOutMaxBounds)noexcept{
    CollisionDetail::ExpandMinMax(point, inOutMinBounds, inOutMaxBounds);
}

NWB_INLINE void SIMDCALL AabbTests::ExpandTriangle(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2,
    SIMDVector& inOutMinBounds,
    SIMDVector& inOutMaxBounds
)noexcept{
    AabbTests::Expand(v0, inOutMinBounds, inOutMaxBounds);
    AabbTests::Expand(v1, inOutMinBounds, inOutMaxBounds);
    AabbTests::Expand(v2, inOutMinBounds, inOutMaxBounds);
}

[[nodiscard]] NWB_INLINE bool SIMDCALL AabbTests::Intersects(
    const SIMDVector lhsMinBounds,
    const SIMDVector lhsMaxBounds,
    const SIMDVector rhsMinBounds,
    const SIMDVector rhsMaxBounds
)noexcept{
    return
        AabbTests::Valid(lhsMinBounds, lhsMaxBounds)
        && AabbTests::Valid(rhsMinBounds, rhsMaxBounds)
        && CollisionDetail::MinMaxIntersects(lhsMinBounds, lhsMaxBounds, rhsMinBounds, rhsMaxBounds)
    ;
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL AabbTests::Center(const SIMDVector minBounds, const SIMDVector maxBounds)noexcept{
    return VectorSetW(VectorScale(VectorAdd(minBounds, maxBounds), 0.5f), 0.0f);
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL AabbTests::Extents(const SIMDVector minBounds, const SIMDVector maxBounds)noexcept{
    return VectorSetW(VectorScale(VectorSubtract(maxBounds, minBounds), 0.5f), 0.0f);
}

[[nodiscard]] NWB_INLINE f32 SIMDCALL AabbTests::Radius(const SIMDVector minBounds, const SIMDVector maxBounds)noexcept{
    return VectorGetX(Vector3Length(AabbTests::Extents(minBounds, maxBounds)));
}

[[nodiscard]] inline bool SIMDCALL AabbTests::Transform(
    const SIMDMatrix& localToWorld,
    const SIMDVector localMinBounds,
    const SIMDVector localMaxBounds,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds
)noexcept{
    if(!AabbTests::Valid(localMinBounds, localMaxBounds))
        return false;
    if(MatrixIsNaN(localToWorld) || MatrixIsInfinite(localToWorld))
        return false;

    AabbTests::Reset(outMinBounds, outMaxBounds);
    for(u32 corner = 0u; corner < 8u; ++corner){
        const SIMDVector cornerSelect = VectorSelectControl(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u, 0u);
        const SIMDVector localPoint = VectorSelect(localMinBounds, localMaxBounds, cornerSelect);
        const SIMDVector point = Vector3Transform(localPoint, localToWorld);
        if(Vector3IsNaN(point) || Vector3IsInfinite(point))
            return false;

        AabbTests::Expand(point, outMinBounds, outMaxBounds);
    }
    return AabbTests::Valid(outMinBounds, outMaxBounds);
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL TriangleTests::EdgeCross2D(
    const SIMDVector a,
    const SIMDVector b,
    const SIMDVector c
)noexcept{
    const SIMDVector ab = VectorSubtract(b, a);
    const SIMDVector ac = VectorSubtract(c, a);
    return VectorSubtract(VectorMultiply(VectorSplatX(ab), VectorSplatY(ac)), VectorMultiply(VectorSplatY(ab), VectorSplatX(ac)));
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL TriangleTests::SignedArea2D(
    const SIMDVector a,
    const SIMDVector b,
    const SIMDVector c
)noexcept{
    return VectorScale(TriangleTests::EdgeCross2D(a, b, c), 0.5f);
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL TriangleTests::AreaNormal(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)noexcept{
    return Vector3Cross(VectorSubtract(v1, v0), VectorSubtract(v2, v0));
}

[[nodiscard]] NWB_INLINE bool SIMDCALL TriangleTests::ContainsPoint2D(
    const SIMDVector point,
    const SIMDVector a,
    const SIMDVector b,
    const SIMDVector c,
    const SIMDVector tolerance
)noexcept{
    const SIMDVector negativeTolerance = VectorNegate(VectorSplatX(tolerance));
    const SIMDVector ab = TriangleTests::EdgeCross2D(a, b, point);
    const SIMDVector bc = TriangleTests::EdgeCross2D(b, c, point);
    const SIMDVector ca = TriangleTests::EdgeCross2D(c, a, point);
    return
        VectorGetX(VectorGreaterOrEqual(ab, negativeTolerance)) != 0.0f
        && VectorGetX(VectorGreaterOrEqual(bc, negativeTolerance)) != 0.0f
        && VectorGetX(VectorGreaterOrEqual(ca, negativeTolerance)) != 0.0f
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingSphere::transform(BoundingSphere& outSphere, const SIMDMatrix& matrix)const noexcept{
    SIMDVector scale{};
    SIMDVector rotation{};
    SIMDVector translation{};
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const f32 sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    if(MatrixDecompose(&scale, &rotation, &translation, matrix)){
        const SIMDVector absScale = VectorAbs(scale);
        const f32 maxScale = VectorGetX(CollisionDetail::Vector3MaxComponent(absScale));
        StoreFloat(CollisionDetail::SphereCenterRadius(Vector3Transform(centerVector, matrix), sphereRadius * maxScale), &outSphere.centerRadius);
        return;
    }

    const SIMDVector maxScaleVector = VectorMax(Vector3Length(matrix.v[0]), VectorMax(Vector3Length(matrix.v[1]), Vector3Length(matrix.v[2])));
    const f32 maxScale = VectorGetX(maxScaleVector);
    StoreFloat(CollisionDetail::SphereCenterRadius(Vector3Transform(centerVector, matrix), sphereRadius * maxScale), &outSphere.centerRadius);
}

inline void SIMDCALL BoundingSphere::transform(
    BoundingSphere& outSphere,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector transformedCenter = VectorAdd(Vector3Rotate(VectorScale(CollisionDetail::SphereCenter(sphereValue), scale), rotation), translation);
    StoreFloat(CollisionDetail::SphereCenterRadius(transformedCenter, CollisionDetail::SphereRadius(sphereValue) * Abs(scale)), &outSphere.centerRadius);
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::contains(const SIMDVector point)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector delta = VectorSubtract(point, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector sphereRadius = VectorReplicate(CollisionDetail::SphereRadius(sphereValue));
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(delta), VectorMultiply(sphereRadius, sphereRadius))) ? ContainmentType::Contains : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    if(CollisionDetail::PointsInsideSphere(points, CollisionDetail::s_TriangleVertexCount, CollisionDetail::SphereCenter(sphereValue), VectorReplicate(CollisionDetail::SphereRadius(sphereValue))))
        return ContainmentType::Contains;
    return intersects(v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingSphere& sphere)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector otherSphereValue = LoadFloat(sphere.centerRadius);
    const f32 sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const f32 otherRadius = CollisionDetail::SphereRadius(otherSphereValue);
    const SIMDVector delta = VectorSubtract(CollisionDetail::SphereCenter(otherSphereValue), CollisionDetail::SphereCenter(sphereValue));
    const f32 distanceSq = VectorGetX(Vector3LengthSq(delta));
    if(sphereRadius >= otherRadius){
        const f32 radiusDelta = sphereRadius - otherRadius;
        if(distanceSq <= radiusDelta * radiusDelta)
            return ContainmentType::Contains;
    }

    const f32 radiusSum = sphereRadius + otherRadius;
    if(distanceSq >= radiusSum * radiusSum)
        return ContainmentType::Disjoint;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingBox& box)const noexcept{
    if(!intersects(box))
        return ContainmentType::Disjoint;

    SIMDVector corners[BoundingBox::s_CornerCount];
    CollisionDetail::AabbCorners(LoadFloat(box.center), LoadFloat(box.extents), corners);
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    if(CollisionDetail::PointsInsideSphere(corners, BoundingBox::s_CornerCount, CollisionDetail::SphereCenter(sphereValue), VectorReplicate(CollisionDetail::SphereRadius(sphereValue))))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingOrientedBox& box)const noexcept{
    if(!intersects(box))
        return ContainmentType::Disjoint;

    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation), corners);
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    if(CollisionDetail::PointsInsideSphere(corners, BoundingOrientedBox::s_CornerCount, CollisionDetail::SphereCenter(sphereValue), VectorReplicate(CollisionDetail::SphereRadius(sphereValue))))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingFrustum& frustum)const noexcept{
    if(!intersects(frustum))
        return ContainmentType::Disjoint;

    SIMDVector corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        corners
    );
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    if(CollisionDetail::PointsInsideSphere(corners, BoundingFrustum::s_CornerCount, CollisionDetail::SphereCenter(sphereValue), VectorReplicate(CollisionDetail::SphereRadius(sphereValue))))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingSphere& sphere)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector otherSphereValue = LoadFloat(sphere.centerRadius);
    const f32 radiusSum = CollisionDetail::SphereRadius(sphereValue) + CollisionDetail::SphereRadius(otherSphereValue);
    const SIMDVector delta = VectorSubtract(CollisionDetail::SphereCenter(otherSphereValue), CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector radiusSumVector = VectorReplicate(radiusSum);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(delta), VectorMultiply(radiusSumVector, radiusSumVector)));
}

[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingBox& box)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(box.center), LoadFloat(box.extents), minBounds, maxBounds);
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = VectorReplicate(CollisionDetail::SphereRadius(sphereValue));
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(centerVector, minBounds, maxBounds);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, centerVector)), VectorMultiply(sphereRadius, sphereRadius)));
}

[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingOrientedBox& box)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector sphereRadius = VectorReplicate(CollisionDetail::SphereRadius(sphereValue));
    const SIMDVector localCenter = CollisionDetail::PointToObbLocal(CollisionDetail::SphereCenter(sphereValue), LoadFloat(box.center), LoadFloat(box.orientation));
    const SIMDVector extents = LoadFloat(box.extents);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(localCenter, VectorNegate(extents), extents);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, localCenter)), VectorMultiply(sphereRadius, sphereRadius)));
}

[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingFrustum& frustum)const noexcept{
    return frustum.intersects(*this);
}

[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = VectorReplicate(CollisionDetail::SphereRadius(sphereValue));
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnTriangle(centerVector, v0, v1, v2);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, centerVector)), VectorMultiply(sphereRadius, sphereRadius)));
}

[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingSphere::intersects(const SIMDVector plane)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector distance = CollisionDetail::PlaneDistance(plane, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector sphereRadius = VectorReplicate(CollisionDetail::SphereRadius(sphereValue));
    if(CollisionDetail::Vector4AllTrue(VectorGreater(distance, sphereRadius)))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(VectorLess(distance, VectorNegate(sphereRadius))))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersects(
    const SIMDVector origin,
    const SIMDVector direction,
    f32& outDistance
)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector sphereRadius = VectorReplicate(CollisionDetail::SphereRadius(sphereValue));
    const SIMDVector localOrigin = VectorSubtract(origin, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector bVector = Vector3Dot(localOrigin, direction);
    const SIMDVector cVector = VectorSubtract(Vector3LengthSq(localOrigin), VectorMultiply(sphereRadius, sphereRadius));
    const f32 b = VectorGetX(bVector);
    const f32 c = VectorGetX(cVector);
    if(c > 0.0f && b > 0.0f)
        return false;

    const SIMDVector discriminantVector = VectorSubtract(VectorMultiply(bVector, bVector), cVector);
    const f32 discriminant = VectorGetX(discriminantVector);
    if(discriminant < 0.0f)
        return false;

    const SIMDVector distance = VectorSubtract(VectorNegate(bVector), VectorSqrt(discriminantVector));
    outDistance = Max(0.0f, VectorGetX(distance));
    return true;
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containedBy(
    const SIMDVector plane0,
    const SIMDVector plane1,
    const SIMDVector plane2,
    const SIMDVector plane3,
    const SIMDVector plane4,
    const SIMDVector plane5
)const noexcept{
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = VectorReplicate(CollisionDetail::SphereRadius(sphereValue));
    SIMDVector anyIntersecting = VectorFalseInt();
    for(const SIMDVector plane : planes){
        SIMDVector outside{};
        SIMDVector inside{};
        CollisionDetail::FastIntersectSpherePlane(centerVector, sphereRadius, plane, outside, inside);
        if(CollisionDetail::Vector4AllTrue(outside))
            return ContainmentType::Disjoint;
        anyIntersecting = VectorOrInt(anyIntersecting, VectorEqualInt(inside, VectorFalseInt()));
    }
    return CollisionDetail::Vector4AllTrue(anyIntersecting) ? ContainmentType::Intersects : ContainmentType::Contains;
}

inline void BoundingSphere::createMerged(
    BoundingSphere& outSphere,
    const BoundingSphere& sphere0,
    const BoundingSphere& sphere1
)noexcept{
    const SIMDVector sphereValue0 = LoadFloat(sphere0.centerRadius);
    const SIMDVector sphereValue1 = LoadFloat(sphere1.centerRadius);
    const SIMDVector center0 = CollisionDetail::SphereCenter(sphereValue0);
    const SIMDVector center1 = CollisionDetail::SphereCenter(sphereValue1);
    const f32 radius0 = CollisionDetail::SphereRadius(sphereValue0);
    const f32 radius1 = CollisionDetail::SphereRadius(sphereValue1);
    const SIMDVector delta = VectorSubtract(center1, center0);
    const SIMDVector distanceSquared = Vector3LengthSq(delta);
    const f32 distanceSq = VectorGetX(distanceSquared);
    const f32 radiusDelta = radius0 - radius1;

    if(radiusDelta * radiusDelta >= distanceSq){
        outSphere = radius0 >= radius1 ? sphere0 : sphere1;
        return;
    }

    const SIMDVector distanceVector = VectorSqrt(distanceSquared);
    const f32 distance = VectorGetX(distanceVector);
    const SIMDVector newRadiusVector = VectorMultiply(
        VectorAdd(distanceVector, VectorReplicate(radius0 + radius1)),
        s_SIMDOneHalf
    );
    const f32 newRadius = VectorGetX(newRadiusVector);
    SIMDVector newCenter = center0;
    if(distance > CollisionDetail::s_RayEpsilon)
        newCenter = VectorMultiplyAdd(delta, VectorReplicate((newRadius - radius0) / distance), center0);

    StoreFloat(CollisionDetail::SphereCenterRadius(newCenter, newRadius), &outSphere.centerRadius);
}

inline void BoundingSphere::createFromBoundingBox(BoundingSphere& outSphere, const BoundingBox& box)noexcept{
    StoreFloat(CollisionDetail::SphereCenterRadius(LoadFloat(box.center), VectorGetX(Vector3Length(LoadFloat(box.extents)))), &outSphere.centerRadius);
}

inline void BoundingSphere::createFromBoundingBox(BoundingSphere& outSphere, const BoundingOrientedBox& box)noexcept{
    StoreFloat(CollisionDetail::SphereCenterRadius(LoadFloat(box.center), VectorGetX(Vector3Length(LoadFloat(box.extents)))), &outSphere.centerRadius);
}

inline void BoundingSphere::createFromPoints(
    BoundingSphere& outSphere,
    const usize count,
    const Float3U* points,
    const usize stride
)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(count > 0u);
    SIMDVector centerVector = VectorZero();
    for(usize i = 0u; i < count; ++i)
        centerVector = VectorAdd(centerVector, LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, i)));
    centerVector = VectorDivide(centerVector, VectorReplicate(static_cast<f32>(count)));

    SIMDVector radiusSq = VectorZero();
    for(usize i = 0u; i < count; ++i){
        const SIMDVector delta = VectorSubtract(LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, i)), centerVector);
        radiusSq = VectorMax(radiusSq, Vector3LengthSq(delta));
    }

    StoreFloat(CollisionDetail::SphereCenterRadius(centerVector, VectorGetX(VectorSqrt(radiusSq))), &outSphere.centerRadius);
}

inline void BoundingSphere::createFromFrustum(BoundingSphere& outSphere, const BoundingFrustum& frustum)noexcept{
    SIMDVector corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        corners
    );
    StoreFloat(CollisionDetail::CreateSphereFromVectorPoints(corners, BoundingFrustum::s_CornerCount), &outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingBox::transform(BoundingBox& outBox, const SIMDMatrix& matrix)const noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::AabbCorners(LoadFloat(center), LoadFloat(extents), corners);
    SIMDVector minBounds = Vector3Transform(corners[0], matrix);
    SIMDVector maxBounds = minBounds;
    for(u32 i = 1u; i < s_CornerCount; ++i)
        CollisionDetail::ExpandMinMax(Vector3Transform(corners[i], matrix), minBounds, maxBounds);
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, centerVector, extentsVector);
    StoreFloat(centerVector, &outBox.center);
    StoreFloat(extentsVector, &outBox.extents);
}

inline void SIMDCALL BoundingBox::transform(
    BoundingBox& outBox,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::AabbCorners(LoadFloat(center), LoadFloat(extents), corners);
    const SIMDVector scaleVector = VectorReplicate(scale);
    SIMDVector minBounds = VectorAdd(Vector3Rotate(VectorMultiply(corners[0], scaleVector), rotation), translation);
    SIMDVector maxBounds = minBounds;
    for(u32 i = 1u; i < s_CornerCount; ++i){
        const SIMDVector transformed = VectorAdd(Vector3Rotate(VectorMultiply(corners[i], scaleVector), rotation), translation);
        CollisionDetail::ExpandMinMax(transformed, minBounds, maxBounds);
    }
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, centerVector, extentsVector);
    StoreFloat(centerVector, &outBox.center);
    StoreFloat(extentsVector, &outBox.extents);
}

inline void BoundingBox::getCorners(Float3U* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVector cornerVectors[s_CornerCount];
    CollisionDetail::AabbCorners(LoadFloat(center), LoadFloat(extents), cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), &corners[i]);
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::contains(const SIMDVector point)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    return Vector3GreaterOrEqual(point, minBounds) && Vector3LessOrEqual(point, maxBounds) ? ContainmentType::Contains : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(points, CollisionDetail::s_TriangleVertexCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return intersects(v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingSphere& sphere)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    const SIMDVector sphereValue = LoadFloat(sphere.centerRadius);
    const SIMDVector sphereCenter = CollisionDetail::SphereCenter(sphereValue);
    const f32 sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(sphereCenter, minBounds, maxBounds);
    const SIMDVector radiusVector = VectorReplicate(sphereRadius);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(Vector3LengthSq(VectorSubtract(closestPoint, sphereCenter)), VectorMultiply(radiusVector, radiusVector))))
        return ContainmentType::Disjoint;

    if(Vector3GreaterOrEqual(VectorSubtract(sphereCenter, radiusVector), minBounds) && Vector3LessOrEqual(VectorAdd(sphereCenter, radiusVector), maxBounds))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingBox& box)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    SIMDVector otherMin{};
    SIMDVector otherMax{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(box.center), LoadFloat(box.extents), otherMin, otherMax);
    if(!CollisionDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax))
        return ContainmentType::Disjoint;
    if(Vector3GreaterOrEqual(otherMin, minBounds) && Vector3LessOrEqual(otherMax, maxBounds))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingOrientedBox& box)const noexcept{
    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation), corners);
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(corners, BoundingOrientedBox::s_CornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return intersects(box) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingFrustum& frustum)const noexcept{
    SIMDVector corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        corners
    );
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(corners, BoundingFrustum::s_CornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return intersects(frustum) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline bool BoundingBox::intersects(const BoundingSphere& sphere)const noexcept{
    return sphere.intersects(*this);
}

[[nodiscard]] inline bool BoundingBox::intersects(const BoundingBox& box)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    SIMDVector otherMin{};
    SIMDVector otherMax{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(box.center), LoadFloat(box.extents), otherMin, otherMax);
    return CollisionDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax);
}

[[nodiscard]] inline bool BoundingBox::intersects(const BoundingOrientedBox& box)const noexcept{
    return CollisionDetail::ObbIntersectsObb(
        LoadFloat(center),
        LoadFloat(extents),
        s_SIMDIdentityR3,
        LoadFloat(box.center),
        LoadFloat(box.extents),
        LoadFloat(box.orientation)
    );
}

[[nodiscard]] inline bool BoundingBox::intersects(const BoundingFrustum& frustum)const noexcept{
    return frustum.intersects(*this);
}

[[nodiscard]] inline bool SIMDCALL BoundingBox::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    return CollisionDetail::TriangleAabbOverlap(v0, v1, v2, minBounds, maxBounds);
}

[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingBox::intersects(const SIMDVector plane)const noexcept{
    SIMDVector outside{};
    SIMDVector inside{};
    CollisionDetail::FastIntersectAxisAlignedBoxPlane(LoadFloat(center), LoadFloat(extents), plane, outside, inside);
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline bool SIMDCALL BoundingBox::intersects(
    const SIMDVector origin,
    const SIMDVector direction,
    f32& outDistance
)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    return CollisionDetail::RayIntersectsMinMax(origin, direction, minBounds, maxBounds, outDistance);
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::containedBy(
    const SIMDVector plane0,
    const SIMDVector plane1,
    const SIMDVector plane2,
    const SIMDVector plane3,
    const SIMDVector plane4,
    const SIMDVector plane5
)const noexcept{
    SIMDVector points[s_CornerCount];
    CollisionDetail::AabbCorners(LoadFloat(center), LoadFloat(extents), points);
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(points, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

inline void BoundingBox::createMerged(BoundingBox& outBox, const BoundingBox& box0, const BoundingBox& box1)noexcept{
    SIMDVector min0{};
    SIMDVector max0{};
    SIMDVector min1{};
    SIMDVector max1{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(box0.center), LoadFloat(box0.extents), min0, max0);
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(box1.center), LoadFloat(box1.extents), min1, max1);
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(VectorMin(min0, min1), VectorMax(max0, max1), centerVector, extentsVector);
    StoreFloat(centerVector, &outBox.center);
    StoreFloat(extentsVector, &outBox.extents);
}

inline void BoundingBox::createFromSphere(BoundingBox& outBox, const BoundingSphere& sphere)noexcept{
    const SIMDVector sphereValue = LoadFloat(sphere.centerRadius);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector extentsVector = VectorReplicate(CollisionDetail::SphereRadius(sphereValue));
    StoreFloat(VectorSetW(centerVector, 0.0f), &outBox.center);
    StoreFloat(VectorSetW(extentsVector, 0.0f), &outBox.extents);
}

inline void SIMDCALL BoundingBox::createFromPoints(BoundingBox& outBox, const SIMDVector point0, const SIMDVector point1)noexcept{
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(VectorMin(point0, point1), VectorMax(point0, point1), centerVector, extentsVector);
    StoreFloat(centerVector, &outBox.center);
    StoreFloat(extentsVector, &outBox.extents);
}

inline void BoundingBox::createFromPoints(
    BoundingBox& outBox,
    const usize count,
    const Float3U* points,
    const usize stride
)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(count > 0u);
    SIMDVector minBounds = LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, 0u));
    SIMDVector maxBounds = minBounds;
    for(usize i = 1u; i < count; ++i)
        CollisionDetail::ExpandMinMax(LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, i)), minBounds, maxBounds);
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, centerVector, extentsVector);
    StoreFloat(centerVector, &outBox.center);
    StoreFloat(extentsVector, &outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingOrientedBox::transform(BoundingOrientedBox& outBox, const SIMDMatrix& matrix)const noexcept{
    SIMDVector scale{};
    SIMDVector rotation{};
    SIMDVector translation{};
    if(!MatrixDecompose(&scale, &rotation, &translation, matrix)){
        SIMDVector corners[s_CornerCount];
        CollisionDetail::ObbCorners(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), corners);
        SIMDVector minBounds = Vector3Transform(corners[0], matrix);
        SIMDVector maxBounds = minBounds;
        for(u32 i = 1u; i < s_CornerCount; ++i)
            CollisionDetail::ExpandMinMax(Vector3Transform(corners[i], matrix), minBounds, maxBounds);

        SIMDVector centerVector{};
        SIMDVector extentsVector{};
        CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, centerVector, extentsVector);
        StoreFloat(centerVector, &outBox.center);
        StoreFloat(extentsVector, &outBox.extents);
        outBox.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    const SIMDVector centerVector = Vector3Transform(LoadFloat(center), matrix);
    const SIMDVector extentsVector = VectorMultiply(LoadFloat(extents), VectorAbs(scale));
    const SIMDVector orientationVector = QuaternionNormalize(QuaternionMultiply(LoadFloat(orientation), rotation));
    StoreFloat(VectorSetW(centerVector, 0.0f), &outBox.center);
    StoreFloat(VectorSetW(extentsVector, 0.0f), &outBox.extents);
    StoreFloat(orientationVector, &outBox.orientation);
}

inline void SIMDCALL BoundingOrientedBox::transform(
    BoundingOrientedBox& outBox,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{
    const SIMDVector centerVector = VectorAdd(Vector3Rotate(VectorScale(LoadFloat(center), scale), rotation), translation);
    const SIMDVector extentsVector = VectorScale(LoadFloat(extents), Abs(scale));
    const SIMDVector orientationVector = QuaternionNormalize(QuaternionMultiply(LoadFloat(orientation), rotation));
    StoreFloat(VectorSetW(centerVector, 0.0f), &outBox.center);
    StoreFloat(VectorSetW(extentsVector, 0.0f), &outBox.extents);
    StoreFloat(orientationVector, &outBox.orientation);
}

inline void BoundingOrientedBox::getCorners(Float3U* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVector cornerVectors[s_CornerCount];
    CollisionDetail::ObbCorners(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), &corners[i]);
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::contains(const SIMDVector point)const noexcept{
    return CollisionDetail::PointInsideObb(point, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation)) ? ContainmentType::Contains : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    if(CollisionDetail::PointsInsideObb(points, CollisionDetail::s_TriangleVertexCount, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation)))
        return ContainmentType::Contains;
    return intersects(v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingSphere& sphere)const noexcept{
    const SIMDVector sphereValue = LoadFloat(sphere.centerRadius);
    const f32 sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector localCenter = CollisionDetail::PointToObbLocal(CollisionDetail::SphereCenter(sphereValue), LoadFloat(center), LoadFloat(orientation));
    const SIMDVector extentsVector = LoadFloat(extents);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(localCenter, VectorNegate(extentsVector), extentsVector);
    const SIMDVector radiusVector = VectorReplicate(sphereRadius);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(Vector3LengthSq(VectorSubtract(closestPoint, localCenter)), VectorMultiply(radiusVector, radiusVector))))
        return ContainmentType::Disjoint;

    if(Vector3LessOrEqual(VectorAbs(localCenter), VectorSubtract(extentsVector, radiusVector)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingBox& box)const noexcept{
    SIMDVector corners[BoundingBox::s_CornerCount];
    CollisionDetail::AabbCorners(LoadFloat(box.center), LoadFloat(box.extents), corners);
    if(CollisionDetail::PointsInsideObb(corners, BoundingBox::s_CornerCount, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation)))
        return ContainmentType::Contains;
    return intersects(box) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingOrientedBox& box)const noexcept{
    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation), corners);
    if(CollisionDetail::PointsInsideObb(corners, BoundingOrientedBox::s_CornerCount, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation)))
        return ContainmentType::Contains;
    return intersects(box) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingFrustum& frustum)const noexcept{
    SIMDVector corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        corners
    );
    if(CollisionDetail::PointsInsideObb(corners, BoundingFrustum::s_CornerCount, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation)))
        return ContainmentType::Contains;
    return intersects(frustum) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingSphere& sphere)const noexcept{
    return sphere.intersects(*this);
}

[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingBox& box)const noexcept{
    return CollisionDetail::ObbIntersectsObb(
        LoadFloat(center),
        LoadFloat(extents),
        LoadFloat(orientation),
        LoadFloat(box.center),
        LoadFloat(box.extents),
        s_SIMDIdentityR3
    );
}

[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingOrientedBox& box)const noexcept{
    return CollisionDetail::ObbIntersectsObb(
        LoadFloat(center),
        LoadFloat(extents),
        LoadFloat(orientation),
        LoadFloat(box.center),
        LoadFloat(box.extents),
        LoadFloat(box.orientation)
    );
}

[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingFrustum& frustum)const noexcept{
    return frustum.intersects(*this);
}

[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    const SIMDVector centerVector = LoadFloat(center);
    const SIMDVector orientationVector = LoadFloat(orientation);
    const SIMDVector localV0 = CollisionDetail::PointToObbLocal(v0, centerVector, orientationVector);
    const SIMDVector localV1 = CollisionDetail::PointToObbLocal(v1, centerVector, orientationVector);
    const SIMDVector localV2 = CollisionDetail::PointToObbLocal(v2, centerVector, orientationVector);
    const SIMDVector extentsVector = LoadFloat(extents);
    return CollisionDetail::TriangleAabbOverlap(localV0, localV1, localV2, VectorNegate(extentsVector), extentsVector);
}

[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingOrientedBox::intersects(const SIMDVector plane)const noexcept{
    SIMDVector outside{};
    SIMDVector inside{};
    SIMDVector axis0{};
    SIMDVector axis1{};
    SIMDVector axis2{};
    CollisionDetail::ObbAxes(LoadFloat(orientation), axis0, axis1, axis2);
    CollisionDetail::FastIntersectOrientedBoxPlane(
        LoadFloat(center),
        LoadFloat(extents),
        axis0,
        axis1,
        axis2,
        plane,
        outside,
        inside
    );
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersects(
    const SIMDVector origin,
    const SIMDVector direction,
    f32& outDistance
)const noexcept{
    const SIMDVector orientationVector = LoadFloat(orientation);
    const SIMDVector localOrigin = CollisionDetail::PointToObbLocal(origin, LoadFloat(center), orientationVector);
    const SIMDVector localDirection = Vector3InverseRotate(direction, orientationVector);
    const SIMDVector extentsVector = LoadFloat(extents);
    return CollisionDetail::RayIntersectsMinMax(localOrigin, localDirection, VectorNegate(extentsVector), extentsVector, outDistance);
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containedBy(
    const SIMDVector plane0,
    const SIMDVector plane1,
    const SIMDVector plane2,
    const SIMDVector plane3,
    const SIMDVector plane4,
    const SIMDVector plane5
)const noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::ObbCorners(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), corners);
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

inline void BoundingOrientedBox::createFromBoundingBox(BoundingOrientedBox& outBox, const BoundingBox& box)noexcept{
    outBox.center = box.center;
    outBox.extents = box.extents;
    outBox.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
}

inline void BoundingOrientedBox::createFromPoints(
    BoundingOrientedBox& outBox,
    const usize count,
    const Float3U* points,
    const usize stride
)noexcept{
    BoundingBox box;
    BoundingBox::createFromPoints(box, count, points, stride);
    createFromBoundingBox(outBox, box);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline BoundingFrustum::BoundingFrustum(const SIMDMatrix& projection, const bool rightHandedCoordinates)noexcept{
    createFromMatrix(*this, projection, rightHandedCoordinates);
}

inline void SIMDCALL BoundingFrustum::transform(BoundingFrustum& outFrustum, const SIMDMatrix& matrix)const noexcept{
    SIMDVector scale{};
    SIMDVector rotation{};
    SIMDVector translation{};
    if(!MatrixDecompose(&scale, &rotation, &translation, matrix)){
        scale = s_SIMDOne;
        rotation = s_SIMDIdentityR3;
        translation = Vector3Transform(VectorZero(), matrix);
    }

    const SIMDVector absScale = VectorAbs(scale);
    const f32 maxScale = VectorGetX(CollisionDetail::Vector3MaxComponent(absScale));
    const SIMDVector originVector = Vector3Transform(LoadFloat(origin), matrix);
    const SIMDVector orientationVector = QuaternionNormalize(QuaternionMultiply(LoadFloat(orientation), rotation));
    StoreFloat(VectorSetW(originVector, 0.0f), &outFrustum.origin);
    StoreFloat(orientationVector, &outFrustum.orientation);
    outFrustum.rightSlope = rightSlope;
    outFrustum.leftSlope = leftSlope;
    outFrustum.topSlope = topSlope;
    outFrustum.bottomSlope = bottomSlope;
    outFrustum.nearPlane = nearPlane * maxScale;
    outFrustum.farPlane = farPlane * maxScale;
}

inline void SIMDCALL BoundingFrustum::transform(
    BoundingFrustum& outFrustum,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{
    const SIMDVector originVector = VectorAdd(Vector3Rotate(VectorScale(LoadFloat(origin), scale), rotation), translation);
    const SIMDVector orientationVector = QuaternionNormalize(QuaternionMultiply(LoadFloat(orientation), rotation));
    StoreFloat(VectorSetW(originVector, 0.0f), &outFrustum.origin);
    StoreFloat(orientationVector, &outFrustum.orientation);
    outFrustum.rightSlope = rightSlope;
    outFrustum.leftSlope = leftSlope;
    outFrustum.topSlope = topSlope;
    outFrustum.bottomSlope = bottomSlope;
    outFrustum.nearPlane = nearPlane * Abs(scale);
    outFrustum.farPlane = farPlane * Abs(scale);
}

inline void BoundingFrustum::getCorners(Float3U* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVector cornerVectors[s_CornerCount];
    CollisionDetail::FrustumCorners(
        LoadFloat(origin),
        LoadFloat(orientation),
        rightSlope,
        leftSlope,
        topSlope,
        bottomSlope,
        nearPlane,
        farPlane,
        cornerVectors
    );
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), &corners[i]);
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::contains(const SIMDVector point)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    for(const SIMDVector plane : planes){
        if(CollisionDetail::Vector4AllTrue(VectorLess(CollisionDetail::PlaneDistance(plane, point), VectorZero())))
            return ContainmentType::Disjoint;
    }
    return ContainmentType::Contains;
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    return CollisionDetail::ContainmentFromPlaneTests(points, CollisionDetail::s_TriangleVertexCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingSphere& sphere)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    const SIMDVector sphereValue = LoadFloat(sphere.centerRadius);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = VectorReplicate(CollisionDetail::SphereRadius(sphereValue));
    SIMDVector anyIntersecting = VectorFalseInt();
    for(const SIMDVector plane : planes){
        SIMDVector outside{};
        SIMDVector inside{};
        CollisionDetail::FastIntersectSpherePlane(centerVector, sphereRadius, plane, outside, inside);
        if(CollisionDetail::Vector4AllTrue(outside))
            return ContainmentType::Disjoint;
        anyIntersecting = VectorOrInt(anyIntersecting, VectorEqualInt(inside, VectorFalseInt()));
    }
    return CollisionDetail::Vector4AllTrue(anyIntersecting) ? ContainmentType::Intersects : ContainmentType::Contains;
}

[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingBox& box)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector corners[BoundingBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    CollisionDetail::AabbCorners(LoadFloat(box.center), LoadFloat(box.extents), corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingOrientedBox& box)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    CollisionDetail::ObbCorners(LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation), corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingOrientedBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingFrustum& frustum)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    CollisionDetail::FrustumCorners(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        corners
    );
    const ContainmentType::Enum containment = CollisionDetail::ContainmentFromPlaneTests(corners, BoundingFrustum::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
    if(containment != ContainmentType::Intersects)
        return containment;

    SIMDVector otherPlanes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector thisCorners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumPlanes(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        otherPlanes
    );
    CollisionDetail::FrustumCorners(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, thisCorners);
    return CollisionDetail::FrustumPlanesIntersectPoints(otherPlanes, thisCorners, BoundingFrustum::s_CornerCount) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingSphere& sphere)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    return CollisionDetail::FrustumPlanesIntersectSphere(planes, LoadFloat(sphere.centerRadius));
}

[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingBox& box)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    return CollisionDetail::FrustumPlanesIntersectAxisAlignedBox(planes, LoadFloat(box.center), LoadFloat(box.extents));
}

[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingOrientedBox& box)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    return CollisionDetail::FrustumPlanesIntersectOrientedBox(planes, LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}

[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingFrustum& frustum)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector otherPlanes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    CollisionDetail::FrustumPlanes(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        otherPlanes
    );

    SIMDVector corners[s_CornerCount];
    SIMDVector otherCorners[s_CornerCount];
    CollisionDetail::FrustumCorners(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, corners);
    CollisionDetail::FrustumCorners(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        otherCorners
    );

    return CollisionDetail::FrustumPlanesIntersectPoints(planes, otherCorners, s_CornerCount)
        && CollisionDetail::FrustumPlanesIntersectPoints(otherPlanes, corners, s_CornerCount);
}

[[nodiscard]] inline bool SIMDCALL BoundingFrustum::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    return contains(v0, v1, v2) != ContainmentType::Disjoint;
}

[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingFrustum::intersects(const SIMDVector plane)const noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::FrustumCorners(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, corners);
    SIMDVector outside{};
    SIMDVector inside{};
    CollisionDetail::FastIntersectPointsPlane(corners, s_CornerCount, plane, outside, inside);
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline bool SIMDCALL BoundingFrustum::intersects(
    const SIMDVector rayOrigin,
    const SIMDVector direction,
    f32& outDistance
)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    f32 tMin = 0.0f;
    f32 tMax = s_MaxF32;
    for(const SIMDVector plane : planes){
        const f32 distance = VectorGetX(CollisionDetail::PlaneDistance(plane, rayOrigin));
        const f32 denominator = VectorGetX(Vector3Dot(plane, direction));
        if(Abs(denominator) <= CollisionDetail::s_RayEpsilon){
            if(distance < 0.0f)
                return false;
            continue;
        }

        const f32 t = -distance / denominator;
        if(denominator > 0.0f)
            tMin = Max(tMin, t);
        else
            tMax = Min(tMax, t);
        if(tMin > tMax)
            return false;
    }

    outDistance = tMin;
    return true;
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::containedBy(
    const SIMDVector plane0,
    const SIMDVector plane1,
    const SIMDVector plane2,
    const SIMDVector plane3,
    const SIMDVector plane4,
    const SIMDVector plane5
)const noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::FrustumCorners(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, corners);
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

inline void BoundingFrustum::getPlanes(
    SIMDVector* nearPlaneOut,
    SIMDVector* farPlaneOut,
    SIMDVector* rightPlaneOut,
    SIMDVector* leftPlaneOut,
    SIMDVector* topPlaneOut,
    SIMDVector* bottomPlaneOut
)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    if(nearPlaneOut)
        *nearPlaneOut = planes[0];
    if(farPlaneOut)
        *farPlaneOut = planes[1];
    if(rightPlaneOut)
        *rightPlaneOut = planes[2];
    if(leftPlaneOut)
        *leftPlaneOut = planes[3];
    if(topPlaneOut)
        *topPlaneOut = planes[4];
    if(bottomPlaneOut)
        *bottomPlaneOut = planes[5];
}

inline void SIMDCALL BoundingFrustum::createFromMatrix(
    BoundingFrustum& outFrustum,
    const SIMDMatrix& projection,
    const bool rightHandedCoordinates
)noexcept{
    outFrustum.origin = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    outFrustum.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    const f32 m00 = VectorGetX(projection.v[0]);
    const f32 m11 = VectorGetY(projection.v[1]);
    const f32 m02 = VectorGetZ(projection.v[0]);
    const f32 m12 = VectorGetZ(projection.v[1]);
    const f32 m22 = VectorGetZ(projection.v[2]);
    const f32 m23 = VectorGetW(projection.v[2]);

    outFrustum.rightSlope = (1.0f - m02) / m00;
    outFrustum.leftSlope = (-1.0f - m02) / m00;
    outFrustum.topSlope = (1.0f - m12) / m11;
    outFrustum.bottomSlope = (-1.0f - m12) / m11;

    if(rightHandedCoordinates){
        outFrustum.nearPlane = m23 / m22;
        outFrustum.farPlane = m23 / (m22 + 1.0f);
    }else{
        outFrustum.nearPlane = -m23 / m22;
        outFrustum.farPlane = m23 / (1.0f - m22);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL TriangleTests::Intersects(
    const SIMDVector origin,
    const SIMDVector direction,
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2,
    f32& outDistance
)noexcept{
    return CollisionDetail::RayIntersectsTriangle(origin, direction, v0, v1, v2, outDistance);
}

[[nodiscard]] inline bool SIMDCALL TriangleTests::Intersects(
    const SIMDVector a0,
    const SIMDVector a1,
    const SIMDVector a2,
    const SIMDVector b0,
    const SIMDVector b1,
    const SIMDVector b2
)noexcept{
    const auto segmentIntersectsTriangle = [](const SIMDVector p0, const SIMDVector p1, const SIMDVector t0, const SIMDVector t1, const SIMDVector t2)noexcept{
        f32 distance = 0.0f;
        const SIMDVector direction = VectorSubtract(p1, p0);
        return CollisionDetail::RayIntersectsTriangle(p0, direction, t0, t1, t2, distance) && distance >= 0.0f && distance <= 1.0f;
    };

    if(segmentIntersectsTriangle(a0, a1, b0, b1, b2) || segmentIntersectsTriangle(a1, a2, b0, b1, b2) || segmentIntersectsTriangle(a2, a0, b0, b1, b2))
        return true;
    if(segmentIntersectsTriangle(b0, b1, a0, a1, a2) || segmentIntersectsTriangle(b1, b2, a0, a1, a2) || segmentIntersectsTriangle(b2, b0, a0, a1, a2))
        return true;

    const SIMDVector minA = VectorMin(a0, VectorMin(a1, a2));
    const SIMDVector maxA = VectorMax(a0, VectorMax(a1, a2));
    const SIMDVector minB = VectorMin(b0, VectorMin(b1, b2));
    const SIMDVector maxB = VectorMax(b0, VectorMax(b1, b2));
    return CollisionDetail::MinMaxIntersects(minA, maxA, minB, maxB);
}

[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL TriangleTests::Intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2,
    const SIMDVector plane
)noexcept{
    const SIMDVector d0 = CollisionDetail::PlaneDistance(plane, v0);
    const SIMDVector d1 = CollisionDetail::PlaneDistance(plane, v1);
    const SIMDVector d2 = CollisionDetail::PlaneDistance(plane, v2);
    const SIMDVector minDistance = VectorMin(d0, VectorMin(d1, d2));
    const SIMDVector maxDistance = VectorMax(d0, VectorMax(d1, d2));
    if(CollisionDetail::Vector4AllTrue(VectorGreater(minDistance, VectorZero())))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(VectorLess(maxDistance, VectorZero())))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline ContainmentType::Enum SIMDCALL TriangleTests::ContainedBy(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2,
    const SIMDVector plane0,
    const SIMDVector plane1,
    const SIMDVector plane2,
    const SIMDVector plane3,
    const SIMDVector plane4,
    const SIMDVector plane5
)noexcept{
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(points, CollisionDetail::s_TriangleVertexCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

