// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"

#include <core/metascript/adhd/ast/ast_common.h>
#include <core/metascript/adhd/ast/builtin_type_semantics.h>


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


TEST(AdhdCompiler, LowerBoundRejectsNonUsizeSpanArguments){
    AdhdTestArena arena;
    CompileResult valid(arena.arena);
    ASSERT_TRUE(Compiler::Compile("usize f(in Span<usize> b){ return lowerBoundIndex(b.subspan(0u, b.size()), 0u); }", "valid.adhd", arena.arena, valid));
    EXPECT_TRUE(MStringView(valid.generatedHeader).find("::LowerBoundIndex(") != MStringView::npos || MStringView(valid.generatedSource).find("::LowerBoundIndex(") != MStringView::npos);

    constexpr Array<MStringView, 2> s_InvalidSources{
        "usize f(){ return lowerBoundIndex(3u, 0u); }",
        "usize f(in Span<u32> b){ return lowerBoundIndex(b, 0u); }",
    };
    for(const MStringView source : s_InvalidSources){
        CompileResult invalid(arena.arena);
        EXPECT_FALSE(Compiler::Compile(source, "invalid.adhd", arena.arena, invalid));
        ASSERT_FALSE(invalid.errors.empty());
        EXPECT_EQ(MStringView(invalid.errors[0u].message), "built-in function argument must be Span<usize>");
    }
}


TEST(AdhdCompiler, ByteBuiltinsCheckContainerTypesAndAlignmentUsesSize){
    AdhdTestArena arena;
    CompileResult valid(arena.arena);
    constexpr MStringView s_Valid =
        "Expected<usize> align(in usize value){ return alignUpChecked(value, 4u); }\n"
        "void store(in f32 value, mut FixedVector<u8, 8u> bytes){ writeF32LittleEndianAt(value, 0u, bytes); }\n";
    ASSERT_TRUE(Compiler::Compile(s_Valid, "byte_builtins.adhd", arena.arena, valid));
    EXPECT_TRUE(MStringView(valid.generatedHeader).find("::AlignUpCheckedSize(") != MStringView::npos || MStringView(valid.generatedSource).find("::AlignUpCheckedSize(") != MStringView::npos);
    constexpr Array<MStringView, 3> s_InvalidSources{
        "u32 f(in Span<u32> b){ return readPartialU32LittleEndian(b, 0u); }",
        "void f(in f32 v, mut FixedVector<u32, 8u> b){ writeF32LittleEndianAt(v, 0u, b); }",
        "void f(in f32 v, in FixedVector<u8, 8u> b){ writeF32LittleEndianAt(v, 0u, b); }",
    };
    for(const MStringView source : s_InvalidSources){
        CompileResult invalid(arena.arena);
        EXPECT_FALSE(Compiler::Compile(source, "bad_byte_builtins.adhd", arena.arena, invalid));
    }
}


static_assert(NWB::Core::Metascript::Adhd::BuiltinTypeSemantics::FindBuiltinScalarType("u32")->byteSize == 4u);
static_assert(NWB::Core::Metascript::Adhd::BuiltinTypeSemantics::FindBuiltinScalarType("f64")->isFloat);
static_assert(NWB::Core::Metascript::Adhd::BuiltinTypeSemantics::FindBuiltinScalarType("unknown") == nullptr);


