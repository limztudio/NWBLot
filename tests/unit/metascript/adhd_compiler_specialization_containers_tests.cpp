// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_specialization_containers_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::AdhdCompilerTestsDetail;
namespace Compiler = NWB::Core::Metascript::Adhd::Compiler;
using NWB::Core::Metascript::Adhd::CompileResult;
using NWB::Core::Metascript::MStringView;

TEST(AdhdCompiler, ExcludesUnannotatedPrivateMembersFromReflectionAndRejectsParallelNativeContainerMutation){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "[reflect]\r\n"
        "class JobNode {\r\n"
        "    public i32 visibleId = 1;\r\n"
        "    private i32 hiddenState = 2;\r\n"
        "    [reflect]\r\n"
        "    private i32 reflectedSecret = 3;\r\n"
        "    private void internalHelper() {}\r\n"
        "    [reflect]\r\n"
        "    private void reflectedHelper() {}\r\n"
        "    public void run() {}\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/job/job_node.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("reflectedFieldCount()noexcept{ return 2u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("reflectedMethodCount()noexcept{ return 2u; }"), MStringView::npos);
    EXPECT_EQ(hdrView.find("inMap.findField(\"hiddenState\")"), MStringView::npos);
    EXPECT_NE(hdrView.find("inMap.findField(\"reflectedSecret\")"), MStringView::npos);
    EXPECT_EQ(cppView.find("return \"hiddenState\";"), MStringView::npos);
    EXPECT_EQ(cppView.find("return \"internalHelper\";"), MStringView::npos);

    static constexpr MStringView s_UnsafeParallelNativeMutContainer =
        "type IntVec = Vector<i32>;\r\n"
        "void badParallelNative(mut IntVec items) {\r\n"
        "    parallel_for(u32 i = 0u; i < 8u; ++i) {\r\n"
        "        native(mut v = items, in i) {\r\n"
        "            v.push_back(static_cast<i32>(i));\r\n"
        "        }\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badParNativeRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UnsafeParallelNativeMutContainer, "impl/job/bad_par_native.adhd", arena.arena, badParNativeRes));
}

TEST(AdhdCompiler, RejectsUninitializedLocalReferencesAndUnknownLambdaCaptures){
    AdhdTestArena arena;

    static constexpr MStringView s_UninitRefLocalSource =
        "void badUninitRef() {\r\n"
        "    mut String s;\r\n"
        "}\r\n";
    CompileResult badRefRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UninitRefLocalSource, "impl/sim/bad_ref.adhd", arena.arena, badRefRes));

    static constexpr MStringView s_UnknownLambdaCaptureSource =
        "void badCapture() {\r\n"
        "    auto fn = lambda[in missingVar]() -> void {\r\n"
        "    };\r\n"
        "}\r\n";
    CompileResult badCapRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UnknownLambdaCaptureSource, "impl/sim/bad_cap.adhd", arena.arena, badCapRes));
}

TEST(AdhdCompiler, RejectsUncapturedOrConstThisLambdaMutationsDuplicateSwitchDefaultsAndBodylessOolMethods){
    AdhdTestArena arena;

    static constexpr MStringView s_UncapturedThisInLambdaSource =
        "class BadNode {\r\n"
        "    public i32 x = 1;\r\n"
        "    public i32 test()const {\r\n"
        "        auto fn = lambda() -> i32 {\r\n"
        "            return this.x;\r\n"
        "        };\r\n"
        "        return fn();\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badUncapturedThisRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UncapturedThisInLambdaSource, "impl/sim/bad_this_cap.adhd", arena.arena, badUncapturedThisRes));

    static constexpr MStringView s_MutateInThisLambdaSource =
        "class BadConstThis {\r\n"
        "    public i32 x = 1;\r\n"
        "    public void test() {\r\n"
        "        auto fn = lambda[in this]() -> void {\r\n"
        "            this.x = 2;\r\n"
        "        };\r\n"
        "        fn();\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badMutInThisRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutateInThisLambdaSource, "impl/sim/bad_in_this.adhd", arena.arena, badMutInThisRes));

    static constexpr MStringView s_DuplicateDefaultSwitchSource =
        "void badSwitch(in u32 x) {\r\n"
        "    switch (x) {\r\n"
        "    default:\r\n"
        "        break;\r\n"
        "    default:\r\n"
        "        break;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badDupDefaultRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DuplicateDefaultSwitchSource, "impl/sim/bad_dup_default.adhd", arena.arena, badDupDefaultRes));

    static constexpr MStringView s_BodylessOutOfLineSource =
        "class Box {\r\n"
        "    public void run();\r\n"
        "}\r\n"
        "void Box::run();\r\n";
    CompileResult badOolRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BodylessOutOfLineSource, "impl/sim/bad_ool.adhd", arena.arena, badOolRes));
}

TEST(AdhdCompiler, RejectsOutOfBoundsSwizzleAndWrongBuiltinArity){
    AdhdTestArena arena;

    static constexpr MStringView s_MixedSwizzleSource =
        "void badSwizzle() {\r\n"
        "    f32x3_i32 v(1.0f, 2.0f, 3.0f, 4);\r\n"
        "    f32x3_i32 s = v.xyzw;\r\n"
        "}\r\n";
    CompileResult badSwizzleRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MixedSwizzleSource, "impl/sim/bad_mixed_swizzle.adhd", arena.arena, badSwizzleRes));

    static constexpr MStringView s_WrongBuiltinAritySource =
        "f32 badDot(in VectorSingle a) {\r\n"
        "    return dot(a);\r\n"
        "}\r\n";
    CompileResult badBuiltinArityRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_WrongBuiltinAritySource, "impl/sim/bad_builtin_arity.adhd", arena.arena, badBuiltinArityRes));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

