// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_reflection_nwb_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::AdhdCompilerTestsDetail;
using NWB::Core::Metascript::Adhd::Compiler;
using NWB::Core::Metascript::Adhd::CompileResult;
using NWB::Core::Metascript::MString;
using NWB::Core::Metascript::MStringView;
using NWB::Core::Metascript::MetaArena;

TEST(AdhdCompiler, PreservesNumericLiteralSuffixesUnderSingleLetterMacrosAndRejectsCyclesAndDuplicates){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "/*\r\n"
        "#include <nonexistent_cpp_header.h>\r\n"
        "#define UNCLOSED_MACRO(a, b\r\n"
        "native {\r\n"
        "*/\r\n"
        "#define f 999 /* trailing block comment on define */\r\n"
        "#define u 888\r\n"
        "#define x 777\r\n"
        "i32 computeLiteralWithMacros() {\r\n"
        "    f32 scale = 1.0f;\r\n"
        "    u32 mask = 0x10u + 10u;\r\n"
        "    return cast<i32>(scale) + cast<i32>(mask);\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "/abs/work/dual_accessor.adhd", arena.arena, res));

    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());
    EXPECT_NE(cppView.find("f32 scale = 1.0f;"), MStringView::npos);
    EXPECT_NE(cppView.find("u32 mask = (0x10u + 10u);"), MStringView::npos);

    const MString relNs = Compiler::DeriveNamespaceFromPath("impl/2d_scene/node.adhd", arena.arena);
    EXPECT_EQ(MStringView(relNs.data(), relNs.size()), "NWB::Impl::_2dScene");

    static constexpr MStringView s_DuplicateAttrSource =
        "[reflect, reflect]\r\n"
        "void dupAttr() {}\r\n";
    CompileResult dupAttrRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DuplicateAttrSource, "impl/sim/dup_attr.adhd", arena.arena, dupAttrRes));

    static constexpr MStringView s_CyclicInterfaceSource =
        "interface IAlpha : IBeta {\r\n"
        "    void step();\r\n"
        "}\r\n"
        "interface IBeta : IAlpha {\r\n"
        "    void tick();\r\n"
        "}\r\n";
    CompileResult cyclicIfaceRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_CyclicInterfaceSource, "impl/sim/cyclic_iface.adhd", arena.arena, cyclicIfaceRes));

    static constexpr MStringView s_DuplicateTemplateParamSource =
        "class BadPair<T, T> {\r\n"
        "    public T value;\r\n"
        "}\r\n";
    CompileResult dupTParamRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DuplicateTemplateParamSource, "impl/sim/dup_tparam.adhd", arena.arena, dupTParamRes));

    static constexpr MStringView s_DuplicateNativeBindingSource =
        "void badNative(in i32 a, mut i32 b) {\r\n"
        "    native(in a, mut a = b) {\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult dupNativeRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DuplicateNativeBindingSource, "impl/sim/dup_native.adhd", arena.arena, dupNativeRes));
}

TEST(AdhdCompiler, RejectsGlobalConstMutationMissingReturnsParallelIndexMutationAndNonEnumScopeAccess){
    AdhdTestArena arena;

    static constexpr MStringView s_MutateGlobalConstSource =
        "const u32 kImmutable = 10u;\r\n"
        "void badMutateGlobal() {\r\n"
        "    kImmutable = 20u;\r\n"
        "}\r\n";
    CompileResult mutGlobalRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutateGlobalConstSource, "impl/sim/bad_mut_global.adhd", arena.arena, mutGlobalRes));

    static constexpr MStringView s_MissingReturnSource =
        "i32 missingReturn(in i32 x) {\r\n"
        "    i32 y = x + 1;\r\n"
        "}\r\n";
    CompileResult missingRetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MissingReturnSource, "impl/sim/bad_missing_ret.adhd", arena.arena, missingRetRes));

    static constexpr MStringView s_MutateParallelForIndexSource =
        "void badParIndexMut() {\r\n"
        "    parallel_for (u32 i = 0u; i < 8u; ++i) {\r\n"
        "        i += 1u;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult parIdxMutRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutateParallelForIndexSource, "impl/sim/bad_par_idx_mut.adhd", arena.arena, parIdxMutRes));

    static constexpr MStringView s_InvalidNonEnumScopeAccessSource =
        "class Dummy {\r\n"
        "    public u32 x = 0u;\r\n"
        "}\r\n"
        "u32 badScope() {\r\n"
        "    return Dummy::x;\r\n"
        "}\r\n";
    CompileResult badScopeRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_InvalidNonEnumScopeAccessSource, "impl/sim/bad_non_enum_scope.adhd", arena.arena, badScopeRes));
}

