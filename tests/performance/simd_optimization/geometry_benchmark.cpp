// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/floating_point_environment.h>
#include <global/error_code.h>
#include <global/expected.h>
#include <global/not_null.h>
#include <global/simd/collision_triangle.h>
#include <global/string_view.h>
#include <global/scope_exit.h>
#include <global/timer.h>

#include <iostream>
#if defined(NWB_PLATFORM_LINUX)
#include <fenv.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_simd_geometry_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ZeroAxisLabel = "zero_axis";
constexpr AStringView s_ValidateMode = "validate";
constexpr AStringView s_AbsoluteMode = "abs";
constexpr AStringView s_GuardMode = "guard";

using Intersection = bool (NWB_SIMD_CALL *)(SIMDVectorDouble, SIMDVectorDouble, SIMDVectorDouble, SIMDVectorDouble, SIMDVectorDouble, SIMDVectorDouble)noexcept;

// Fixture storage for one triangle pair.
struct TrianglePair{
    Double4 first[3];
    Double4 second[3];
};

// Loaded triangle pair used by the checks.
struct TrianglePairVectors{
    SIMDVectorDouble first[3];
    SIMDVectorDouble second[3];
};

inline constexpr u32 s_FixtureCount = 64u;
inline constexpr u32 s_Rounds = 7u;
inline constexpr u32 s_Passes = 512u;
static volatile u64 s_Checksum = 0u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL BaselineDot3(SIMDVectorDouble first, SIMDVectorDouble second)noexcept{
    first = VectorDoubleAndInt(first, s_DoubleSIMDMask3);
    second = VectorDoubleAndInt(second, s_DoubleSIMDMask3);
    const SIMDVectorDouble product = VectorDoubleMultiply(first, second);
    SIMDVectorDouble result = VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
    return VectorDoubleAdd(result, VectorDoubleSplatZ(product));
}

NWB_NOINLINE bool NWB_SIMD_CALL Baseline(const SIMDVectorDouble a0, const SIMDVectorDouble a1, const SIMDVectorDouble a2, const SIMDVectorDouble b0, const SIMDVectorDouble b1, const SIMDVectorDouble b2)noexcept{
    const SIMDVectorDouble first0 = VectorDoubleAndInt(a0, s_DoubleSIMDMask3), first1 = VectorDoubleAndInt(a1, s_DoubleSIMDMask3), first2 = VectorDoubleAndInt(a2, s_DoubleSIMDMask3);
    const SIMDVectorDouble second0 = VectorDoubleAndInt(b0, s_DoubleSIMDMask3), second1 = VectorDoubleAndInt(b1, s_DoubleSIMDMask3), second2 = VectorDoubleAndInt(b2, s_DoubleSIMDMask3);
    const SIMDVectorDouble minA = VectorDoubleMin(first0, VectorDoubleMin(first1, first2)), maxA = VectorDoubleMax(first0, VectorDoubleMax(first1, first2));
    const SIMDVectorDouble minB = VectorDoubleMin(second0, VectorDoubleMin(second1, second2)), maxB = VectorDoubleMax(second0, VectorDoubleMax(second1, second2));
    if(!CollisionDoubleDetail::MinMaxIntersects(minA, maxA, minB, maxB))
        return false;

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble relativeA1 = VectorDoubleSubtract(first1, first0), relativeA2 = VectorDoubleSubtract(first2, first0);
    const SIMDVectorDouble relativeB0 = VectorDoubleSubtract(second0, first0), relativeB1 = VectorDoubleSubtract(second1, first0), relativeB2 = VectorDoubleSubtract(second2, first0);
    const auto differenceRoundoff = [](const SIMDVectorDouble point, const SIMDVectorDouble origin, const SIMDVectorDouble difference)noexcept{
        // TwoDiff recovers the subtraction residual without expanding the allowance with a large common translation.
        const SIMDVectorDouble virtualOrigin = VectorDoubleSubtract(point, difference), virtualPoint = VectorDoubleAdd(difference, virtualOrigin);
        return VectorDoubleAdd(VectorDoubleSubtract(point, virtualPoint), VectorDoubleSubtract(virtualOrigin, origin));
    };
    const SIMDVectorDouble roundoffA1 = differenceRoundoff(first1, first0, relativeA1), roundoffA2 = differenceRoundoff(first2, first0, relativeA2);
    const SIMDVectorDouble roundoffB0 = differenceRoundoff(second0, first0, relativeB0), roundoffB1 = differenceRoundoff(second1, first0, relativeB1), roundoffB2 = differenceRoundoff(second2, first0, relativeB2);
    const SIMDVectorDouble edgeA0 = relativeA1, edgeA1 = VectorDoubleSubtract(first2, first1), edgeA2 = VectorDoubleNegate(relativeA2);
    const SIMDVectorDouble edgeB0 = VectorDoubleSubtract(second1, second0), edgeB1 = VectorDoubleSubtract(second2, second1), edgeB2 = VectorDoubleSubtract(second0, second2);
    const auto crossAxis = [](const SIMDVectorDouble first, const SIMDVectorDouble second)noexcept{
        // Separate products make parallel directions produce a zero axis instead of an explicit-FMA residual.
        const SIMDVectorDouble positive = VectorDoubleMultiply(VectorDoubleSwizzle<1, 2, 0, 3>(first), VectorDoubleSwizzle<2, 0, 1, 3>(second));
        const SIMDVectorDouble negative = VectorDoubleMultiply(VectorDoubleSwizzle<2, 0, 1, 3>(first), VectorDoubleSwizzle<1, 2, 0, 3>(second));
        return VectorDoubleSubtract(positive, negative);
    };
    const auto separated = [&](const SIMDVectorDouble axis)noexcept{
        const SIMDVectorDouble absoluteAxis = VectorDoubleAbs(axis);
        const auto projectionInterval = [&](const SIMDVectorDouble relative, const SIMDVectorDouble roundoff)noexcept{
            const SIMDVectorDouble projection = BaselineDot3(relative, axis);
            const SIMDVectorDouble magnitude = BaselineDot3(VectorDoubleAbs(relative), absoluteAxis), coordinateError = BaselineDot3(VectorDoubleAbs(roundoff), absoluteAxis);
            // Three products and two additions need an absolute cancellation bound; eight epsilons also cover rounding the bound and endpoints.
            const SIMDVectorDouble allowance = VectorDoubleMultiplyAddExpression(VectorDoubleAdd(magnitude, coordinateError), VectorDoubleReplicate(0x1p-49), coordinateError);
            return VectorDoubleSelect(VectorDoubleSubtract(projection, allowance), VectorDoubleAdd(projection, allowance), s_DoubleSIMDMaskY);
        };
        const SIMDVectorDouble intervalA1 = projectionInterval(relativeA1, roundoffA1), intervalA2 = projectionInterval(relativeA2, roundoffA2);
        const SIMDVectorDouble minimumA = VectorDoubleMin(zero, VectorDoubleMin(VectorDoubleSplatX(intervalA1), VectorDoubleSplatX(intervalA2)));
        const SIMDVectorDouble maximumA = VectorDoubleMax(zero, VectorDoubleMax(VectorDoubleSplatY(intervalA1), VectorDoubleSplatY(intervalA2)));
        const SIMDVectorDouble intervalB0 = projectionInterval(relativeB0, roundoffB0), intervalB1 = projectionInterval(relativeB1, roundoffB1), intervalB2 = projectionInterval(relativeB2, roundoffB2);
        const SIMDVectorDouble minimumB = VectorDoubleMin(VectorDoubleSplatX(intervalB0), VectorDoubleMin(VectorDoubleSplatX(intervalB1), VectorDoubleSplatX(intervalB2)));
        const SIMDVectorDouble maximumB = VectorDoubleMax(VectorDoubleSplatY(intervalB0), VectorDoubleMax(VectorDoubleSplatY(intervalB1), VectorDoubleSplatY(intervalB2)));
        return VectorDouble4Less(maximumA, minimumB) || VectorDouble4Less(maximumB, minimumA);
    };
    const SIMDVectorDouble normalA = crossAxis(edgeA0, VectorDoubleNegate(edgeA2)), normalB = crossAxis(edgeB0, VectorDoubleNegate(edgeB2));
    if(separated(normalA) || separated(normalB))
        return false;

    if(VectorDouble3Equal(normalA, zero) && VectorDouble3Equal(normalB, zero)){
        // Closed degenerate triangles retain their point/segment hulls, including collinear endpoint contact.
        const SIMDVectorDouble directionA = VectorDouble3Equal(edgeA0, zero) ? edgeA1 : edgeA0, directionB = VectorDouble3Equal(edgeB0, zero) ? edgeB1 : edgeB0;
        const SIMDVectorDouble normal = crossAxis(directionA, directionB);
        if(separated(directionA) || separated(directionB) || separated(normal) || separated(crossAxis(directionA, normal)) || separated(crossAxis(directionB, normal)))
            return false;
        const auto parallelSeparated = [&](const SIMDVectorDouble direction)noexcept{
            return separated(crossAxis(direction, s_DoubleSIMDIdentityR0)) || separated(crossAxis(direction, s_DoubleSIMDIdentityR1)) || separated(crossAxis(direction, s_DoubleSIMDIdentityR2));
        };
        return !parallelSeparated(directionA) && !parallelSeparated(directionB);
    }

    const auto edgeSeparated = [&](const SIMDVectorDouble edge)noexcept{
        // In-plane edge normals are necessary when the triangles are coplanar or one hull is degenerate.
        return separated(crossAxis(edge, normalA)) || separated(crossAxis(edge, normalB)) || separated(crossAxis(edge, edgeB0)) || separated(crossAxis(edge, edgeB1)) || separated(crossAxis(edge, edgeB2));
    };
    if(edgeSeparated(edgeA0) || edgeSeparated(edgeA1) || edgeSeparated(edgeA2))
        return false;
    const auto secondEdgeSeparated = [&](const SIMDVectorDouble edge)noexcept{
        return separated(crossAxis(edge, normalA)) || separated(crossAxis(edge, normalB));
    };
    return !secondEdgeSeparated(edgeB0) && !secondEdgeSeparated(edgeB1) && !secondEdgeSeparated(edgeB2);
}