TEST(AdhdCompiler, NumericGrammarAndBoundBuiltinsCheckArguments){
    AdhdTestArena arena;
    CompileResult valid(arena.arena);
    constexpr MStringView s_Valid =
        "u8 integer(in StringView s){ return classifyIntegerGrammar(s); }\n"
        "b8 incomplete(in StringView s){ return classifyFloatGrammar(s) == s_NumericGrammarIncomplete; }\n"
        "f32 bound(in f64 v, in b8 upper){ return roundF64ToF32Bound(v, upper); }\n"
        "f64 step(in f64 v, in f64 lo, in f64 hi, in f64 delta, in b8 up){ return stepF64Bounded(v, lo, hi, delta, up); }\n"
        "void advance(mut u64 value){ advanceWrappingNonzeroU64(value); }\n"
        "u32 width(in u32 value){ return bitWidthLowU32(value, 8u); }\n";
    ASSERT_TRUE(Compiler::Compile(s_Valid, "numeric_builtins.adhd", arena.arena, valid));
    const MStringView source(valid.generatedSource);
    const MStringView header(valid.generatedHeader);
    EXPECT_NE(header.find("#include <global/binary_big_endian.h>"), MStringView::npos);
    EXPECT_NE(header.find("#include <global/binary_little_endian.h>"), MStringView::npos);
    EXPECT_NE(header.find("#include <global/numeric_range.h>"), MStringView::npos);
    for(const MStringView name : { MStringView("::ClassifyIntegerGrammar("), MStringView("::ClassifyFloatGrammar("), MStringView("::RoundF64ToF32Bound("), MStringView("::StepF64Bounded("), MStringView("::AdvanceWrappingNonzero("), MStringView("::BitWidthLowU32("), MStringView("::s_NumericGrammarIncomplete") })
        EXPECT_TRUE(source.find(name) != MStringView::npos || header.find(name) != MStringView::npos);
    constexpr Array<MStringView, 8> s_Invalid{
        "u8 f(in u32 value){ return classifyIntegerGrammar(value); }",
        "f32 f(in f32 value, in b8 upper){ return roundF64ToF32Bound(value, upper); }",
        "f32 f(in f64 value, in u32 upper){ return roundF64ToF32Bound(value, upper); }",
        "f64 f(in f64 v, in b8 up){ return stepF64Bounded(v, v, v, v); }",
        "f64 f(in f64 v, in u32 up){ return stepF64Bounded(v, v, v, v, up); }",
        "void f(in u64 v){ advanceWrappingNonzeroU64(v); }",
        "void f(mut u32 v){ advanceWrappingNonzeroU64(v); }",
        "u32 f(in u64 v){ return bitWidthLowU32(v, 8u); }",
    };
    for(const MStringView text : s_Invalid){
        CompileResult invalid(arena.arena);
        EXPECT_FALSE(Compiler::Compile(text, "bad_numeric_builtins.adhd", arena.arena, invalid));
    }
}


TEST(AdhdCompiler, RawSimdHashKeepsRegisterAndMutableSeed){
    AdhdTestArena arena;
    CompileResult valid(arena.arena);
    ASSERT_TRUE(Compiler::Compile("void f(in VectorSingle v, mut usize seed){ hashCombineF32x4(v, seed); } b8 eq(in VectorSingle a, in VectorSingle b){ return equalF32x4Bits(a, b); }", "hash.adhd", arena.arena, valid));
    const MStringView header(valid.generatedHeader);
    const MStringView source(valid.generatedSource);
    const MStringView generated = source.find("::HashCombineF32x4(") != MStringView::npos ? source : header;
    EXPECT_NE(generated.find("::HashCombineF32x4(v, seed)"), MStringView::npos);
    EXPECT_EQ(generated.find("::LoadFloat("), MStringView::npos);
    constexpr Array<MStringView, 3> s_InvalidSources{
        "void f(in VectorSingle v){ hashCombineF32x4(v, 1u); }",
        "void f(in VectorSingle v, in usize seed){ hashCombineF32x4(v, seed); }",
        "void f(in VectorDouble v, mut usize seed){ hashCombineF32x4(v, seed); }",
    };
    for(const MStringView text : s_InvalidSources){
        CompileResult invalid(arena.arena);
        EXPECT_FALSE(Compiler::Compile(text, "bad_hash.adhd", arena.arena, invalid));
    }
    CompileResult shadowed(arena.arena);
    ASSERT_TRUE(Compiler::Compile("void hashCombineF32x4(in u32 v, mut usize seed){ seed = cast<usize>(v); } void f(in u32 v, mut usize seed){ hashCombineF32x4(v, seed); }", "shadow_hash.adhd", arena.arena, shadowed));
}


