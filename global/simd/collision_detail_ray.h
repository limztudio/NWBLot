// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "collision_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CollisionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f32> RayIntersectsMinMax(const SIMDVectorSingle origin, const SIMDVectorSingle direction, const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    const SIMDVectorSingle center = VectorScale(VectorAdd(minBounds, maxBounds), s_Half);
    const SIMDVectorSingle extents = VectorScale(VectorSubtract(maxBounds, minBounds), s_Half);
    const SIMDVectorSingle axisOrigin = VectorSubtract(center, origin);
    const SIMDVectorSingle isParallel = VectorLessOrEqual(VectorAbs(direction), VectorReplicate(s_RayEpsilon));
    const SIMDVectorSingle inverseDirection = VectorReciprocal(direction);
    const SIMDVectorSingle t1 = VectorMultiply(VectorSubtract(axisOrigin, extents), inverseDirection);
    const SIMDVectorSingle t2 = VectorMultiply(VectorAdd(axisOrigin, extents), inverseDirection);
    const SIMDVectorSingle negativeMax = VectorReplicate(-s_MaxF32);
    const SIMDVectorSingle positiveMax = VectorReplicate(s_MaxF32);

    SIMDVectorSingle tMin = VectorSelect(VectorMin(t1, t2), negativeMax, isParallel);
    SIMDVectorSingle tMax = VectorSelect(VectorMax(t1, t2), positiveMax, isParallel);
    tMin = VectorMax(tMin, VectorSplatY(tMin));
    tMin = VectorMax(tMin, VectorSplatZ(tMin));
    tMax = VectorMin(tMax, VectorSplatY(tMax));
    tMax = VectorMin(tMax, VectorSplatZ(tMax));

    SIMDVectorSingle noIntersection = VectorGreater(VectorSplatX(tMin), VectorSplatX(tMax));
    noIntersection = VectorOrInt(noIntersection, VectorLess(VectorSplatX(tMax), VectorZero()));
    noIntersection = VectorOrInt(noIntersection, VectorAndCInt(isParallel, VectorInBounds(axisOrigin, extents)));

    if(Vector3AnyTrue(noIntersection)){
        return MakeUnexpected(Failure{});
    }

    return VectorGetX(tMin);
}

[[nodiscard]] inline SIMDVectorSingle ClosestPointOnTriangle(const SIMDVectorSingle point, const SIMDVectorSingle a, const SIMDVectorSingle b, const SIMDVectorSingle c)noexcept{
    const SIMDVectorSingle ab = VectorSubtract(b, a);
    const SIMDVectorSingle ac = VectorSubtract(c, a);
    const SIMDVectorSingle ap = VectorSubtract(point, a);
    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle d1 = Vector3Dot(ab, ap);
    const SIMDVectorSingle d2 = Vector3Dot(ac, ap);
    if(Vector4LessOrEqual(d1, zero) && Vector4LessOrEqual(d2, zero))
        return a;

    const SIMDVectorSingle bp = VectorSubtract(point, b);
    const SIMDVectorSingle d3 = Vector3Dot(ab, bp);
    const SIMDVectorSingle d4 = Vector3Dot(ac, bp);
    if(Vector4GreaterOrEqual(d3, zero) && Vector4LessOrEqual(d4, d3))
        return b;

    const SIMDVectorSingle vc = VectorSubtract(VectorMultiply(d1, d4), VectorMultiply(d3, d2));
    if(Vector4LessOrEqual(vc, zero) && Vector4GreaterOrEqual(d1, zero) && Vector4LessOrEqual(d3, zero))
        return VectorMultiplyAdd(ab, VectorDivide(d1, VectorSubtract(d1, d3)), a);

    const SIMDVectorSingle cp = VectorSubtract(point, c);
    const SIMDVectorSingle d5 = Vector3Dot(ab, cp);
    const SIMDVectorSingle d6 = Vector3Dot(ac, cp);
    if(Vector4GreaterOrEqual(d6, zero) && Vector4LessOrEqual(d5, d6))
        return c;

    const SIMDVectorSingle vb = VectorSubtract(VectorMultiply(d5, d2), VectorMultiply(d1, d6));
    if(Vector4LessOrEqual(vb, zero) && Vector4GreaterOrEqual(d2, zero) && Vector4LessOrEqual(d6, zero))
        return VectorMultiplyAdd(ac, VectorDivide(d2, VectorSubtract(d2, d6)), a);

    const SIMDVectorSingle va = VectorSubtract(VectorMultiply(d3, d6), VectorMultiply(d5, d4));
    const SIMDVectorSingle d43 = VectorSubtract(d4, d3);
    const SIMDVectorSingle d56 = VectorSubtract(d5, d6);
    if(Vector4LessOrEqual(va, zero) && Vector4GreaterOrEqual(d43, zero) && Vector4GreaterOrEqual(d56, zero)){
        const SIMDVectorSingle bc = VectorSubtract(c, b);
        return VectorMultiplyAdd(bc, VectorDivide(d43, VectorAdd(d43, d56)), b);
    }

    const SIMDVectorSingle denom = VectorReciprocal(VectorAdd(va, VectorAdd(vb, vc)));
    return VectorMultiplyAdd(ac, VectorMultiply(vc, denom), VectorMultiplyAdd(ab, VectorMultiply(vb, denom), a));
}