NWB_NOINLINE bool NWB_SIMD_CALL CurrentDot3Baseline(const SIMDVectorDouble a0, const SIMDVectorDouble a1, const SIMDVectorDouble a2, const SIMDVectorDouble b0, const SIMDVectorDouble b1, const SIMDVectorDouble b2)noexcept{
    const SIMDVectorDouble first0 = VectorDoubleAndInt(a0, s_DoubleSIMDMask3), first1 = VectorDoubleAndInt(a1, s_DoubleSIMDMask3), first2 = VectorDoubleAndInt(a2, s_DoubleSIMDMask3);
    const SIMDVectorDouble second0 = VectorDoubleAndInt(b0, s_DoubleSIMDMask3), second1 = VectorDoubleAndInt(b1, s_DoubleSIMDMask3), second2 = VectorDoubleAndInt(b2, s_DoubleSIMDMask3);
    const SIMDVectorDouble minA = VectorDoubleMin(first0, VectorDoubleMin(first1, first2)), maxA = VectorDoubleMax(first0, VectorDoubleMax(first1, first2));
    const SIMDVectorDouble minB = VectorDoubleMin(second0, VectorDoubleMin(second1, second2)), maxB = VectorDoubleMax(second0, VectorDoubleMax(second1, second2));
    if(!CollisionDoubleDetail::MinMaxIntersects(minA, maxA, minB, maxB))
        return false;

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble relativeA1 = VectorDoubleSubtract(first1, first0), relativeA2 = VectorDoubleSubtract(first2, first0);
    const SIMDVectorDouble relativeB0 = VectorDoubleSubtract(second0, first0), relativeB1 = VectorDoubleSubtract(second1, first0), relativeB2 = VectorDoubleSubtract(second2, first0);
    const auto differenceRoundoff = [](const SIMDVectorDouble point, const SIMDVectorDouble origin, const SIMDVectorDouble difference)noexcept{
        // TwoDiff recovers the subtraction residual without expanding the allowance with a large common translation.
        const SIMDVectorDouble virtualOrigin = VectorDoubleSubtract(point, difference), virtualPoint = VectorDoubleAdd(difference, virtualOrigin);
        return VectorDoubleAdd(VectorDoubleSubtract(point, virtualPoint), VectorDoubleSubtract(virtualOrigin, origin));
    };
    const SIMDVectorDouble roundoffA1 = differenceRoundoff(first1, first0, relativeA1), roundoffA2 = differenceRoundoff(first2, first0, relativeA2);
    const SIMDVectorDouble roundoffB0 = differenceRoundoff(second0, first0, relativeB0), roundoffB1 = differenceRoundoff(second1, first0, relativeB1), roundoffB2 = differenceRoundoff(second2, first0, relativeB2);
    const SIMDVectorDouble edgeA0 = relativeA1, edgeA1 = VectorDoubleSubtract(first2, first1), edgeA2 = VectorDoubleNegate(relativeA2);
    const SIMDVectorDouble edgeB0 = VectorDoubleSubtract(second1, second0), edgeB1 = VectorDoubleSubtract(second2, second1), edgeB2 = VectorDoubleSubtract(second0, second2);
    const auto crossAxis = [](const SIMDVectorDouble first, const SIMDVectorDouble second)noexcept{
        // Separate products make parallel directions produce a zero axis instead of an explicit-FMA residual.
        const SIMDVectorDouble positive = VectorDoubleMultiply(VectorDoubleSwizzle<1, 2, 0, 3>(first), VectorDoubleSwizzle<2, 0, 1, 3>(second));
        const SIMDVectorDouble negative = VectorDoubleMultiply(VectorDoubleSwizzle<2, 0, 1, 3>(first), VectorDoubleSwizzle<1, 2, 0, 3>(second));
        return VectorDoubleSubtract(positive, negative);
    };
    const auto separated = [&](const SIMDVectorDouble axis)noexcept{
        const SIMDVectorDouble absoluteAxis = VectorDoubleAbs(axis);
        const auto projectionInterval = [&](const SIMDVectorDouble relative, const SIMDVectorDouble roundoff)noexcept{
            const SIMDVectorDouble projection = VectorDouble3Dot(relative, axis);
            const SIMDVectorDouble magnitude = VectorDouble3Dot(VectorDoubleAbs(relative), absoluteAxis), coordinateError = VectorDouble3Dot(VectorDoubleAbs(roundoff), absoluteAxis);
            // Three products and two additions need an absolute cancellation bound; eight epsilons also cover rounding the bound and endpoints.
            const SIMDVectorDouble allowance = VectorDoubleMultiplyAddExpression(VectorDoubleAdd(magnitude, coordinateError), VectorDoubleReplicate(0x1p-49), coordinateError);
            return VectorDoubleSelect(VectorDoubleSubtract(projection, allowance), VectorDoubleAdd(projection, allowance), s_DoubleSIMDMaskY);
        };
        const SIMDVectorDouble intervalA1 = projectionInterval(relativeA1, roundoffA1), intervalA2 = projectionInterval(relativeA2, roundoffA2);
        const SIMDVectorDouble minimumA = VectorDoubleMin(zero, VectorDoubleMin(VectorDoubleSplatX(intervalA1), VectorDoubleSplatX(intervalA2)));
        const SIMDVectorDouble maximumA = VectorDoubleMax(zero, VectorDoubleMax(VectorDoubleSplatY(intervalA1), VectorDoubleSplatY(intervalA2)));
        const SIMDVectorDouble intervalB0 = projectionInterval(relativeB0, roundoffB0), intervalB1 = projectionInterval(relativeB1, roundoffB1), intervalB2 = projectionInterval(relativeB2, roundoffB2);
        const SIMDVectorDouble minimumB = VectorDoubleMin(VectorDoubleSplatX(intervalB0), VectorDoubleMin(VectorDoubleSplatX(intervalB1), VectorDoubleSplatX(intervalB2)));
        const SIMDVectorDouble maximumB = VectorDoubleMax(VectorDoubleSplatY(intervalB0), VectorDoubleMax(VectorDoubleSplatY(intervalB1), VectorDoubleSplatY(intervalB2)));
        return VectorDouble4Less(maximumA, minimumB) || VectorDouble4Less(maximumB, minimumA);
    };
    const SIMDVectorDouble normalA = crossAxis(edgeA0, VectorDoubleNegate(edgeA2)), normalB = crossAxis(edgeB0, VectorDoubleNegate(edgeB2));
    if(separated(normalA) || separated(normalB))
        return false;

    if(VectorDouble3Equal(normalA, zero) && VectorDouble3Equal(normalB, zero)){
        // Closed degenerate triangles retain their point/segment hulls, including collinear endpoint contact.
        const SIMDVectorDouble directionA = VectorDouble3Equal(edgeA0, zero) ? edgeA1 : edgeA0, directionB = VectorDouble3Equal(edgeB0, zero) ? edgeB1 : edgeB0;
        const SIMDVectorDouble normal = crossAxis(directionA, directionB);
        if(separated(directionA) || separated(directionB) || separated(normal) || separated(crossAxis(directionA, normal)) || separated(crossAxis(directionB, normal)))
            return false;
        const auto parallelSeparated = [&](const SIMDVectorDouble direction)noexcept{
            return separated(crossAxis(direction, s_DoubleSIMDIdentityR0)) || separated(crossAxis(direction, s_DoubleSIMDIdentityR1)) || separated(crossAxis(direction, s_DoubleSIMDIdentityR2));
        };
        return !parallelSeparated(directionA) && !parallelSeparated(directionB);
    }

    const auto edgeSeparated = [&](const SIMDVectorDouble edge)noexcept{
        // In-plane edge normals are necessary when the triangles are coplanar or one hull is degenerate.
        return separated(crossAxis(edge, normalA)) || separated(crossAxis(edge, normalB)) || separated(crossAxis(edge, edgeB0)) || separated(crossAxis(edge, edgeB1)) || separated(crossAxis(edge, edgeB2));
    };
    if(edgeSeparated(edgeA0) || edgeSeparated(edgeA1) || edgeSeparated(edgeA2))
        return false;
    const auto secondEdgeSeparated = [&](const SIMDVectorDouble edge)noexcept{
        return separated(crossAxis(edge, normalA)) || separated(crossAxis(edge, normalB));
    };
    return !secondEdgeSeparated(edgeB0) && !secondEdgeSeparated(edgeB1) && !secondEdgeSeparated(edgeB2);
}

