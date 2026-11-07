// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_simd_parallel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::AdhdCompilerTestsDetail;
namespace Compiler = NWB::Core::Metascript::Adhd::Compiler;
using NWB::Core::Metascript::Adhd::CompileResult;
using NWB::Core::Metascript::MStringView;

TEST(AdhdCompiler, RejectsNonPodNestedSerializeArrayAndUnknownWaitTarget){
    AdhdTestArena arena;

    static constexpr MStringView s_InvalidNestedSerializeSource =
        "[serialize]\r\n"
        "class BadBinaryPacket {\r\n"
        "    public Array<String, 4> names = Array<String, 4>();\r\n"
        "}\r\n";
    CompileResult badSerRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_InvalidNestedSerializeSource, "impl/scene/bad_packet.adhd", arena.arena, badSerRes));

    static constexpr MStringView s_InvalidWaitTargetSource =
        "void badWait() {\r\n"
        "    wait(NonExistentTask);\r\n"
        "}\r\n";
    CompileResult badWaitRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_InvalidWaitTargetSource, "impl/scene/bad_wait.adhd", arena.arena, badWaitRes));
}

TEST(AdhdCompiler, RejectsMismatchedTemplatedInterfaceOverrideSignature){
    AdhdTestArena arena;

    static constexpr MStringView s_MismatchedTemplatedInterfaceSource =
        "interface ISink<T> {\r\n"
        "    void send(in T item);\r\n"
        "}\r\n"
        "class BadSink : ISink<i32> {\r\n"
        "    public void send(in f32 item)override {}\r\n"
        "}\r\n";
    CompileResult badSinkRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MismatchedTemplatedInterfaceSource, "impl/net/bad_sink.adhd", arena.arena, badSinkRes));
}

TEST(AdhdCompiler, HandlesBraceCharacterLiteralsInsideNativeBlockAndRejectsDuplicateCallableSignatures){
    AdhdTestArena arena;

    static constexpr MStringView s_NativeBraceCharSource =
        "void inspectNative(mut String text) {\r\n"
        "    native(mut s = text) {\r\n"
        "        if(!s.empty() && s[0] == '{') {\r\n"
        "            s[0] = '}';\r\n"
        "        }\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    EXPECT_TRUE(Compiler::Compile(s_NativeBraceCharSource, "impl/data/indexed_bucket.adhd", arena.arena, res));

    static constexpr MStringView s_DuplicateFreeFnSource =
        "i32 dupFn(in i32 x) { return x; }\r\n"
        "i32 dupFn(in i32 x) { return x + 1; }\r\n";
    CompileResult dupFreeRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DuplicateFreeFnSource, "impl/data/dup_fn.adhd", arena.arena, dupFreeRes));

    static constexpr MStringView s_DuplicateClassMethodSource =
        "class DupMethod {\r\n"
        "    public i32 calc(in i32 x)const { return x; }\r\n"
        "    public i32 calc(in i32 y)const { return y; }\r\n"
        "}\r\n";
    CompileResult dupMethodRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DuplicateClassMethodSource, "impl/data/dup_method.adhd", arena.arena, dupMethodRes));
}