[[nodiscard]] inline Expected<f32> RayIntersectsTriangle(const SIMDVectorSingle origin, const SIMDVectorSingle direction, const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2)noexcept{
    const SIMDVectorSingle edge1 = VectorSubtract(v1, v0);
    const SIMDVectorSingle edge2 = VectorSubtract(v2, v0);
    const SIMDVectorSingle p = Vector3Cross(direction, edge2);
    const SIMDVectorSingle determinant = Vector3Dot(edge1, p);
    const SIMDVectorSingle zero = VectorZero();
    if(Vector4LessOrEqual(VectorAbs(determinant), VectorReplicate(s_RayEpsilon)))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle inverseDeterminant = VectorReciprocal(determinant);
    const SIMDVectorSingle s = VectorSubtract(origin, v0);
    const SIMDVectorSingle u = VectorMultiply(Vector3Dot(s, p), inverseDeterminant);
    if(Vector4Less(u, zero) || Vector4Greater(u, s_SIMDOne))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle q = Vector3Cross(s, edge1);
    const SIMDVectorSingle v = VectorMultiply(Vector3Dot(direction, q), inverseDeterminant);
    if(Vector4Less(v, zero) || Vector4Greater(VectorAdd(u, v), s_SIMDOne))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle t = VectorMultiply(Vector3Dot(edge2, q), inverseDeterminant);
    if(Vector4Less(t, zero))
        return MakeUnexpected(Failure{});

    return VectorGetX(t);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CollisionDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f64> RayIntersectsMinMax(const SIMDVectorDouble origin, const SIMDVectorDouble direction, const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    const SIMDVectorDouble center = VectorDoubleScale(VectorDoubleAdd(minBounds, maxBounds), s_DoubleHalf);
    const SIMDVectorDouble extents = VectorDoubleScale(VectorDoubleSubtract(maxBounds, minBounds), s_DoubleHalf);
    const SIMDVectorDouble axisOrigin = VectorDoubleSubtract(center, origin);
    const SIMDVectorDouble isParallel = VectorDoubleLessOrEqual(VectorDoubleAbs(direction), VectorDoubleReplicate(s_DoubleRayEpsilon));
    const SIMDVectorDouble inverseDirection = VectorDoubleReciprocal(direction);
    const SIMDVectorDouble nearOffset = VectorDoubleSubtract(axisOrigin, extents);
    const SIMDVectorDouble t1 = VectorDoubleMultiply(nearOffset, inverseDirection);
    const SIMDVectorDouble t2 = VectorDoubleMultiply(VectorDoubleAdd(axisOrigin, extents), inverseDirection);
    const SIMDVectorDouble negativeMax = VectorDoubleReplicate(-Limit<f64>::s_Max);
    const SIMDVectorDouble positiveMax = VectorDoubleReplicate(Limit<f64>::s_Max);

    SIMDVectorDouble tMin = VectorDoubleSelect(VectorDoubleMin(t1, t2), negativeMax, isParallel);
    SIMDVectorDouble tMax = VectorDoubleSelect(VectorDoubleMax(t1, t2), positiveMax, isParallel);
    tMin = VectorDoubleMax(tMin, VectorDoubleSplatY(tMin));
    tMin = VectorDoubleMax(tMin, VectorDoubleSplatZ(tMin));
    tMax = VectorDoubleMin(tMax, VectorDoubleSplatY(tMax));
    tMax = VectorDoubleMin(tMax, VectorDoubleSplatZ(tMax));

    SIMDVectorDouble noIntersection = VectorDoubleGreater(VectorDoubleSplatX(tMin), VectorDoubleSplatX(tMax));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleLess(VectorDoubleSplatX(tMax), VectorDoubleZero()));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleAndCInt(isParallel, VectorDoubleInBounds(axisOrigin, extents)));

    if(VectorDouble3AnyTrue(noIntersection)){
        return MakeUnexpected(Failure{});
    }

    return VectorDoubleGetX(tMin);
}