NWB_NOINLINE bool NWB_SIMD_CALL ZeroAxis(const SIMDVectorDouble a0, const SIMDVectorDouble a1, const SIMDVectorDouble a2, const SIMDVectorDouble b0, const SIMDVectorDouble b1, const SIMDVectorDouble b2)noexcept{
    const SIMDVectorDouble first0 = VectorDoubleAndInt(a0, s_DoubleSIMDMask3), first1 = VectorDoubleAndInt(a1, s_DoubleSIMDMask3), first2 = VectorDoubleAndInt(a2, s_DoubleSIMDMask3);
    const SIMDVectorDouble second0 = VectorDoubleAndInt(b0, s_DoubleSIMDMask3), second1 = VectorDoubleAndInt(b1, s_DoubleSIMDMask3), second2 = VectorDoubleAndInt(b2, s_DoubleSIMDMask3);
    const SIMDVectorDouble minA = VectorDoubleMin(first0, VectorDoubleMin(first1, first2)), maxA = VectorDoubleMax(first0, VectorDoubleMax(first1, first2));
    const SIMDVectorDouble minB = VectorDoubleMin(second0, VectorDoubleMin(second1, second2)), maxB = VectorDoubleMax(second0, VectorDoubleMax(second1, second2));
    if(!CollisionDoubleDetail::MinMaxIntersects(minA, maxA, minB, maxB))
        return false;

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble relativeA1 = VectorDoubleSubtract(first1, first0), relativeA2 = VectorDoubleSubtract(first2, first0);
    const SIMDVectorDouble relativeB0 = VectorDoubleSubtract(second0, first0), relativeB1 = VectorDoubleSubtract(second1, first0), relativeB2 = VectorDoubleSubtract(second2, first0);
    const auto differenceRoundoff = [](const SIMDVectorDouble point, const SIMDVectorDouble origin, const SIMDVectorDouble difference)noexcept{
        // TwoDiff recovers the subtraction residual without expanding the allowance with a large common translation.
        const SIMDVectorDouble virtualOrigin = VectorDoubleSubtract(point, difference), virtualPoint = VectorDoubleAdd(difference, virtualOrigin);
        return VectorDoubleAdd(VectorDoubleSubtract(point, virtualPoint), VectorDoubleSubtract(virtualOrigin, origin));
    };
    const SIMDVectorDouble roundoffA1 = differenceRoundoff(first1, first0, relativeA1), roundoffA2 = differenceRoundoff(first2, first0, relativeA2);
    const SIMDVectorDouble roundoffB0 = differenceRoundoff(second0, first0, relativeB0), roundoffB1 = differenceRoundoff(second1, first0, relativeB1), roundoffB2 = differenceRoundoff(second2, first0, relativeB2);
    const SIMDVectorDouble edgeA0 = relativeA1, edgeA1 = VectorDoubleSubtract(first2, first1), edgeA2 = VectorDoubleNegate(relativeA2);
    const SIMDVectorDouble edgeB0 = VectorDoubleSubtract(second1, second0), edgeB1 = VectorDoubleSubtract(second2, second1), edgeB2 = VectorDoubleSubtract(second0, second2);
    const auto crossAxis = [](const SIMDVectorDouble first, const SIMDVectorDouble second)noexcept{
        // Separate products make parallel directions produce a zero axis instead of an explicit-FMA residual.
        const SIMDVectorDouble positive = VectorDoubleMultiply(VectorDoubleSwizzle<1, 2, 0, 3>(first), VectorDoubleSwizzle<2, 0, 1, 3>(second));
        const SIMDVectorDouble negative = VectorDoubleMultiply(VectorDoubleSwizzle<2, 0, 1, 3>(first), VectorDoubleSwizzle<1, 2, 0, 3>(second));
        return VectorDoubleSubtract(positive, negative);
    };
    const auto separated = [&](const SIMDVectorDouble axis)noexcept{
        if(VectorDouble3Equal(axis, zero))
            return false;
        const SIMDVectorDouble absoluteAxis = VectorDoubleAbs(axis);
        const auto projectionInterval = [&](const SIMDVectorDouble relative, const SIMDVectorDouble roundoff)noexcept{
            const SIMDVectorDouble projection = VectorDouble3Dot(relative, axis);
            const SIMDVectorDouble magnitude = VectorDouble3Dot(VectorDoubleAbs(relative), absoluteAxis), coordinateError = VectorDouble3Dot(VectorDoubleAbs(roundoff), absoluteAxis);
            // Three products and two additions need an absolute cancellation bound; eight epsilons also cover rounding the bound and endpoints.
            const SIMDVectorDouble allowance = VectorDoubleMultiplyAddExpression(VectorDoubleAdd(magnitude, coordinateError), VectorDoubleReplicate(0x1p-49), coordinateError);
            return VectorDoubleSelect(VectorDoubleSubtract(projection, allowance), VectorDoubleAdd(projection, allowance), s_DoubleSIMDMaskY);
        };
        const SIMDVectorDouble intervalA1 = projectionInterval(relativeA1, roundoffA1), intervalA2 = projectionInterval(relativeA2, roundoffA2);
        const SIMDVectorDouble minimumA = VectorDoubleMin(zero, VectorDoubleMin(VectorDoubleSplatX(intervalA1), VectorDoubleSplatX(intervalA2)));
        const SIMDVectorDouble maximumA = VectorDoubleMax(zero, VectorDoubleMax(VectorDoubleSplatY(intervalA1), VectorDoubleSplatY(intervalA2)));
        const SIMDVectorDouble intervalB0 = projectionInterval(relativeB0, roundoffB0), intervalB1 = projectionInterval(relativeB1, roundoffB1), intervalB2 = projectionInterval(relativeB2, roundoffB2);
        const SIMDVectorDouble minimumB = VectorDoubleMin(VectorDoubleSplatX(intervalB0), VectorDoubleMin(VectorDoubleSplatX(intervalB1), VectorDoubleSplatX(intervalB2)));
        const SIMDVectorDouble maximumB = VectorDoubleMax(VectorDoubleSplatY(intervalB0), VectorDoubleMax(VectorDoubleSplatY(intervalB1), VectorDoubleSplatY(intervalB2)));
        return VectorDouble4Less(maximumA, minimumB) || VectorDouble4Less(maximumB, minimumA);
    };
    const SIMDVectorDouble normalA = crossAxis(edgeA0, VectorDoubleNegate(edgeA2)), normalB = crossAxis(edgeB0, VectorDoubleNegate(edgeB2));
    if(separated(normalA) || separated(normalB))
        return false;

    if(VectorDouble3Equal(normalA, zero) && VectorDouble3Equal(normalB, zero)){
        // Closed degenerate triangles retain their point/segment hulls, including collinear endpoint contact.
        const SIMDVectorDouble directionA = VectorDouble3Equal(edgeA0, zero) ? edgeA1 : edgeA0, directionB = VectorDouble3Equal(edgeB0, zero) ? edgeB1 : edgeB0;
        const SIMDVectorDouble normal = crossAxis(directionA, directionB);
        if(separated(directionA) || separated(directionB) || separated(normal) || separated(crossAxis(directionA, normal)) || separated(crossAxis(directionB, normal)))
            return false;
        const auto parallelSeparated = [&](const SIMDVectorDouble direction)noexcept{
            return separated(crossAxis(direction, s_DoubleSIMDIdentityR0)) || separated(crossAxis(direction, s_DoubleSIMDIdentityR1)) || separated(crossAxis(direction, s_DoubleSIMDIdentityR2));
        };
        return !parallelSeparated(directionA) && !parallelSeparated(directionB);
    }

    const auto edgeSeparated = [&](const SIMDVectorDouble edge)noexcept{
        // In-plane edge normals are necessary when the triangles are coplanar or one hull is degenerate.
        return separated(crossAxis(edge, normalA)) || separated(crossAxis(edge, normalB)) || separated(crossAxis(edge, edgeB0)) || separated(crossAxis(edge, edgeB1)) || separated(crossAxis(edge, edgeB2));
    };
    if(edgeSeparated(edgeA0) || edgeSeparated(edgeA1) || edgeSeparated(edgeA2))
        return false;
    const auto secondEdgeSeparated = [&](const SIMDVectorDouble edge)noexcept{
        return separated(crossAxis(edge, normalA)) || separated(crossAxis(edge, normalB));
    };
    return !secondEdgeSeparated(edgeB0) && !secondEdgeSeparated(edgeB1) && !secondEdgeSeparated(edgeB2);
}

