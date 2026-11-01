// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_simd_parallel_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::AdhdCompilerTestsDetail;
using NWB::Core::Metascript::Adhd::Compiler;
using NWB::Core::Metascript::Adhd::CompileResult;
using NWB::Core::Metascript::MString;
using NWB::Core::Metascript::MStringView;
using NWB::Core::Metascript::MetaArena;

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
        "    public void send(in f32 item) override {}\r\n"
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
        "f32x4x4 makeSplatMatrix(in f32 s) {\r\n"
        "    f32x4x4 m = f32x4x4(s);\r\n"
        "    return m;\r\n"
        "}\r\n";
    CompileResult validMatRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_ValidMatrixSplatSource, "impl/sim/mat_splat.adhd", arena.arena, validMatRes));
    const MStringView matSrcView(validMatRes.generatedSource.data(), validMatRes.generatedSource.size());
    EXPECT_NE(matSrcView.find("::MatrixSet("), MStringView::npos);

    static constexpr MStringView s_StorageOnlySimdParamSource =
        "f32x4 combinePlane(in f32x4 rawNormal, in f32x4 cameraPosition) {\r\n"
        "    return rawNormal + cameraPosition;\r\n"
        "}\r\n"
        "f32x4 buildSidePlane(in f32x4 forward, in f32x4 side, in f32x4 tanHalfAngle, in f32x4 cameraPosition) {\r\n"
        "    const f32x4 rawNormal = forward * tanHalfAngle + side;\r\n"
        "    return combinePlane(f32x4(rawNormal.x, rawNormal.y, rawNormal.z, 0.0f), cameraPosition);\r\n"
        "}\r\n";
    CompileResult storageOnlyRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_StorageOnlySimdParamSource, "impl/sim/storage_only_param.adhd", arena.arena, storageOnlyRes));
    const MStringView storageOnlyView(storageOnlyRes.generatedSource.data(), storageOnlyRes.generatedSource.size());
    const usize buildSidePos = storageOnlyView.find("buildSidePlane(");
    ASSERT_NE(buildSidePos, MStringView::npos);
    EXPECT_EQ(storageOnlyView.substr(buildSidePos).find("__adhd_simd_cameraPosition"), MStringView::npos);

    static constexpr MStringView s_BadMixedSimdSplatSource =
        "void badMixedSplat() {\r\n"
        "    f32x3_i32 v = f32x3_i32(1.0f);\r\n"
        "}\r\n";
    CompileResult badMixedSplatRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadMixedSimdSplatSource, "impl/sim/bad_mixed_splat.adhd", arena.arena, badMixedSplatRes));

    static constexpr MStringView s_BadMixedSimdIndexSource =
        "f32 badMixedIndex(in f32x3_i32 v) {\r\n"
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
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

