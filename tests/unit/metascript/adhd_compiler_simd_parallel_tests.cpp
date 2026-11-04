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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