NWB_NOINLINE bool NWB_SIMD_CALL AbsHoisted(const SIMDVectorDouble a0, const SIMDVectorDouble a1, const SIMDVectorDouble a2, const SIMDVectorDouble b0, const SIMDVectorDouble b1, const SIMDVectorDouble b2)noexcept{
    const SIMDVectorDouble first0 = VectorDoubleAndInt(a0, s_DoubleSIMDMask3), first1 = VectorDoubleAndInt(a1, s_DoubleSIMDMask3), first2 = VectorDoubleAndInt(a2, s_DoubleSIMDMask3);
    const SIMDVectorDouble second0 = VectorDoubleAndInt(b0, s_DoubleSIMDMask3), second1 = VectorDoubleAndInt(b1, s_DoubleSIMDMask3), second2 = VectorDoubleAndInt(b2, s_DoubleSIMDMask3);
    const SIMDVectorDouble minA = VectorDoubleMin(first0, VectorDoubleMin(first1, first2)), maxA = VectorDoubleMax(first0, VectorDoubleMax(first1, first2));
    const SIMDVectorDouble minB = VectorDoubleMin(second0, VectorDoubleMin(second1, second2)), maxB = VectorDoubleMax(second0, VectorDoubleMax(second1, second2));
    if(!CollisionDoubleDetail::MinMaxIntersects(minA, maxA, minB, maxB))
        return false;

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble relativeA1 = VectorDoubleSubtract(first1, first0), relativeA2 = VectorDoubleSubtract(first2, first0);
    const SIMDVectorDouble relativeB0 = VectorDoubleSubtract(second0, first0), relativeB1 = VectorDoubleSubtract(second1, first0), relativeB2 = VectorDoubleSubtract(second2, first0);
    const SIMDVectorDouble absoluteA1 = VectorDoubleAbs(relativeA1), absoluteA2 = VectorDoubleAbs(relativeA2);
    const SIMDVectorDouble absoluteB0 = VectorDoubleAbs(relativeB0), absoluteB1 = VectorDoubleAbs(relativeB1), absoluteB2 = VectorDoubleAbs(relativeB2);
    const auto differenceRoundoff = [](const SIMDVectorDouble point, const SIMDVectorDouble origin, const SIMDVectorDouble difference)noexcept{
        // TwoDiff recovers the subtraction residual without expanding the allowance with a large common translation.
        const SIMDVectorDouble virtualOrigin = VectorDoubleSubtract(point, difference), virtualPoint = VectorDoubleAdd(difference, virtualOrigin);
        return VectorDoubleAdd(VectorDoubleSubtract(point, virtualPoint), VectorDoubleSubtract(virtualOrigin, origin));
    };
    const SIMDVectorDouble roundoffA1 = VectorDoubleAbs(differenceRoundoff(first1, first0, relativeA1)), roundoffA2 = VectorDoubleAbs(differenceRoundoff(first2, first0, relativeA2));
    const SIMDVectorDouble roundoffB0 = VectorDoubleAbs(differenceRoundoff(second0, first0, relativeB0));
    const SIMDVectorDouble roundoffB1 = VectorDoubleAbs(differenceRoundoff(second1, first0, relativeB1)), roundoffB2 = VectorDoubleAbs(differenceRoundoff(second2, first0, relativeB2));
    const SIMDVectorDouble edgeA0 = relativeA1, edgeA1 = VectorDoubleSubtract(first2, first1), edgeA2 = VectorDoubleNegate(relativeA2);
    const SIMDVectorDouble edgeB0 = VectorDoubleSubtract(second1, second0), edgeB1 = VectorDoubleSubtract(second2, second1), edgeB2 = VectorDoubleSubtract(second0, second2);
    const auto crossAxis = [](const SIMDVectorDouble first, const SIMDVectorDouble second)noexcept{
        // Separate products make parallel directions produce a zero axis instead of an explicit-FMA residual.
        const SIMDVectorDouble positive = VectorDoubleMultiply(VectorDoubleSwizzle<1, 2, 0, 3>(first), VectorDoubleSwizzle<2, 0, 1, 3>(second));
        const SIMDVectorDouble negative = VectorDoubleMultiply(VectorDoubleSwizzle<2, 0, 1, 3>(first), VectorDoubleSwizzle<1, 2, 0, 3>(second));
        return VectorDoubleSubtract(positive, negative);
    };
    const auto separated = [&](const SIMDVectorDouble axis)noexcept{
        if(VectorDouble3Equal(axis, zero))
            return false;
        const SIMDVectorDouble absoluteAxis = VectorDoubleAbs(axis);
        const auto projectionInterval = [&](const SIMDVectorDouble relative, const SIMDVectorDouble absoluteRelative, const SIMDVectorDouble absoluteRoundoff)noexcept{
            const SIMDVectorDouble projection = VectorDouble3Dot(relative, axis);
            const SIMDVectorDouble magnitude = VectorDouble3Dot(absoluteRelative, absoluteAxis), coordinateError = VectorDouble3Dot(absoluteRoundoff, absoluteAxis);
            // Three products and two additions need an absolute cancellation bound; eight epsilons also cover rounding the bound and endpoints.
            const SIMDVectorDouble allowance = VectorDoubleMultiplyAddExpression(VectorDoubleAdd(magnitude, coordinateError), VectorDoubleReplicate(0x1p-49), coordinateError);
            return VectorDoubleSelect(VectorDoubleSubtract(projection, allowance), VectorDoubleAdd(projection, allowance), s_DoubleSIMDMaskY);
        };
        const SIMDVectorDouble intervalA1 = projectionInterval(relativeA1, absoluteA1, roundoffA1), intervalA2 = projectionInterval(relativeA2, absoluteA2, roundoffA2);
        const SIMDVectorDouble minimumA = VectorDoubleMin(zero, VectorDoubleMin(VectorDoubleSplatX(intervalA1), VectorDoubleSplatX(intervalA2)));
        const SIMDVectorDouble maximumA = VectorDoubleMax(zero, VectorDoubleMax(VectorDoubleSplatY(intervalA1), VectorDoubleSplatY(intervalA2)));
        const SIMDVectorDouble intervalB0 = projectionInterval(relativeB0, absoluteB0, roundoffB0), intervalB1 = projectionInterval(relativeB1, absoluteB1, roundoffB1), intervalB2 = projectionInterval(relativeB2, absoluteB2, roundoffB2);
        const SIMDVectorDouble minimumB = VectorDoubleMin(VectorDoubleSplatX(intervalB0), VectorDoubleMin(VectorDoubleSplatX(intervalB1), VectorDoubleSplatX(intervalB2)));
        const SIMDVectorDouble maximumB = VectorDoubleMax(VectorDoubleSplatY(intervalB0), VectorDoubleMax(VectorDoubleSplatY(intervalB1), VectorDoubleSplatY(intervalB2)));
        return VectorDouble4Less(maximumA, minimumB) || VectorDouble4Less(maximumB, minimumA);
    };
    const SIMDVectorDouble normalA = crossAxis(edgeA0, VectorDoubleNegate(edgeA2)), normalB = crossAxis(edgeB0, VectorDoubleNegate(edgeB2));
    if(separated(normalA) || separated(normalB))
        return false;

    if(VectorDouble3Equal(normalA, zero) && VectorDouble3Equal(normalB, zero)){
        // Closed degenerate triangles retain their point/segment hulls, including collinear endpoint contact.
        const SIMDVectorDouble directionA = VectorDouble3Equal(edgeA0, zero) ? edgeA1 : edgeA0, directionB = VectorDouble3Equal(edgeB0, zero) ? edgeB1 : edgeB0;
        const SIMDVectorDouble normal = crossAxis(directionA, directionB);
        if(separated(directionA) || separated(directionB) || separated(normal) || separated(crossAxis(directionA, normal)) || separated(crossAxis(directionB, normal)))
            return false;
        const auto parallelSeparated = [&](const SIMDVectorDouble direction)noexcept{
            return separated(crossAxis(direction, s_DoubleSIMDIdentityR0)) || separated(crossAxis(direction, s_DoubleSIMDIdentityR1)) || separated(crossAxis(direction, s_DoubleSIMDIdentityR2));
        };
        return !parallelSeparated(directionA) && !parallelSeparated(directionB);
    }

    const auto edgeSeparated = [&](const SIMDVectorDouble edge)noexcept{
        // In-plane edge normals are necessary when the triangles are coplanar or one hull is degenerate.
        return separated(crossAxis(edge, normalA)) || separated(crossAxis(edge, normalB)) || separated(crossAxis(edge, edgeB0)) || separated(crossAxis(edge, edgeB1)) || separated(crossAxis(edge, edgeB2));
    };
    if(edgeSeparated(edgeA0) || edgeSeparated(edgeA1) || edgeSeparated(edgeA2))
        return false;
    const auto secondEdgeSeparated = [&](const SIMDVectorDouble edge)noexcept{
        return separated(crossAxis(edge, normalA)) || separated(crossAxis(edge, normalB));
    };
    return !secondEdgeSeparated(edgeB0) && !secondEdgeSeparated(edgeB1) && !secondEdgeSeparated(edgeB2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_NOINLINE u64 Run(const NotNull<Intersection> implementation, const TrianglePair (&pairs)[s_FixtureCount], const u32 passes)noexcept{
    u64 result = 0u;
    for(u32 pass = 0u; pass < passes; ++pass){
        for(const TrianglePair& pair : pairs)
            result += implementation.get()(LoadDouble(pair.first[0]), LoadDouble(pair.first[1]), LoadDouble(pair.first[2]), LoadDouble(pair.second[0]), LoadDouble(pair.second[1]), LoadDouble(pair.second[2])) ? 1u : 0u;
    }
    s_Checksum = result;
    return result;
}

[[nodiscard]] static SIMDVectorDouble LoadPosition(const f64 x, const f64 y, const f64 z)noexcept{
    return VectorDoubleSet(x, y, z, 0.0);
}

[[nodiscard]] static TrianglePairVectors LoadTrianglePair(const TrianglePair& pair)noexcept{
    return { { LoadDouble(pair.first[0]), LoadDouble(pair.first[1]), LoadDouble(pair.first[2]) }, { LoadDouble(pair.second[0]), LoadDouble(pair.second[1]), LoadDouble(pair.second[2]) } };
}

static void StoreTrianglePair(const TrianglePairVectors& vectors, TrianglePair& pair)noexcept{
    for(u32 vertex = 0u; vertex < 3u; ++vertex){
        StoreDouble(vectors.first[vertex], pair.first[vertex]);
        StoreDouble(vectors.second[vertex], pair.second[vertex]);
    }
}

static void Fill(const u32 kind, TrianglePair (&pairs)[s_FixtureCount])noexcept{
    for(u32 index = 0u; index < s_FixtureCount; ++index){
        // index * -0.25 has the same bits as -(index * 0.25).
        const SIMDVectorDouble translation = VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(index)), VectorDoubleSet(0.25, -0.25, 0.25, 0.0));
        TrianglePairVectors pair;
        if(kind == 0u){
            pair = { { LoadPosition(0.0, 0.0, 0.0), LoadPosition(4.0, 0.0, 0.0), LoadPosition(0.0, 4.0, 0.0) }, { LoadPosition(1.0, 1.0, 0.0), LoadPosition(3.0, 1.0, 0.0), LoadPosition(1.0, 3.0, 0.0) } };
        }
        else if(kind == 1u){
            pair = { { LoadPosition(0.0, 0.0, 0.0), LoadPosition(4.0, 0.0, 0.0), LoadPosition(0.0, 4.0, 0.0) }, { LoadPosition(1.0, 1.0, 0.0), LoadPosition(1.0, 1.0, 0.0), LoadPosition(1.0, 1.0, 0.0) } };
        }
        else if(kind == 2u){
            pair = { { LoadPosition(0.0, 0.0, 0.0), LoadPosition(1.0, 1.0, 0.0), LoadPosition(2.0, 2.0, 0.0) }, { LoadPosition(1.0, 1.0, 0.0), LoadPosition(2.0, 2.0, 0.0), LoadPosition(3.0, 3.0, 0.0) } };
        }
        else if(kind == 3u){
            pair = { { LoadPosition(0.0, 0.0, 0.0), LoadPosition(3.0, 1.0, 2.0), LoadPosition(1.0, 4.0, -1.0) }, { LoadPosition(1.0, 1.25, 0.25), LoadPosition(1.5, 2.5, 1.0), LoadPosition(0.25, 1.75, 1.75) } };
        }
        else if(kind == 4u){
            pair = { { LoadPosition(0.0, 0.0, 0.0), LoadPosition(2.0, 0.0, 0.0), LoadPosition(0.0, 2.0, 0.0) }, { LoadPosition(1.5, 1.5, -1.0), LoadPosition(1.5, 2.0, 1.0), LoadPosition(2.0, 1.5, 1.0) } };
        }
        else if(kind == 5u){
            pair = { { LoadPosition(0.0, 0.0, 0.0), LoadPosition(2.0, 0.0, 0.0), LoadPosition(0.0, 2.0, 0.0) }, { LoadPosition(100.0, 100.0, 100.0), LoadPosition(102.0, 100.0, 100.0), LoadPosition(100.0, 102.0, 100.0) } };
        }
        else{
            pair = { { LoadPosition(1.0, 1.0, 1.0), LoadPosition(1.0, 1.0, 1.0), LoadPosition(1.0, 1.0, 1.0) }, { LoadPosition(1.0, 1.0, 1.0), LoadPosition(1.0, 1.0, 1.0), LoadPosition(1.0, 1.0, 1.0) } };
        }
        for(u32 vertex = 0u; vertex < 3u; ++vertex){
            pair.first[vertex] = VectorDoubleAdd(pair.first[vertex], translation);
            pair.second[vertex] = VectorDoubleAdd(pair.second[vertex], translation);
        }
        StoreTrianglePair(pair, pairs[index]);
    }
}