[[nodiscard]] inline SIMDVectorDouble ClosestPointOnTriangle(const SIMDVectorDouble point, const SIMDVectorDouble a, const SIMDVectorDouble b, const SIMDVectorDouble c)noexcept{
    const SIMDVectorDouble ab = VectorDoubleSubtract(b, a);
    const SIMDVectorDouble ac = VectorDoubleSubtract(c, a);
    const SIMDVectorDouble ap = VectorDoubleSubtract(point, a);
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble d1 = VectorDouble3Dot(ab, ap);
    const SIMDVectorDouble d2 = VectorDouble3Dot(ac, ap);
    if(VectorDouble4LessOrEqual(d1, zero) && VectorDouble4LessOrEqual(d2, zero))
        return a;

    const SIMDVectorDouble bp = VectorDoubleSubtract(point, b);
    const SIMDVectorDouble d3 = VectorDouble3Dot(ab, bp);
    const SIMDVectorDouble d4 = VectorDouble3Dot(ac, bp);
    if(VectorDouble4GreaterOrEqual(d3, zero) && VectorDouble4LessOrEqual(d4, d3))
        return b;

    const SIMDVectorDouble vc = VectorDoubleSubtract(VectorDoubleMultiply(d1, d4), VectorDoubleMultiply(d3, d2));
    if(VectorDouble4LessOrEqual(vc, zero) && VectorDouble4GreaterOrEqual(d1, zero) && VectorDouble4LessOrEqual(d3, zero))
        return VectorDoubleMultiplyAdd(ab, VectorDoubleDivide(d1, VectorDoubleSubtract(d1, d3)), a);

    const SIMDVectorDouble cp = VectorDoubleSubtract(point, c);
    const SIMDVectorDouble d5 = VectorDouble3Dot(ab, cp);
    const SIMDVectorDouble d6 = VectorDouble3Dot(ac, cp);
    if(VectorDouble4GreaterOrEqual(d6, zero) && VectorDouble4LessOrEqual(d5, d6))
        return c;

    const SIMDVectorDouble vb = VectorDoubleSubtract(VectorDoubleMultiply(d5, d2), VectorDoubleMultiply(d1, d6));
    if(VectorDouble4LessOrEqual(vb, zero) && VectorDouble4GreaterOrEqual(d2, zero) && VectorDouble4LessOrEqual(d6, zero))
        return VectorDoubleMultiplyAdd(ac, VectorDoubleDivide(d2, VectorDoubleSubtract(d2, d6)), a);

    const SIMDVectorDouble va = VectorDoubleSubtract(VectorDoubleMultiply(d3, d6), VectorDoubleMultiply(d5, d4));
    const SIMDVectorDouble d43 = VectorDoubleSubtract(d4, d3);
    const SIMDVectorDouble d56 = VectorDoubleSubtract(d5, d6);
    if(VectorDouble4LessOrEqual(va, zero) && VectorDouble4GreaterOrEqual(d43, zero) && VectorDouble4GreaterOrEqual(d56, zero)){
        const SIMDVectorDouble bc = VectorDoubleSubtract(c, b);
        return VectorDoubleMultiplyAdd(bc, VectorDoubleDivide(d43, VectorDoubleAdd(d43, d56)), b);
    }

    const SIMDVectorDouble denom = VectorDoubleReciprocal(VectorDoubleAdd(va, VectorDoubleAdd(vb, vc)));
    return VectorDoubleMultiplyAdd(ac, VectorDoubleMultiply(vc, denom), VectorDoubleMultiplyAdd(ab, VectorDoubleMultiply(vb, denom), a));
}

[[nodiscard]] inline Expected<f64> RayIntersectsTriangle(const SIMDVectorDouble origin, const SIMDVectorDouble direction, const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2)noexcept{
    const SIMDVectorDouble edge1 = VectorDoubleSubtract(v1, v0);
    const SIMDVectorDouble edge2 = VectorDoubleSubtract(v2, v0);
    const SIMDVectorDouble p = VectorDouble3Cross(direction, edge2);
    const SIMDVectorDouble determinant = VectorDouble3Dot(edge1, p);
    const SIMDVectorDouble zero = VectorDoubleZero();
    if(VectorDouble4LessOrEqual(VectorDoubleAbs(determinant), VectorDoubleReplicate(s_DoubleRayEpsilon)))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble inverseDeterminant = VectorDoubleReciprocal(determinant);
    const SIMDVectorDouble s = VectorDoubleSubtract(origin, v0);
    const SIMDVectorDouble u = VectorDoubleMultiply(VectorDouble3Dot(s, p), inverseDeterminant);
    if(VectorDouble4Less(u, zero) || VectorDouble4Greater(u, s_DoubleSIMDOne))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble q = VectorDouble3Cross(s, edge1);
    const SIMDVectorDouble v = VectorDoubleMultiply(VectorDouble3Dot(direction, q), inverseDeterminant);
    if(VectorDouble4Less(v, zero) || VectorDouble4Greater(VectorDoubleAdd(u, v), s_DoubleSIMDOne))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble t = VectorDoubleMultiply(VectorDouble3Dot(edge2, q), inverseDeterminant);
    if(VectorDouble4Less(t, zero))
        return MakeUnexpected(Failure{});

    return VectorDoubleGetX(t);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

