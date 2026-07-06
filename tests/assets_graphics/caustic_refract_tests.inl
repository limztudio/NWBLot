// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CPU mirror of caustic/refract.slangi's nwbCausticRefract -- the SAME explicit Snell form the shader emits
// (k = 1 - eta*eta*(1 - cosI*cosI); k < 0 -> float3(0); else eta*i - (eta*cosI + sqrt(k))*n). It deliberately
// reimplements the formula without calling Vector3Refract so that a divergence between this formula and the shader's,
// or a bug in the formula itself, fails the test. The oracle it is checked against is Vector3Refract
// (global/math/vector.h), the runtime path RefractV backs, so the mirror, the shader, and the engine math all agree on
// the same definition including the TIR-returns-zero branch.
static SIMDVector CausticRefractCpuMirrorVector(const SIMDVector incident, const SIMDVector normal, const float eta){
    const SIMDVector etaVector = VectorReplicate(eta);
    const SIMDVector cosI = Vector3Dot(incident, normal);
    const SIMDVector k = VectorSubtract(
        s_SIMDOne,
        VectorMultiply(VectorMultiply(etaVector, etaVector), VectorSubtract(s_SIMDOne, VectorMultiply(cosI, cosI)))
    );
    if(VectorGetX(k) < 0.0f)
        return VectorZero();

    const SIMDVector scale = VectorMultiplyAdd(etaVector, cosI, VectorSqrt(k));
    return VectorSubtract(VectorMultiply(etaVector, incident), VectorMultiply(scale, normal));
}

static void CausticRefractCpuMirror(const float incident[3], const float normal[3], const float eta, float outRefracted[3]){
    const SIMDVector refracted = CausticRefractCpuMirrorVector(
        VectorSet(incident[0], incident[1], incident[2], 0.0f),
        VectorSet(normal[0], normal[1], normal[2], 0.0f),
        eta
    );
    outRefracted[0] = VectorGetX(refracted);
    outRefracted[1] = VectorGetY(refracted);
    outRefracted[2] = VectorGetZ(refracted);
}

static float CausticVector3LengthSquared(const float value[3]){
    const SIMDVector lengthSquared = Vector3LengthSq(VectorSet(value[0], value[1], value[2], 0.0f));
    return VectorGetX(lengthSquared);
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
        EXPECT_GT(CausticVector3LengthSquared(mirror), 0.25f) << testCase.name;
        EXPECT_NEAR(mirror[0], reference[0], 1e-5f) << testCase.name;
        EXPECT_NEAR(mirror[1], reference[1], 1e-5f) << testCase.name;
        EXPECT_NEAR(mirror[2], reference[2], 1e-5f) << testCase.name;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