[[nodiscard]] static bool CheckPair(const TrianglePairVectors& pair, const bool expected)noexcept{
    constexpr u32 s_Permutations[][3] = { { 0u, 1u, 2u }, { 0u, 2u, 1u }, { 1u, 0u, 2u }, { 1u, 2u, 0u }, { 2u, 0u, 1u }, { 2u, 1u, 0u } };
    for(const auto& firstOrder : s_Permutations){
        for(const auto& secondOrder : s_Permutations){
            for(const bool reverse : { false, true }){
                const auto& first = reverse ? pair.second : pair.first;
                const auto& second = reverse ? pair.first : pair.second;
                const auto& firstIndices = reverse ? secondOrder : firstOrder;
                const auto& secondIndices = reverse ? firstOrder : secondOrder;
                const bool baseline = Baseline(first[firstIndices[0]], first[firstIndices[1]], first[firstIndices[2]], second[secondIndices[0]], second[secondIndices[1]], second[secondIndices[2]]);
                const bool candidate = ZeroAxis(first[firstIndices[0]], first[firstIndices[1]], first[firstIndices[2]], second[secondIndices[0]], second[secondIndices[1]], second[secondIndices[2]]);
                const bool production = TriangleDoubleTests::Intersects(first[firstIndices[0]], first[firstIndices[1]], first[firstIndices[2]], second[secondIndices[0]], second[secondIndices[1]], second[secondIndices[2]]);
                const bool hoisted = AbsHoisted(first[firstIndices[0]], first[firstIndices[1]], first[firstIndices[2]], second[secondIndices[0]], second[secondIndices[1]], second[secondIndices[2]]);
                if(baseline != expected || candidate != baseline || production != baseline || hoisted != baseline)
                    return false;
            }
        }
    }
    return true;
}