TEST(AdhdCompiler, RejectsConstReferenceReturnViolationsAndInvalidContainerTemplateArity){
    AdhdTestArena arena;

    static constexpr MStringView s_ReturnInParamAsMutRefSource =
        "mut i32 badMutReturn(in i32 x) {\r\n"
        "    return x;\r\n"
        "}\r\n";
    CompileResult badMutRetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ReturnInParamAsMutRefSource, "impl/sim/bad_mut_ret.adhd", arena.arena, badMutRetRes));

    static constexpr MStringView s_MutateInReturnValueSource =
        "in i32 getConstRef(in i32 x) {\r\n"
        "    return x;\r\n"
        "}\r\n"
        "void badMutateConstRef(in i32 a) {\r\n"
        "    getConstRef(a) = 5;\r\n"
        "}\r\n";
    CompileResult badMutConstCallRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutateInReturnValueSource, "impl/sim/bad_mut_const_call.adhd", arena.arena, badMutConstCallRes));

    static constexpr MStringView s_InvalidVectorAritySource =
        "void badVecArity() {\r\n"
        "    Vector<i32, i32> v;\r\n"
        "}\r\n";
    CompileResult badVecArityRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_InvalidVectorAritySource, "impl/sim/bad_vec_arity.adhd", arena.arena, badVecArityRes));
}

TEST(AdhdCompiler, RejectsVoidVariablesByValueInterfaceContainersAndInvalidParallelWaits){
    AdhdTestArena arena;

    static constexpr MStringView s_VoidVariableSource =
        "void badVoidVar() {\r\n"
        "    void x = 0;\r\n"
        "}\r\n";
    CompileResult badVoidVarRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_VoidVariableSource, "impl/sim/bad_void_var.adhd", arena.arena, badVoidVarRes));

    static constexpr MStringView s_ByValueInterfaceTemplateArgSource =
        "interface IJob {\r\n"
        "    void tick();\r\n"
        "}\r\n"
        "void badIfaceVec() {\r\n"
        "    Vector<IJob> jobs;\r\n"
        "}\r\n";
    CompileResult badIfaceVecRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ByValueInterfaceTemplateArgSource, "impl/sim/bad_iface_vec.adhd", arena.arena, badIfaceVecRes));

    static constexpr MStringView s_WaitInsideParallelSource =
        "void badParallelWait() {\r\n"
        "    parallel TaskA {\r\n"
        "        wait();\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badParWaitRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_WaitInsideParallelSource, "impl/sim/bad_par_wait.adhd", arena.arena, badParWaitRes));

    static constexpr MStringView s_DuplicateWaitTargetSource =
        "void badDupWait() {\r\n"
        "    parallel TaskA {\r\n"
        "    }\r\n"
        "    wait(TaskA, TaskA);\r\n"
        "}\r\n";
    CompileResult badDupWaitRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DuplicateWaitTargetSource, "impl/sim/bad_dup_wait.adhd", arena.arena, badDupWaitRes));
}

TEST(AdhdCompiler, RejectsCyclicTypeAliasesAndRecursiveByValueClassMembers){
    AdhdTestArena arena;

    static constexpr MStringView s_CyclicTypeAliasSource =
        "type AliasA = AliasB;\r\n"
        "type AliasB = AliasA;\r\n";
    CompileResult cyclicAliasRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_CyclicTypeAliasSource, "impl/sim/cyclic_alias.adhd", arena.arena, cyclicAliasRes));

    static constexpr MStringView s_RecursiveClassValueMemberSource =
        "class BadNode {\r\n"
        "    public Optional<BadNode> next;\r\n"
        "}\r\n";
    CompileResult recClassRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_RecursiveClassValueMemberSource, "impl/sim/rec_class.adhd", arena.arena, recClassRes));
}

TEST(AdhdCompiler, RejectsMismatchedParameterizedInterfaceBoundsDoubleRefAliasesAndEscapingMutLambdas){
    AdhdTestArena arena;

    static constexpr MStringView s_WrongParameterizedBoundSource =
        "interface IStream<T> {\r\n"
        "    T read()const;\r\n"
        "}\r\n"
        "class IntStream : IStream<i32> {\r\n"
        "    public i32 read()const override { return 0; }\r\n"
        "}\r\n"
        "f32 consumeFloat<S : IStream<f32>>(in S s) {\r\n"
        "    return s.read();\r\n"
        "}\r\n"
        "f32 badCall(in IntStream is) {\r\n"
        "    return consumeFloat<IntStream>(is);\r\n"
        "}\r\n";
    CompileResult wrongBoundRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_WrongParameterizedBoundSource, "impl/sim/wrong_param_bound.adhd", arena.arena, wrongBoundRes));

    static constexpr MStringView s_DoubleRefAliasSource =
        "interface IFoo {\r\n"
        "    void tick();\r\n"
        "}\r\n"
        "type FooRef = in IFoo;\r\n"
        "void badDoubleRef(in FooRef f) {\r\n"
        "    mut FooRef local = f;\r\n"
        "}\r\n";
    CompileResult doubleRefRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DoubleRefAliasSource, "impl/sim/double_ref_alias.adhd", arena.arena, doubleRefRes));

    static constexpr MStringView s_EscapingMutLambdaSource =
        "Func<i32()> badReturnMutLambda() {\r\n"
        "    i32 counter = 0;\r\n"
        "    return lambda[mut counter]() -> i32 {\r\n"
        "        counter += 1;\r\n"
        "        return counter;\r\n"
        "    };\r\n"
        "}\r\n";
    CompileResult escapingLambdaRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_EscapingMutLambdaSource, "impl/sim/escaping_mut_lambda.adhd", arena.arena, escapingLambdaRes));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

