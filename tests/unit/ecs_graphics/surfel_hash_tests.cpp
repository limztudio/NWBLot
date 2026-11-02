// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/array.h>
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
    return float3(value.x / divisor, value.y / divisor, value.z / divisor);
}

b8 operator==(const int3 first, const int3 second)noexcept{
    return first.x == second.x && first.y == second.y && first.z == second.z;
}

float3 FloorShaderVector(const float3 value)noexcept{
    float3 floored;
    StoreFloat(VectorFloor(LoadFloat(value)), floored);
    return floored;
}

b8 AllShaderLanes(const b8 value)noexcept{ return value; }

#define floor FloorShaderVector
#define max Max
#define all AllShaderLanes
#include <impl/assets/graphics/gi/surfel/surfel_hash.slangi>
#undef all
#undef max
#undef floor


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FieldRecord{
    float3 position;
    f32 radiance;
    int3 cell;
};

f32 GatherBoundaryValue(const float3 position, const b8 validateCell)noexcept{
    // Move only lane Z.
    const float3 biasedPosition(position.x, position.y, position.z + 0.05f);
    const int3 baseCell = nwbSurfelCellCoord(biasedPosition, NWB_SURFEL_CELL_SIZE);
    const Array<FieldRecord, 2> records = {{
        { float3(0.61f, 0.61f, 0.0f), 1.0f, nwbSurfelCellCoord(float3(0.61f, 0.61f, 0.0f), NWB_SURFEL_CELL_SIZE) },
        { float3(1.21f, 0.61f, 0.0f), 0.0f, nwbSurfelCellCoord(float3(1.21f, 0.61f, 0.0f), NWB_SURFEL_CELL_SIZE) },
    }};
    f32 irradiance = 0.0f;
    f32 totalWeight = 0.0f;
    for(i32 z = -2; z <= 2; ++z)
    for(i32 y = -2; y <= 2; ++y)
    for(i32 x = -2; x <= 2; ++x){
        const int3 visitedCell(baseCell.x + x, baseCell.y + y, baseCell.z + z);
        const u32 visitedBucket = nwbSurfelHashCell(visitedCell, NWB_SURFEL_HASH_CELL_COUNT);
        for(const FieldRecord& record : records){
            if(nwbSurfelHashCell(record.cell, NWB_SURFEL_HASH_CELL_COUNT) != visitedBucket)
                continue;
            if(validateCell && !nwbSurfelSameCell(record.cell, visitedCell))
                continue;
            const f32 dx = biasedPosition.x - record.position.x;
            const f32 dy = biasedPosition.y - record.position.y;
            const f32 dz = biasedPosition.z - record.position.z;
            const f32 distance = Sqrt(dx * dx + dy * dy + dz * dz);
            const f32 weight = Max(1.0f - (distance / NWB_SURFEL_DEFAULT_RADIUS), 0.0f);
            irradiance += record.radiance * weight;
            totalWeight += weight;
        }
    }
    return irradiance / totalWeight;
}


TEST(SurfelHash, SymmetricHashCollisionAdmitsEachResidentCellOnce){
    const int3 negative(-1, -1, 0);
    const int3 positive(1, 1, 0);
    ASSERT_EQ(nwbSurfelHashCell(negative, NWB_SURFEL_HASH_CELL_COUNT), nwbSurfelHashCell(positive, NWB_SURFEL_HASH_CELL_COUNT));
    const Array<float3, 2> residents = { float3(-0.01f, -0.01f, 0.0f), float3(0.61f, 0.61f, 0.0f) };
    const Array<int3, 2> residentCells = { nwbSurfelCellCoord(residents[0], NWB_SURFEL_CELL_SIZE), nwbSurfelCellCoord(residents[1], NWB_SURFEL_CELL_SIZE) };
    Array<u32, 2> bucketVisits{};
    Array<u32, 2> admittedVisits{};
    for(i32 z = -2; z <= 2; ++z)
    for(i32 y = -2; y <= 2; ++y)
    for(i32 x = -2; x <= 2; ++x){
        const int3 visitedCell(x, y, z);
        const u32 visitedBucket = nwbSurfelHashCell(visitedCell, NWB_SURFEL_HASH_CELL_COUNT);
        for(usize index = 0u; index < 2u; ++index){
            if(nwbSurfelHashCell(residentCells[index], NWB_SURFEL_HASH_CELL_COUNT) != visitedBucket)
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
    ASSERT_EQ(nwbSurfelCellCoord(before, NWB_SURFEL_CELL_SIZE).x, 1);
    ASSERT_EQ(nwbSurfelCellCoord(after, NWB_SURFEL_CELL_SIZE).x, 2);
    EXPECT_GT(Abs(GatherBoundaryValue(before, false) - GatherBoundaryValue(after, false)), 0.1f);
    EXPECT_NEAR(GatherBoundaryValue(before, true), GatherBoundaryValue(after, true), 1e-5f);
}

TEST(SurfelHash, CollidingDistantCellsRetainUpperCoordinateBits){
    const int3 nearCell(1, -1, 0);
    const int3 distantCell(1 + static_cast<i32>(NWB_SURFEL_HASH_CELL_COUNT), -1, 0);
    ASSERT_EQ(nwbSurfelHashCell(nearCell, NWB_SURFEL_HASH_CELL_COUNT), nwbSurfelHashCell(distantCell, NWB_SURFEL_HASH_CELL_COUNT));
    EXPECT_FALSE(nwbSurfelSameCell(distantCell, nearCell));
    EXPECT_FALSE(nwbSurfelSameCell(nearCell, distantCell));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