TEST(AdhdCompiler, RectangleIntrinsicsKeepRegistersAndRejectWrongTypes){
    AdhdTestArena arena;
    constexpr MStringView s_Valid =
        "type RectVec = VectorSingle;\n"
        "VectorSingle combine(in RectVec a, in RectVec b){ return IntersectRectVec(IntersectRectBoundsVec(a, b), b); }\n"
        "b8 visible(in RectVec a, in RectVec b){ return HasVisibleRectIntersectionVec(a, b); }\n"
        "VectorSingle inset(in RectVec a, in RectVec b){ return InsetRectVec(a, b); }\n"
        "void take(in RectVec v){} void call(in RectVec a, in RectVec b){ take(IntersectRectVec(a, b)); }\n";
    CompileResult valid(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Valid, "rect.adhd", arena.arena, valid));
    const MStringView source(valid.generatedSource);
    const MStringView header(valid.generatedHeader);
    const MStringView generated = source.find("::IntersectRectValuePreservingExtent(") != MStringView::npos ? source : header;
    EXPECT_NE(generated.find("::IntersectRectBoundsValue("), MStringView::npos);
    EXPECT_NE(generated.find("::IntersectRectValuePreservingExtent("), MStringView::npos);
    EXPECT_EQ(generated.find("::LoadFloat("), MStringView::npos);
    EXPECT_TRUE(source.find("::HasVisibleRectIntersection(") != MStringView::npos || header.find("::HasVisibleRectIntersection(") != MStringView::npos);
    EXPECT_TRUE(source.find("::InsetRectValuePreservingExtent(") != MStringView::npos || header.find("::InsetRectValuePreservingExtent(") != MStringView::npos);

    constexpr Array<MStringView, 3> s_Invalid{
        "VectorSingle f(in VectorSingle a){ return IntersectRectVec(a, 1.0f); }",
        "VectorSingle f(in VectorDouble a, in VectorSingle b){ return IntersectRectVec(a, b); }",
        "VectorSingle f(in MatrixSingle a, in VectorSingle b){ return IntersectRectVec(a, b); }",
    };
    for(const MStringView text : s_Invalid){
        CompileResult invalid(arena.arena);
        EXPECT_FALSE(Compiler::Compile(text, "invalid_rect.adhd", arena.arena, invalid));
        ASSERT_FALSE(invalid.errors.empty());
        EXPECT_EQ(MStringView(invalid.errors[0u].message), "built-in function argument must be VectorSingle");
    }
    constexpr Array<MStringView, 2> s_MutationTargets{
        "void take(mut VectorSingle v){} void f(in VectorSingle a){ take(IntersectRectVec(a, a)); }",
        "void take(mut b8 v){} void f(in VectorSingle a){ take(HasVisibleRectIntersectionVec(a, a)); }",
    };
    for(const MStringView text : s_MutationTargets){
        CompileResult invalid(arena.arena);
        EXPECT_FALSE(Compiler::Compile(text, "mut_rect.adhd", arena.arena, invalid));
        ASSERT_FALSE(invalid.errors.empty());
        EXPECT_EQ(MStringView(invalid.errors[0u].message), "cannot mutate or pass temporary rvalue return value to a 'mut' target");
    }

    CompileResult shadowed(arena.arena);
    constexpr MStringView s_Shadow = "VectorSingle IntersectRectVec(in VectorSingle a, in VectorSingle b){ return a; } VectorSingle f(in VectorSingle a, in VectorSingle b){ return IntersectRectVec(a, b); }";
    ASSERT_TRUE(Compiler::Compile(s_Shadow, "shadow_rect.adhd", arena.arena, shadowed));
    EXPECT_EQ(MStringView(shadowed.generatedHeader).find("::IntersectRectValuePreservingExtent("), MStringView::npos);
    EXPECT_EQ(MStringView(shadowed.generatedSource).find("::IntersectRectValuePreservingExtent("), MStringView::npos);

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