TEST(AdhdCompiler, RejectsMixedSimdSplatAndIndexInvalidVectorArityAndNonCanonicalParallelFor){
    AdhdTestArena arena;

    static constexpr MStringView s_ValidMatrixSplatSource =
        "MatrixSingle makeSplatMatrix(in f32 s) {\r\n"
        "    MatrixSingle m = MatrixSingle(s);\r\n"
        "    return m;\r\n"
        "}\r\n";
    CompileResult validMatRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_ValidMatrixSplatSource, "impl/sim/mat_splat.adhd", arena.arena, validMatRes));
    const MStringView matSrcView(validMatRes.generatedSource.data(), validMatRes.generatedSource.size());
    EXPECT_NE(matSrcView.find("::MatrixSet("), MStringView::npos);

    static constexpr MStringView s_StorageOnlySimdParamSource =
        "VectorSingle combinePlane(in VectorSingle rawNormal, in VectorSingle cameraPosition) {\r\n"
        "    return rawNormal + cameraPosition;\r\n"
        "}\r\n"
        "VectorSingle buildSidePlane(in VectorSingle forward, in VectorSingle side, in VectorSingle tanHalfAngle, in VectorSingle cameraPosition) {\r\n"
        "    const VectorSingle rawNormal = forward * tanHalfAngle + side;\r\n"
        "    return combinePlane(VectorSingle(rawNormal.x, rawNormal.y, rawNormal.z, 0.0f), cameraPosition);\r\n"
        "}\r\n";
    CompileResult storageOnlyRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_StorageOnlySimdParamSource, "impl/sim/storage_only_param.adhd", arena.arena, storageOnlyRes));
    const MStringView storageOnlyView(storageOnlyRes.generatedSource.data(), storageOnlyRes.generatedSource.size());
    const usize buildSidePos = storageOnlyView.find("buildSidePlane(");
    ASSERT_NE(buildSidePos, MStringView::npos);
    EXPECT_EQ(storageOnlyView.substr(buildSidePos).find("::LoadFloat(cameraPosition)"), MStringView::npos);

    static constexpr MStringView s_BadStorageSimdParamSource =
        "void badStorageParam(in f32x4 v) {\r\n"
        "}\r\n";
    CompileResult badStorageParamRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadStorageSimdParamSource, "impl/sim/bad_storage_param.adhd", arena.arena, badStorageParamRes));

    static constexpr MStringView s_BadSimdCalcFieldSource =
        "class BadCalcField {\r\n"
        "    public VectorSingle centroid(0.0f, 0.0f, 0.0f, 0.0f);\r\n"
        "}\r\n";
    CompileResult badSimdCalcFieldRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadSimdCalcFieldSource, "impl/sim/bad_calc_field.adhd", arena.arena, badSimdCalcFieldRes));

    static constexpr MStringView s_BadSimdCalcContainerSource =
        "void badCalcContainer() {\r\n"
        "    Array<VectorSingle, 3u> positions;\r\n"
        "}\r\n";
    CompileResult badSimdCalcContainerRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadSimdCalcContainerSource, "impl/sim/bad_calc_container.adhd", arena.arena, badSimdCalcContainerRes));

    static constexpr MStringView s_BadSimdCalcGlobalConstSource =
        "const VectorSingle s_DefaultVec(0.0f, 0.0f, 0.0f, 0.0f);\r\n";
    CompileResult badSimdCalcGlobalConstRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadSimdCalcGlobalConstSource, "impl/sim/bad_calc_const.adhd", arena.arena, badSimdCalcGlobalConstRes));

    static constexpr MStringView s_BadMixedSimdSplatSource =
        "void badMixedSplat() {\r\n"
        "    f32x3_i32 v = f32x3_i32(1.0f);\r\n"
        "}\r\n";
    CompileResult badMixedSplatRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadMixedSimdSplatSource, "impl/sim/bad_mixed_splat.adhd", arena.arena, badMixedSplatRes));

    static constexpr MStringView s_BadMixedSimdIndexSource =
        "f32 badMixedIndex() {\r\n"
        "    f32x3_i32 v(1.0f, 2.0f, 3.0f, 4);\r\n"
        "    return v[0];\r\n"
        "}\r\n";
    CompileResult badMixedIdxRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadMixedSimdIndexSource, "impl/sim/bad_mixed_idx.adhd", arena.arena, badMixedIdxRes));

    static constexpr MStringView s_BadSimdAritySource =
        "void badSimdArity() {\r\n"
        "    f32x4 v = f32x4(1.0f, 2.0f);\r\n"
        "}\r\n";
    CompileResult badSimdArityRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadSimdAritySource, "impl/sim/bad_simd_arity.adhd", arena.arena, badSimdArityRes));

    static constexpr MStringView s_BadParStepZeroSource =
        "void badParStep() {\r\n"
        "    parallel_for(usize i = 0u; i < 8u; i += 0u) {\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badParStepRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadParStepZeroSource, "impl/sim/bad_par_step.adhd", arena.arena, badParStepRes));

    static constexpr MStringView s_BadParFloatIndexSource =
        "void badParFloatIdx() {\r\n"
        "    parallel_for(f32 i = 0.0f; i < 8.0f; ++i) {\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badParFloatRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadParFloatIndexSource, "impl/sim/bad_par_float.adhd", arena.arena, badParFloatRes));

    static constexpr MStringView s_BadMutThisLambdaInParSource =
        "class Collector {\r\n"
        "    public Vector<i32> items;\r\n"
        "    public void runParallel() {\r\n"
        "        auto addFn = lambda[mut this](in i32 v) -> void {\r\n"
        "            this.items.push_back(v);\r\n"
        "        };\r\n"
        "        parallel_for(usize i = 0u; i < 4u; ++i) {\r\n"
        "            addFn(cast<i32>(i));\r\n"
        "        }\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badMutThisParRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadMutThisLambdaInParSource, "impl/sim/bad_mut_this_par.adhd", arena.arena, badMutThisParRes));

    static constexpr MStringView s_SimdMutCondSaturateMatRowSource =
        "b8 tryLoadVec(mut VectorSingle outVec) {\r\n"
        "    outVec = VectorSingle(1.0f, 2.0f, 3.0f, 4.0f);\r\n"
        "    return true;\r\n"
        "}\r\n"
        "f32 computeFromCondAndMat(in MatrixSingle mat) {\r\n"
        "    VectorSingle trackMin = VectorSingle(0.0f, 0.0f, 0.0f, 0.0f);\r\n"
        "    if(!tryLoadVec(trackMin))\r\n"
        "        return 0.0f;\r\n"
        "    const VectorSingle clamped = saturate(trackMin);\r\n"
        "    return clamped.x + mat[0].x;\r\n"
        "}\r\n";
    CompileResult mutCondRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_SimdMutCondSaturateMatRowSource, "impl/sim/mut_cond_mat.adhd", arena.arena, mutCondRes));
    const MStringView mutCondView(mutCondRes.generatedSource.data(), mutCondRes.generatedSource.size());
    EXPECT_NE(mutCondView.find("::VectorSaturate(trackMin)"), MStringView::npos);
    EXPECT_NE(mutCondView.find("mat.v[0]"), MStringView::npos);

    static constexpr MStringView s_SimdLaneInsertAndPermuteSource =
        "VectorSingle normalizeTangentWithSign(in VectorSingle tangent, in VectorSingle normal, in f32x3u posIn, mut f32x3u outPos) {\r\n"
        "    const VectorSingle loadedPos = VectorSingle(posIn.x, posIn.y, posIn.z, 0.0f);\r\n"
        "    outPos = f32x3u(loadedPos.x, loadedPos.y, loadedPos.z);\r\n"
        "    if(lengthSq(tangent.xyz) > 0.0001f) {\r\n"
        "        const VectorSingle normalized = normalize(tangent.xyz);\r\n"
        "        f32 sign = 1.0f;\r\n"
        "        if(tangent.w < 0.0f)\r\n"
        "            sign = -1.0f;\r\n"
        "        return VectorSingle(normalized.x, normalized.y, normalized.z, sign);\r\n"
        "    }\r\n"
        "    const VectorSingle fallback = normalize(normal.xyz);\r\n"
        "    return VectorSingle(fallback.x, fallback.y, fallback.z, normal.w);\r\n"
        "}\r\n";
    CompileResult laneInsertRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_SimdLaneInsertAndPermuteSource, "impl/sim/lane_insert.adhd", arena.arena, laneInsertRes));
    const MStringView laneInsertView(laneInsertRes.generatedSource.data(), laneInsertRes.generatedSource.size());
    EXPECT_NE(laneInsertView.find("::LoadFloat(posIn)"), MStringView::npos);
    EXPECT_NE(laneInsertView.find("::StoreFloat(loadedPos, outPos)"), MStringView::npos);
    EXPECT_NE(laneInsertView.find("::VectorSetW(normalized, sign)"), MStringView::npos);
    EXPECT_NE(laneInsertView.find("::VectorPermute<0u, 1u, 2u, 7u>(fallback, normal)"), MStringView::npos);
    EXPECT_EQ(laneInsertView.find("::VectorSet("), MStringView::npos);
}

