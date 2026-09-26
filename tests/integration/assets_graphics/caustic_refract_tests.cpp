// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_caustic_refract{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CPU mirror of nwbCausticRefract's explicit Snell form (deliberately not via Vector3Refract, so formula drift fails).
// Oracle is Vector3Refract: mirror, shader, and engine math agree incl. the TIR-zero branch.
static SIMDVector CausticRefractCpuMirrorVector(const SIMDVector incident, const SIMDVector normal, const SIMDVector eta){
    const SIMDVector cosI = Vector3Dot(incident, normal);
    const SIMDVector k = VectorSubtract(
        s_SIMDOne,
        VectorMultiply(VectorMultiply(eta, eta), VectorSubtract(s_SIMDOne, VectorMultiply(cosI, cosI)))
    );
    if(VectorGetX(k) < 0.0f)
        return VectorZero();

    const SIMDVector scale = VectorMultiplyAdd(eta, cosI, VectorSqrt(k));
    return VectorSubtract(VectorMultiply(eta, incident), VectorMultiply(scale, normal));
}

static f32 CausticVector3LengthSquared(const SIMDVector value){
    const SIMDVector lengthSquared = Vector3LengthSq(value);
    return VectorGetX(lengthSquared);
}

struct CausticRefractCase{
    const char* name;
    Float3U incident;
    Float3U normal;
    f32 eta;
    bool expectTotalInternalReflection;
};

TEST(AssetsGraphics, CausticRefractMatchesVector3Refract){
    // Incident travels into the surface, normal against it (cosI < 0; RefractV convention). eta = n_from/n_to; grazing exit -> TIR -> zero on both mirror and reference.
    static const f32 s_InvSqrt2 = 0.70710678f;
    const CausticRefractCase cases[] = {
        { "straight_on_entering", Float3U(0.0f, 0.0f, -1.0f), Float3U(0.0f, 0.0f, 1.0f), 1.0f / 1.5f, false },
        { "oblique_entering", Float3U(s_InvSqrt2, 0.0f, -s_InvSqrt2), Float3U(0.0f, 0.0f, 1.0f), 1.0f / 1.5f, false },
        { "oblique_exiting", Float3U(0.5f, 0.0f, -0.86602540f), Float3U(0.0f, 0.0f, 1.0f), 1.5f, false },
        { "tilted_axis_entering", Float3U(0.6f, -0.48f, -0.64f), Float3U(-0.42426407f, 0.56568542f, 0.70710678f), 1.0f / 1.33f, false },
        { "grazing_total_internal_reflection", Float3U(0.99619469f, 0.0f, -0.08715574f), Float3U(0.0f, 0.0f, 1.0f), 1.5f, true },
    };

    for(const CausticRefractCase& testCase : cases){
        const SIMDVector incident = LoadFloat(testCase.incident);
        const SIMDVector normal = LoadFloat(testCase.normal);
        const SIMDVector mirror = CausticRefractCpuMirrorVector(incident, normal, VectorReplicate(testCase.eta));
        const SIMDVector reference = Vector3Refract(incident, normal, testCase.eta);

        if(testCase.expectTotalInternalReflection){
            EXPECT_FLOAT_EQ(VectorGetX(mirror), 0.0f) << testCase.name;
            EXPECT_FLOAT_EQ(VectorGetY(mirror), 0.0f) << testCase.name;
            EXPECT_FLOAT_EQ(VectorGetZ(mirror), 0.0f) << testCase.name;
            EXPECT_FLOAT_EQ(VectorGetX(reference), 0.0f) << testCase.name;
            EXPECT_FLOAT_EQ(VectorGetY(reference), 0.0f) << testCase.name;
            EXPECT_FLOAT_EQ(VectorGetZ(reference), 0.0f) << testCase.name;
            continue;
        }

        // A refracting case must produce a non-degenerate direction (so the TIR check above is not vacuously passing on a zero everywhere), and it must match the SIMD reference within float tolerance.
        EXPECT_GT(CausticVector3LengthSquared(mirror), 0.25f) << testCase.name;
        EXPECT_NEAR(VectorGetX(mirror), VectorGetX(reference), 1e-5f) << testCase.name;
        EXPECT_NEAR(VectorGetY(mirror), VectorGetY(reference), 1e-5f) << testCase.name;
        EXPECT_NEAR(VectorGetZ(mirror), VectorGetZ(reference), 1e-5f) << testCase.name;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