[[nodiscard]] static u64 Next(u64& state)noexcept{
    state ^= state << 13u;
    state ^= state >> 7u;
    state ^= state << 17u;
    return state;
}

[[nodiscard]] static bool CheckExceptionEquivalence(const TrianglePairVectors& pair)noexcept{
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    if(!originalEnvironment)
        return false;
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            __builtin_trap();
    }
    );
    const bool original = Baseline(pair.first[0], pair.first[1], pair.first[2], pair.second[0], pair.second[1], pair.second[2]);
    const i32 originalFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT))
        return false;
    const bool reference = ZeroAxis(pair.first[0], pair.first[1], pair.first[2], pair.second[0], pair.second[1], pair.second[2]);
    const i32 referenceFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT))
        return false;
    const bool hoisted = AbsHoisted(pair.first[0], pair.first[1], pair.first[2], pair.second[0], pair.second[1], pair.second[2]);
    const i32 hoistedFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT))
        return false;
    const bool production = TriangleDoubleTests::Intersects(pair.first[0], pair.first[1], pair.first[2], pair.second[0], pair.second[1], pair.second[2]);
    const i32 productionFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    return original == reference && reference == hoisted && hoisted == production && originalFlags == referenceFlags && referenceFlags == hoistedFlags && hoistedFlags == productionFlags;
}