TEST(AdhdCompiler, OptimizesSimdConstantsMasksSwizzlesReductionsTransformsAndLogicChains){
    AdhdTestArena arena;

    static constexpr MStringView s_SimdOptSource =
        "const f32 s_AffineW = 1.0f;\r\n"
        "const f32x4 s_ZeroVec(0.0f, 0.0f, 0.0f, 0.0f);\r\n"
        "const f32x4 s_AxisY(0.0f, 1.0f, 0.0f, 0.0f);\r\n"
        "class BoxData {\r\n"
        "    public f32x3u minPt = f32x3u(0.0f, 0.0f, 0.0f);\r\n"
        "    public f32x4 extent4 = f32x4(0.0f, 0.0f, 0.0f, 0.0f);\r\n"
        "}\r\n"
        "VectorSingle optimizeSimdOps(in VectorSingle a, in VectorSingle b, in VectorDouble d, in MatrixSingle m, in BoxData box, in f32 scale, mut BoxData outBox) {\r\n"
        "    const MatrixSingle idMat = MatrixSingle(\r\n"
        "        1.0f, 0.0f, 0.0f, 0.0f,\r\n"
        "        0.0f, 1.0f, 0.0f, 0.0f,\r\n"
        "        0.0f, 0.0f, 1.0f, 0.0f,\r\n"
        "        0.0f, 0.0f, 0.0f, 1.0f\r\n"
        "    );\r\n"
        "    const MatrixSingle scaledMat = idMat * scale;\r\n"
        "    const MatrixSingle projMat = MatrixSingle(\r\n"
        "        a.x, 0.0f, 0.0f, 0.0f,\r\n"
        "        0.0f, a.y, 0.0f, 0.0f,\r\n"
        "        0.0f, 0.0f, a.z, a.w,\r\n"
        "        0.0f, 0.0f, 1.0f, 0.0f\r\n"
        "    );\r\n"
        "    const VectorSingle unitMul = (a * 1.0f) + (b * -1.0f);\r\n"
        "    const VectorSingle negFma = (-a) * b + unitMul;\r\n"
        "    const VectorSingle splatMul = VectorSingle(scale) * 2.0f;\r\n"
        "    const VectorSingle pad3 = VectorSingle(scale, scale, scale, 0.0f);\r\n"
        "    const VectorSingle signFlipSplat = VectorSingle(scale, -scale, scale, -scale);\r\n"
        "    const f32 dot3ZeroW = dot(a, VectorSingle(1.0f, 2.0f, 3.0f, 0.0f));\r\n"
        "    const f32 sumAll = dot(a, VectorSingle(1.0f));\r\n"
        "    const VectorSingle satClamp = clamp(a, 0.0f, 1.0f);\r\n"
        "    const VectorSingle satMinMax = min(max(b, 0.0f), 1.0f);\r\n"
        "    const VectorSingle storageSwiz = VectorSingle(box.minPt.z, box.minPt.y, box.minPt.x, 0.0f);\r\n"
        "    const VectorSingle storageBinOp = VectorSingle(box.minPt.x + box.extent4.x, box.minPt.y + box.extent4.y, box.minPt.z + box.extent4.z, 0.0f);\r\n"
        "    const VectorSingle storageNeg3 = VectorSingle(-box.minPt.x, -box.minPt.y, -box.minPt.z, 0.0f);\r\n"
        "    const VectorSingle laneMathMin = VectorSingle(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z), min(a.w, b.w));\r\n"
        "    const VectorSingle altMaskSplat = VectorSingle(scale, 0.0f, scale, 0.0f);\r\n"
        "    const VectorSingle zeroSubAdd = (0.0f - a) + (b - 0.0f);\r\n"
        "    const VectorSingle lerpZero = lerp(a, b, 0.0f);\r\n"
        "    const f32 swizGetX = (a.zyx).x;\r\n"
        "    const MatrixSingle mulTrans = transpose(m * scaledMat);\r\n"
        "    const f32x4 zeroStorageVec = f32x4(0.0f);\r\n"
        "    const f32x4x4 zeroStorageMat = f32x4x4(0.0f);\r\n"
        "    const VectorSingle unitVec = VectorSingle(1.0f, 1.0f, 1.0f, 1.0f);\r\n"
        "    const VectorSingle halfVec = VectorSingle(0.5f);\r\n"
        "    const VectorSingle axisW = VectorSingle(0.0f, 0.0f, 0.0f, 1.0f);\r\n"
        "    const VectorSingle constZero = VectorSingle(s_ZeroVec);\r\n"
        "    const VectorSingle constAxisY = VectorSingle(s_AxisY);\r\n"
        "    const VectorSingle negRep = VectorSingle(-scale, -scale, -scale, -scale);\r\n"
        "    const VectorSingle masked3 = VectorSingle(a.x, a.y, a.z, 0.0f);\r\n"
        "    const VectorSingle clearX = VectorSingle(0.0f, a.y, a.z, a.w);\r\n"
        "    const VectorSingle flipYW = VectorSingle(a.x, -a.y, a.z, -a.w);\r\n"
        "    const VectorSingle storageMaskXY = VectorSingle(box.extent4.x, box.extent4.y, 0.0f, 0.0f);\r\n"
        "    const VectorSingle neg3KeepW = VectorSingle(-a.x, -a.y, -a.z, a.w);\r\n"
        "    const VectorSingle laneBinOp = VectorSingle(a.x * scale, a.y * scale, a.z * scale, 0.0f);\r\n"
        "    const VectorSingle axisYWithW = VectorSingle(0.0f, 1.0f, 0.0f, a.w);\r\n"
        "    const VectorDouble xyUnitZW = VectorDouble(d.x, d.y, 1.0, 1.0);\r\n"
        "    const VectorSingle splatZ = a.zzzz;\r\n"
        "    const VectorSingle foldedSwiz = (a.yzxw).wxyz;\r\n"
        "    const VectorSingle identitySwiz = (a.yzxw).zxyw;\r\n"
        "    const VectorSingle fromDouble = VectorSingle(cast<f32>(d.x), cast<f32>(d.y), cast<f32>(d.z), cast<f32>(d.w));\r\n"
        "    const VectorSingle proj = a * dot(a.xyz, b.xyz);\r\n"
        "    const VectorSingle scaled = b * VectorSingle(scale);\r\n"
        "    const VectorSingle invLen = a * rsqrt(lengthSq(a.xyz));\r\n"
        "    const VectorSingle pt = mul(VectorSingle(box.minPt, s_AffineW), scaledMat);\r\n"
        "    const VectorSingle pt2 = mul(VectorSingle(a.x, a.y, 0.0f, 1.0f), projMat);\r\n"
        "    const VectorSingle nrm = mul(VectorSingle(b, 0.0f), m) + unitMul;\r\n"
        "    outBox.extent4 = f32x4(a, b.w);\r\n"
        "    if(!isFinite(d.x) || d.x > 10.0 || !isFinite(d.y) || d.y > 10.0)\r\n"
        "        return constZero;\r\n"
        "    if(abs(box.minPt.x) > 1.0f || abs(box.minPt.y) > 1.0f || abs(box.minPt.z) > 1.0f)\r\n"
        "        return constAxisY;\r\n"
        "    if(!isFinite(a.x) || !isFinite(a.y) || !isFinite(a.z) || !isFinite(a.w) || a.x < 0.0f || a.y < 0.0f || a.z < 0.0f || a.w < 0.0f)\r\n"
        "        return unitVec;\r\n"
        "    if(a.x <= a.z && a.y <= a.w && isFinite(b.xyz) && a.x == b.x && a.y == b.y && a.z == b.z)\r\n"
        "        return halfVec + axisW + VectorSingle(xyUnitZW);\r\n"
        "    return masked3 + clearX + flipYW + storageMaskXY + negRep + neg3KeepW + laneBinOp + axisYWithW + splatZ + foldedSwiz + identitySwiz + fromDouble + proj + scaled + invLen + pt + pt2 + nrm + negFma + splatMul + pad3 + signFlipSplat + satClamp + satMinMax + storageSwiz + storageBinOp + storageNeg3 + laneMathMin + altMaskSplat + zeroSubAdd + lerpZero + mul(a, mulTrans) + VectorSingle(dot3ZeroW + sumAll + swizGetX + zeroStorageVec.x + zeroStorageMat[0][0]);\r\n"
        "}\r\n";

    CompileResult optRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_SimdOptSource, "impl/sim/simd_opt.adhd", arena.arena, optRes));
    const MStringView optView(optRes.generatedSource.data(), optRes.generatedSource.size());

    EXPECT_NE(optView.find("::MatrixIdentity()"), MStringView::npos);
    EXPECT_EQ(optView.find("for(usize __i = 0u; __i < 4u; ++__i)"), MStringView::npos);
    EXPECT_NE(optView.find("::SIMDMatrix{ .v = { ::VectorAndInt(a, ::s_SIMDMaskX), ::VectorAndInt(a, ::s_SIMDMaskY), ::VectorAndCInt(a, ::s_SIMDMaskXY), ::s_SIMDIdentityR2 } }"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle unitMul = ::VectorSubtract(a, b);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle negFma = ::VectorNegativeMultiplySubtract(a, b, unitMul);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle splatMul = ::VectorReplicate((scale * 2.0f));"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle pad3 = ::VectorAndInt(::VectorReplicate(scale), ::s_SIMDMask3);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle signFlipSplat = ::VectorXorInt(::VectorReplicate(scale), ::s_SIMDFlipYW);"), MStringView::npos);
    EXPECT_NE(optView.find("const f32 dot3ZeroW = ::VectorGetX(::Vector3Dot(a, ::VectorSet(1.0f, 2.0f, 3.0f, 0.0f)));"), MStringView::npos);
    EXPECT_NE(optView.find("const f32 sumAll = ::VectorGetX(::VectorSum(a));"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle satClamp = ::VectorSaturate(a);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle satMinMax = ::VectorSaturate(b);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle storageSwiz = ::VectorSwizzle<2u, 1u, 0u, 3u>(::LoadFloat(box.minPt));"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle storageBinOp = ::VectorAndInt(::VectorAdd(::LoadFloat(box.minPt), ::LoadFloat(box.extent4)), ::s_SIMDMask3);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle storageNeg3 = ::VectorXorInt(::LoadFloat(box.minPt), ::s_SIMDNegate3);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle laneMathMin = ::VectorMin(a, b);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle altMaskSplat = ::VectorAndCInt(::VectorReplicate(scale), ::s_SIMDSelect0101);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle zeroSubAdd = ::VectorSubtract(b, a);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle lerpZero = a;"), MStringView::npos);
    EXPECT_NE(optView.find("const f32 swizGetX = ::VectorGetZ(a);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDMatrix mulTrans = ::MatrixMultiplyTranspose(m, scaledMat);"), MStringView::npos);
    EXPECT_NE(optView.find("const Float4 zeroStorageVec = Float4{};"), MStringView::npos);
    EXPECT_NE(optView.find("const Float44 zeroStorageMat = Float44{};"), MStringView::npos);
    EXPECT_NE(optView.find("::s_SIMDOne"), MStringView::npos);
    EXPECT_NE(optView.find("::s_SIMDOneHalf"), MStringView::npos);
    EXPECT_NE(optView.find("::s_SIMDIdentityR3"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle constZero = ::VectorZero();"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle constAxisY = ::s_SIMDIdentityR1;"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorReplicate(-scale)"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorAndInt(a, ::s_SIMDMask3)"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorAndCInt(a, ::s_SIMDMaskX)"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorXorInt(a, ::s_SIMDFlipYW)"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorAndInt(::LoadFloat(box.extent4), ::s_SIMDMaskXY)"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorXorInt(a, ::s_SIMDNegate3)"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorAndInt(::VectorScale(a, scale), ::s_SIMDMask3)"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorPermute<4u, 5u, 6u, 3u>(a, ::s_SIMDIdentityR1)"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorDoublePermute<0u, 1u, 6u, 7u>(d, ::s_DoubleSIMDOne)"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorSplatZ(a)"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorSwizzle<3u, 1u, 2u, 0u>(a)"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle identitySwiz = a;"), MStringView::npos);
    EXPECT_NE(optView.find("::ConvertVectorDoubleToSingle(d)"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorMultiply(a, ::Vector3Dot(a, b))"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorScale(b, scale)"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorMultiply(a, ::Vector3ReciprocalLength(a))"), MStringView::npos);
    EXPECT_NE(optView.find("::Vector3Transform(::LoadFloat(box.minPt), scaledMat)"), MStringView::npos);
    EXPECT_NE(optView.find("::Vector2Transform(a, projMat)"), MStringView::npos);
    EXPECT_NE(optView.find("::Vector3TransformNormal(b, m)"), MStringView::npos);
    EXPECT_NE(optView.find("::StoreFloat(::VectorPermute<0u, 1u, 2u, 7u>(a, b), outBox.extent4)"), MStringView::npos);
    EXPECT_NE(optView.find("!::VectorDoubleIsFinite(d, ::VectorDoubleComponentMask::s_DoubleXY) || ((::VectorDoubleMoveMask(::VectorDoubleGreater(d, ::VectorDoubleReplicate(10.0))) & ::VectorDoubleComponentMask::s_DoubleXY) != 0u)"), MStringView::npos);
    EXPECT_NE(optView.find("((::VectorMoveMask(::VectorGreater(::VectorAbs(::LoadFloat(box.minPt)), ::s_SIMDOne)) & ::VectorComponentMask::s_XYZ) != 0u)"), MStringView::npos);
    EXPECT_NE(optView.find("!::VectorIsFinite(a, ::VectorComponentMask::s_XYZW)"), MStringView::npos);
    EXPECT_NE(optView.find("((::VectorMoveMask(::VectorLess(a, ::VectorZero())) & ::VectorComponentMask::s_XYZW) != 0u)"), MStringView::npos);
    EXPECT_NE(optView.find("::Vector2LessOrEqual(a, ::VectorSwizzle<2u, 3u, 2u, 3u>(a))"), MStringView::npos);
    EXPECT_NE(optView.find("::VectorIsFinite(b, ::VectorComponentMask::s_XYZ)"), MStringView::npos);
    EXPECT_NE(optView.find("::Vector3Equal(a, b)"), MStringView::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
