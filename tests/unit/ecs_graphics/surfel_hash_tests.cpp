// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/simd/type.h>
#include <global/simd/convert.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_surfel_hash_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Compile the production Slang cell/hash predicates using C++ equivalents of its vector builtins.
using float3 = Float3U;
using uint = u32;

struct ShaderInt3{
    i32 x;
    i32 y;
    i32 z;


public:
    ShaderInt3(const i32 xValue, const i32 yValue, const i32 zValue)noexcept
        : x(xValue)
        , y(yValue)
        , z(zValue)
    {}
    explicit ShaderInt3(const float3 value)noexcept
        : x(static_cast<i32>(value.x))
        , y(static_cast<i32>(value.y))
        , z(static_cast<i32>(value.z))
    {}
};
using int3 = ShaderInt3;

float3 operator/(const float3 value, const f32 divisor)noexcept{
    float3 quotient;
    StoreFloat(VectorDivide(LoadFloat(value), VectorSet(divisor, divisor, divisor, 1.0f)), quotient);
    return quotient;
}

bool operator==(const int3 first, const int3 second)noexcept{
    return first.x == second.x && first.y == second.y && first.z == second.z;
}

float3 FloorShaderVector(const float3 value)noexcept{
    float3 floored;
    StoreFloat(VectorFloor(LoadFloat(value)), floored);
    return floored;
}

bool AllShaderLanes(const bool value)noexcept{ return value; }

#define floor FloorShaderVector
#define max Max
#define all AllShaderLanes
#include <impl/assets/graphics/gi/surfel/surfel_hash.slangi>
#undef all
#undef max
#undef floor


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr f32 s_CellSize = NWB_SURFEL_CELL_SIZE;
constexpr u32 s_CellCount = NWB_SURFEL_HASH_CELL_COUNT;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FieldRecord{
    float3 position;
    f32 radiance;
    int3 cell;
};

SIMDVectorSingle GatherBoundaryValue(const SIMDVectorSingle position, const bool validateCell)noexcept{
    // Only lane Z moves; the fixture lanes are nonzero or +0, so adding +0 keeps them.
    const SIMDVectorSingle biased = VectorAdd(position, VectorSet(0.0f, 0.0f, 0.05f, 0.0f));
    float3 biasedPosition;
    StoreFloat(biased, biasedPosition);
    const int3 baseCell = nwbSurfelCellCoord(biasedPosition, s_CellSize);
    const FieldRecord records[]{
        { float3(0.61f, 0.61f, 0.0f), 1.0f, nwbSurfelCellCoord(float3(0.61f, 0.61f, 0.0f), s_CellSize) },
        { float3(1.21f, 0.61f, 0.0f), 0.0f, nwbSurfelCellCoord(float3(1.21f, 0.61f, 0.0f), s_CellSize) },
    };
    SIMDVectorSingle irradiance = VectorZero();
    SIMDVectorSingle totalWeight = VectorZero();
    for(i32 z = -2; z <= 2; ++z)
    for(i32 y = -2; y <= 2; ++y)
    for(i32 x = -2; x <= 2; ++x){
        const int3 visitedCell(baseCell.x + x, baseCell.y + y, baseCell.z + z);
        const u32 visitedBucket = nwbSurfelHashCell(visitedCell, s_CellCount);
        for(const FieldRecord& record : records){
            if(nwbSurfelHashCell(record.cell, s_CellCount) != visitedBucket)
                continue;
            if(validateCell && !nwbSurfelSameCell(record.cell, visitedCell))
                continue;
            const SIMDVectorSingle delta = VectorSubtract(biased, LoadFloat(record.position));
            const SIMDVectorSingle dx = VectorSplatX(delta);
            const SIMDVectorSingle dy = VectorSplatY(delta);
            const SIMDVectorSingle dz = VectorSplatZ(delta);
            const SIMDVectorSingle distance = VectorSqrt(VectorMultiplyAddExpression(dz, dz, VectorMultiplyAddExpression(dx, dx, VectorMultiply(dy, dy))));
            const SIMDVectorSingle weight = VectorMax(VectorSubtract(VectorSplatOne(), VectorDivide(distance, VectorReplicate(NWB_SURFEL_DEFAULT_RADIUS))), VectorZero());
            irradiance = VectorMultiplyAddExpression(VectorReplicate(record.radiance), weight, irradiance);
            totalWeight = VectorAdd(totalWeight, weight);
        }
    }
    return VectorDivide(irradiance, totalWeight);
}


TEST(SurfelHash, SymmetricHashCollisionAdmitsEachResidentCellOnce){
    const int3 negative(-1, -1, 0);
    const int3 positive(1, 1, 0);
    ASSERT_EQ(nwbSurfelHashCell(negative, s_CellCount), nwbSurfelHashCell(positive, s_CellCount));
    const float3 residents[]{ float3(-0.01f, -0.01f, 0.0f), float3(0.61f, 0.61f, 0.0f) };
    const int3 residentCells[]{ nwbSurfelCellCoord(residents[0], s_CellSize), nwbSurfelCellCoord(residents[1], s_CellSize) };
    u32 bucketVisits[2]{};
    u32 admittedVisits[2]{};
    for(i32 z = -2; z <= 2; ++z)
    for(i32 y = -2; y <= 2; ++y)
    for(i32 x = -2; x <= 2; ++x){
        const int3 visitedCell(x, y, z);
        const u32 visitedBucket = nwbSurfelHashCell(visitedCell, s_CellCount);
        for(usize index = 0u; index < 2u; ++index){
            if(nwbSurfelHashCell(residentCells[index], s_CellCount) != visitedBucket)
                continue;
            ++bucketVisits[index];
            if(nwbSurfelSameCell(residentCells[index], visitedCell))
                ++admittedVisits[index];
        }
    }
    for(usize index = 0u; index < 2u; ++index){
        EXPECT_GT(bucketVisits[index], 1u);
        EXPECT_EQ(admittedVisits[index], 1u);
    }
}

TEST(SurfelHash, NeighborCellBoundaryPreservesTheNormalizedField){
    const float3 before(1.2f - 1e-6f, 0.61f, 0.0f);
    const float3 after(1.2f + 1e-6f, 0.61f, 0.0f);
    ASSERT_EQ(nwbSurfelCellCoord(before, s_CellSize).x, 1);
    ASSERT_EQ(nwbSurfelCellCoord(after, s_CellSize).x, 2);
    EXPECT_GT(VectorGetX(VectorAbs(VectorSubtract(GatherBoundaryValue(LoadFloat(before), false), GatherBoundaryValue(LoadFloat(after), false)))), 0.1f);
    EXPECT_NEAR(VectorGetX(GatherBoundaryValue(LoadFloat(before), true)), VectorGetX(GatherBoundaryValue(LoadFloat(after), true)), 1e-5f);
}

TEST(SurfelHash, CollidingDistantCellsRetainUpperCoordinateBits){
    const int3 nearCell(1, -1, 0);
    const int3 distantCell(1 + static_cast<i32>(s_CellCount), -1, 0);
    ASSERT_EQ(nwbSurfelHashCell(nearCell, s_CellCount), nwbSurfelHashCell(distantCell, s_CellCount));
    EXPECT_FALSE(nwbSurfelSameCell(distantCell, nearCell));
    EXPECT_FALSE(nwbSurfelSameCell(nearCell, distantCell));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