#if defined(NWB_PLATFORM_LINUX)
static void TrapHandler(i32, siginfo_t* information, void*)noexcept{
    _exit(32 + information->si_code);
}

[[nodiscard]] static Expected<i32, ErrorCode> TrapResult(const NotNull<Intersection> implementation, const TrianglePairVectors& pair, const i32 mask)noexcept{
    const pid_t child = fork();
    if(child < 0)
        return MakeUnexpected(MakeErrorCode(static_cast<ErrorCondition>(errno)));
    if(child == 0){
        struct sigaction action{};
        action.sa_sigaction = TrapHandler;
        action.sa_flags = SA_SIGINFO;
        if(sigemptyset(&action.sa_mask) != 0 || sigaction(SIGFPE, &action, nullptr) != 0)
            _exit(127);
        const auto environment = HoldFloatingPointExceptions();
        if(!environment || feenableexcept(mask) < 0)
            _exit(127);
        const bool result = implementation.get()(pair.first[0], pair.first[1], pair.first[2], pair.second[0], pair.second[1], pair.second[2]);
        _exit(result ? 1 : 0);
    }
    i32 status = 0;
    if(waitpid(child, &status, 0) != child)
        return MakeUnexpected(MakeErrorCode(static_cast<ErrorCondition>(errno)));
    if(!WIFEXITED(status))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::state_not_recoverable));
    return WEXITSTATUS(status);
}
#endif

[[nodiscard]] static bool CheckTrapEquivalence(const TrianglePairVectors& pair)noexcept{
#if defined(NWB_PLATFORM_LINUX)
    const auto production = MakeNotNull<Intersection>(TriangleDoubleTests::Intersects);
    for(const i32 mask : { FE_INVALID, FE_OVERFLOW, FE_INVALID | FE_OVERFLOW, FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT }){
        const auto original = TrapResult(MakeNotNull<Intersection>(Baseline), pair, mask);
        if(!original || *original == 127)
            return false;
        for(const auto implementation : { MakeNotNull<Intersection>(ZeroAxis), MakeNotNull<Intersection>(AbsHoisted), production }){
            const auto observed = TrapResult(implementation, pair, mask);
            if(!observed || *observed != *original)
                return false;
        }
    }
#else
    if(!CheckExceptionEquivalence(pair))
        return false;
#endif
    return true;
}

[[nodiscard]] static bool Validate()noexcept{
#pragma STDC FENV_ACCESS ON
    TrianglePair pairs[s_FixtureCount];
    for(u32 kind = 0u; kind < 7u; ++kind){
        Fill(kind, pairs);
        if(!CheckPair(LoadTrianglePair(pairs[0]), kind != 4u && kind != 5u))
            return false;
    }
    u64 state = 0x4f1bbcdc6762c13fu;
    for(u32 sample = 0u; sample < 200000u; ++sample){
        TrianglePairVectors pair{};
        for(u32 vertex = 0u; vertex < 3u; ++vertex){
            f64 coordinates[3];
            for(f64& coordinate : coordinates)
                coordinate = static_cast<f64>(static_cast<i64>(Next(state) % 2000000001u) - 1000000000);
            pair.first[vertex] = VectorDoubleScale(LoadPosition(coordinates[0], coordinates[1], coordinates[2]), 0.25);
        }
        const SIMDVectorDouble contained = VectorDoubleScale(VectorDoubleAdd(VectorDoubleAdd(VectorDoubleScale(pair.first[0], 2.0), pair.first[1]), pair.first[2]), 0.25);
        for(SIMDVectorDouble& vertex : pair.second)
            vertex = contained;
        if(
            !Baseline(pair.first[0], pair.first[1], pair.first[2], contained, contained, contained)
            || !ZeroAxis(pair.first[0], pair.first[1], pair.first[2], contained, contained, contained)
            || !TriangleDoubleTests::Intersects(pair.first[0], pair.first[1], pair.first[2], contained, contained, contained)
            || !AbsHoisted(pair.first[0], pair.first[1], pair.first[2], contained, contained, contained)
        )
            return false;
        for(u32 vertex = 0u; vertex < 3u; ++vertex){
            f64 coordinates[3];
            for(f64& coordinate : coordinates)
                coordinate = static_cast<f64>(static_cast<i64>(Next(state) % 2000001u) - 1000000);
            pair.first[vertex] = LoadPosition(coordinates[0], coordinates[1], coordinates[2]);
        }
        const SIMDVectorDouble containedSingle = VectorDoubleScale(VectorDoubleAdd(VectorDoubleAdd(VectorDoubleScale(pair.first[0], 2.0), pair.first[1]), pair.first[2]), 0.25);
        for(u32 vertex = 0u; vertex < 3u; ++vertex){
            pair.first[vertex] = ConvertVectorSingleToDouble(ConvertVectorDoubleToSingle(pair.first[vertex]));
            pair.second[vertex] = ConvertVectorSingleToDouble(ConvertVectorDoubleToSingle(containedSingle));
        }
        if(
            !Baseline(pair.first[0], pair.first[1], pair.first[2], pair.second[0], pair.second[1], pair.second[2])
            || !ZeroAxis(pair.first[0], pair.first[1], pair.first[2], pair.second[0], pair.second[1], pair.second[2])
            || !TriangleDoubleTests::Intersects(pair.first[0], pair.first[1], pair.first[2], pair.second[0], pair.second[1], pair.second[2])
            || !AbsHoisted(pair.first[0], pair.first[1], pair.first[2], pair.second[0], pair.second[1], pair.second[2])
        )
            return false;
    }
    Fill(0u, pairs);
    TrianglePairVectors signaling = LoadTrianglePair(pairs[0]);
    for(SIMDVectorDouble& vertex : signaling.first)
        vertex = VectorDoubleSetIntW(vertex, 0x7ff0000000000001u);
    for(SIMDVectorDouble& vertex : signaling.second)
        vertex = VectorDoubleSetIntW(vertex, 0x7ff0000000000001u);
    const auto environment = HoldFloatingPointExceptions();
    if(!environment)
        return false;
    const bool baseline = Baseline(signaling.first[0], signaling.first[1], signaling.first[2], signaling.second[0], signaling.second[1], signaling.second[2]);
    const i32 baselineExceptions = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT))
        return false;
    const bool candidate = ZeroAxis(signaling.first[0], signaling.first[1], signaling.first[2], signaling.second[0], signaling.second[1], signaling.second[2]);
    const i32 candidateExceptions = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT))
        return false;
    const bool production = TriangleDoubleTests::Intersects(signaling.first[0], signaling.first[1], signaling.first[2], signaling.second[0], signaling.second[1], signaling.second[2]);
    const i32 productionExceptions = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT))
        return false;
    const bool hoisted = AbsHoisted(signaling.first[0], signaling.first[1], signaling.first[2], signaling.second[0], signaling.second[1], signaling.second[2]);
    const i32 hoistedExceptions = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    if(!SetFloatingPointEnvironment(*environment))
        return false;
    if(!(baseline && candidate && production && hoisted && baselineExceptions == candidateExceptions && candidateExceptions == productionExceptions && productionExceptions == hoistedExceptions && (candidateExceptions & FE_INVALID) == 0))
        return false;

    volatile u64 activeBits[] = { 0x7ff0000000000000u, 0x7ff8000012345678u, 0x7ff0000000000001u };
    const f64 scales[] = { 0.0, 0x1p-300, 0x1p-100, 0x1p+100, 0x1p+300, 0x1p+900 };
    for(const f64 scale : scales){
        Fill(3u, pairs);
        TrianglePairVectors scaled = LoadTrianglePair(pairs[0]);
        for(u32 vertex = 0u; vertex < 3u; ++vertex){
            scaled.first[vertex] = VectorDoubleMultiply(scaled.first[vertex], VectorDoubleReplicate(scale));
            scaled.second[vertex] = VectorDoubleMultiply(scaled.second[vertex], VectorDoubleReplicate(scale));
        }
        if(!CheckExceptionEquivalence(scaled))
            return false;
    }
    for(u32 index = 0u; index < LengthOf(activeBits); ++index){
        Fill(3u, pairs);
        TrianglePairVectors active = LoadTrianglePair(pairs[0]);
        active.first[0] = VectorDoubleSetIntX(active.first[0], activeBits[index]);
        if(!CheckExceptionEquivalence(active))
            return false;
    }
    constexpr f64 maximum = Limit<f64>::s_Max;
    const TrianglePair extreme[] = {
        { { Double4(0.0, 1.0, 1.0), Double4(0.0, 1.0, 1.0), Double4(0.0, 1.0, 1.0) }, { Double4(Limit<f64>::s_Infinity, 1.0, 2.0), Double4(-1.0, -1.0, -1.0), Double4(1.0, 3.0, 4.0) } },
        { { Double4(maximum, 0.0, 0.0), Double4(maximum, 0.0, 0.0), Double4(maximum, 0.0, 0.0) }, { Double4(-maximum, -1.0, -1.0), Double4(0.0, 1.0, 1.0), Double4(maximum, 0.0, 0.0) } }
    };
    for(const TrianglePair& pair : extreme){
        const TrianglePairVectors vectors = LoadTrianglePair(pair);
        if(!CheckExceptionEquivalence(vectors) || !CheckTrapEquivalence(vectors))
            return false;
    }
    return true;
}

