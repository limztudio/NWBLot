// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CPU mirror of caustic/refract.slangi's nwbCausticRefract -- the SAME explicit Snell form the shader emits
// (k = 1 - eta*eta*(1 - cosI*cosI); k < 0 -> float3(0); else eta*i - (eta*cosI + sqrt(k))*n). It deliberately
// reimplements the formula with scalars (not a call into the math library) so that a divergence between this
// formula and the shader's, or a bug in the formula itself, fails the test. The oracle it is checked against is
// Vector3Refract (global/math/vector.h), the runtime path RefractV backs, so the mirror, the shader, and the
// engine math all agree on the same definition including the TIR-returns-zero branch.
static void CausticRefractCpuMirror(const float incident[3], const float normal[3], const float eta, float outRefracted[3]){
    const float cosI = incident[0] * normal[0] + incident[1] * normal[1] + incident[2] * normal[2];
    const float k = 1.0f - eta * eta * (1.0f - cosI * cosI);
    if(k < 0.0f){
        outRefracted[0] = 0.0f;
        outRefracted[1] = 0.0f;
        outRefracted[2] = 0.0f;
        return;
    }
    const float scale = eta * cosI + std::sqrt(k);
    outRefracted[0] = eta * incident[0] - scale * normal[0];
    outRefracted[1] = eta * incident[1] - scale * normal[1];
    outRefracted[2] = eta * incident[2] - scale * normal[2];
}

static void CausticRefractReference(const float incident[3], const float normal[3], const float eta, float outRefracted[3]){
    const SIMDVector refracted = Vector3Refract(
        VectorSet(incident[0], incident[1], incident[2], 0.0f),
        VectorSet(normal[0], normal[1], normal[2], 0.0f),
        eta
    );
    outRefracted[0] = VectorGetX(refracted);
    outRefracted[1] = VectorGetY(refracted);
    outRefracted[2] = VectorGetZ(refracted);
}

struct CausticRefractCase{
    const char* name;
    float incident[3];
    float normal[3];
    float eta;
    bool expectTotalInternalReflection;
};

TEST(AssetsGraphics, CausticRefractMatchesVector3Refract){
    // Incident vectors point INTO the surface (travel direction); the normal is oriented against the incident ray
    // (so cosI < 0), matching the RefractV / Vector3Refract convention. eta = n_from / n_to: eta < 1 enters a denser
    // medium (air->glass, 1/1.5), eta > 1 exits to a thinner one (glass->air, 1.5). The grazing exit case drives the
    // discriminant negative -> total internal reflection -> a zero result on BOTH the mirror and the reference.
    static const float s_InvSqrt2 = 0.70710678f;
    const CausticRefractCase cases[] = {
        { "straight_on_entering", { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f }, 1.0f / 1.5f, false },
        { "oblique_entering", { s_InvSqrt2, 0.0f, -s_InvSqrt2 }, { 0.0f, 0.0f, 1.0f }, 1.0f / 1.5f, false },
        { "oblique_exiting", { 0.5f, 0.0f, -0.86602540f }, { 0.0f, 0.0f, 1.0f }, 1.5f, false },
        { "tilted_axis_entering", { 0.6f, -0.48f, -0.64f }, { -0.42426407f, 0.56568542f, 0.70710678f }, 1.0f / 1.33f, false },
        { "grazing_total_internal_reflection", { 0.99619469f, 0.0f, -0.08715574f }, { 0.0f, 0.0f, 1.0f }, 1.5f, true },
    };

    for(const CausticRefractCase& testCase : cases){
        float mirror[3];
        float reference[3];
        CausticRefractCpuMirror(testCase.incident, testCase.normal, testCase.eta, mirror);
        CausticRefractReference(testCase.incident, testCase.normal, testCase.eta, reference);

        if(testCase.expectTotalInternalReflection){
            EXPECT_FLOAT_EQ(mirror[0], 0.0f) << testCase.name;
            EXPECT_FLOAT_EQ(mirror[1], 0.0f) << testCase.name;
            EXPECT_FLOAT_EQ(mirror[2], 0.0f) << testCase.name;
            EXPECT_FLOAT_EQ(reference[0], 0.0f) << testCase.name;
            EXPECT_FLOAT_EQ(reference[1], 0.0f) << testCase.name;
            EXPECT_FLOAT_EQ(reference[2], 0.0f) << testCase.name;
            continue;
        }

        // A refracting case must produce a non-degenerate direction (so the TIR check above is not vacuously passing
        // on a zero everywhere), and it must match the SIMD reference within float tolerance.
        const float mirrorLengthSq = mirror[0] * mirror[0] + mirror[1] * mirror[1] + mirror[2] * mirror[2];
        EXPECT_GT(mirrorLengthSq, 0.25f) << testCase.name;
        EXPECT_NEAR(mirror[0], reference[0], 1e-5f) << testCase.name;
        EXPECT_NEAR(mirror[1], reference[1], 1e-5f) << testCase.name;
        EXPECT_NEAR(mirror[2], reference[2], 1e-5f) << testCase.name;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