static void PrintSummary(const StringView workload, const f64 (&values)[s_Rounds]){
    f64 sorted[s_Rounds];
    for(u32 index = 0u; index < s_Rounds; ++index)
        sorted[index] = values[index];
    for(u32 index = 1u; index < s_Rounds; ++index){
        const f64 value = sorted[index];
        u32 destination = index;
        while(destination != 0u && sorted[destination - 1u] > value){
            sorted[destination] = sorted[destination - 1u];
            --destination;
        }
        sorted[destination] = value;
    }
    NWB_COUT << "summary," << workload << ',' << sorted[s_Rounds / 2u] << ',' << sorted[0] << ',' << sorted[s_Rounds - 1u] << '\n';
}

static i32 Benchmark(const bool validationOnly, const bool measureAbs, const bool guardOnly){
    if(!Validate()){
        NWB_CERR << "validation failed\n";
        return 1;
    }
    NWB_COUT << "validation,400000 exact containment probes, winding, finite/nonfinite FENV and trap-order equivalence,passed\n";
    if(validationOnly)
        return 0;
    static constexpr StringView s_Names[] = { "coplanar", "point_hull", "segment_hull", "noncoplanar", "noncoplanar_miss", "aabb_miss", "point_point" };
    const auto reference = MakeNotNull<Intersection>(measureAbs ? ZeroAxis : (guardOnly ? CurrentDot3Baseline : Baseline));
    const auto production = MakeNotNull<Intersection>(TriangleDoubleTests::Intersects);
    const auto selected = measureAbs ? MakeNotNull<Intersection>(AbsHoisted) : (guardOnly ? MakeNotNull<Intersection>(ZeroAxis) : production);
    const StringView referenceName = measureAbs ? s_ZeroAxisLabel : (guardOnly ? "current_dot3_baseline" : "baseline");
    const StringView selectedName = measureAbs ? "abs_hoisted" : (guardOnly ? s_ZeroAxisLabel : "production");
    NWB_COUT << "timer,TimerNow/DurationInNS\n";
    NWB_COUT << "mode," << (measureAbs ? "abs_hoisted_vs_zero_axis" : (guardOnly ? "zero_axis_vs_current_dot3" : "production_vs_original_baseline")) << '\n';
    TrianglePair pairs[s_FixtureCount];
    for(u32 kind = 0u; kind < 7u; ++kind){
        Fill(kind, pairs);
        const u64 expected = Run(reference, pairs, 1u) * s_Passes;
        if(Run(selected, pairs, 1u) * s_Passes != expected)
            return 2;
        f64 baselineValues[s_Rounds], candidateValues[s_Rounds], ratios[s_Rounds];
        for(u32 round = 0u; round < s_Rounds; ++round){
            for(u32 step = 0u; step < 2u; ++step){
                const bool candidate = ((round + step) & 1u) != 0u;
                const auto implementation = candidate ? selected : reference;
                const Timer begin = TimerNow();
                const u64 checksum = Run(implementation, pairs, s_Passes);
                const Timer end = TimerNow();
                if(checksum != expected)
                    return 3;
                const f64 nanoseconds = VectorDoubleGetX(VectorDoubleDivide(VectorDoubleReplicate(DurationInNS<f64>(end, begin)), VectorDoubleReplicate(static_cast<f64>(s_Passes * s_FixtureCount))));
                (candidate ? candidateValues : baselineValues)[round] = nanoseconds;
                NWB_COUT << "sample," << s_Names[kind] << ',' << (candidate ? selectedName : referenceName) << ',' << round << ',' << nanoseconds << ',' << checksum << '\n';
            }
            ratios[round] = VectorDoubleGetX(VectorDoubleDivide(VectorDoubleReplicate(candidateValues[round]), VectorDoubleReplicate(baselineValues[round])));
        }
        NWB_COUT << referenceName << '_';
        PrintSummary(s_Names[kind], baselineValues);
        NWB_COUT << selectedName << '_';
        PrintSummary(s_Names[kind], candidateValues);
        NWB_COUT << "ratio_";
        PrintSummary(s_Names[kind], ratios);
    }
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(int argc, char** argv){
    const StringView mode = argc > 1 ? StringView(argv[1]) : StringView{};
    if(!mode.empty() && mode != __hidden_simd_geometry_benchmark::s_ValidateMode && mode != __hidden_simd_geometry_benchmark::s_AbsoluteMode && mode != __hidden_simd_geometry_benchmark::s_GuardMode)
        return 2;
    return __hidden_simd_geometry_benchmark::Benchmark(mode == __hidden_simd_geometry_benchmark::s_ValidateMode, mode == __hidden_simd_geometry_benchmark::s_AbsoluteMode, mode == __hidden_simd_geometry_benchmark::s_GuardMode);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

