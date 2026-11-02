// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_core_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::AdhdCompilerTestsDetail;
namespace Compiler = NWB::Core::Metascript::Adhd::Compiler;
using NWB::Core::Metascript::Adhd::CompileResult;
using NWB::Core::Metascript::MStringView;

TEST(AdhdPreprocessor, DeduplicatesRepeatedIncludes){
    AdhdTestArena arena;
    MockIncludeResolver resolver;

    static constexpr MStringView s_AdhdSource =
        "#include \"shared_defs.adhd\"\r\n"
        "#include \"shared_defs.adhd\"\r\n"
        "u32 compute(in u32 x) {\r\n"
        "    return x + kSharedLimit;\r\n"
        "}\r\n";

    CompileResult result(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_AdhdSource, "impl/math/compute.adhd", &resolver, arena.arena, result));
    EXPECT_EQ(resolver.resolveCallCount, 1u);
}

TEST(AdhdCompiler, ReordersClassFieldsByDescendingAlignmentAndSize){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "flag<u8> NodeFlags {\r\n"
        "    Active,\r\n"
        "    Visible,\r\n"
        "}\r\n"
        "class TransformNode {\r\n"
        "    public b8 dirty = false;\r\n"
        "    public u32 entityId = 0;\r\n"
        "    public f32x4 translation = f32x4(0.0f, 0.0f, 0.0f, 1.0f);\r\n"
        "    public u64 handle = 0;\r\n"
        "    public NodeFlags flags = NodeFlags::Active;\r\n"
        "    public u16 layer = 1;\r\n"
        "}\r\n";

    CompileResult result(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/scene/transform_node.adhd", arena.arena, result));
    ASSERT_EQ(result.unit.classes.size(), 1u);

    const auto* cls = result.unit.classes[0];
    ASSERT_EQ(cls->orderedFields.size(), 6u);
    EXPECT_EQ(cls->orderedFields[0]->name, "translation");
    EXPECT_EQ(cls->orderedFields[0]->resolvedAlign, 16u);
    EXPECT_EQ(cls->orderedFields[1]->name, "handle");
    EXPECT_EQ(cls->orderedFields[1]->resolvedAlign, 8u);
    EXPECT_EQ(cls->orderedFields[2]->name, "entityId");
    EXPECT_EQ(cls->orderedFields[2]->resolvedAlign, 4u);
    EXPECT_EQ(cls->orderedFields[3]->name, "layer");
    EXPECT_EQ(cls->orderedFields[3]->resolvedAlign, 2u);
    EXPECT_EQ(cls->orderedFields[4]->name, "dirty");
    EXPECT_EQ(cls->orderedFields[4]->resolvedAlign, 1u);
    EXPECT_EQ(cls->orderedFields[5]->name, "flags");
    EXPECT_EQ(cls->orderedFields[5]->resolvedAlign, 1u);
}

TEST(AdhdCompiler, SerializeClassPacksTightlyInSourceOrder){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "[serialize, asset(\"mesh_header\")]\r\n"
        "class BinaryHeader {\r\n"
        "    public u8 magic = 1;\r\n"
        "    public u32 version = 2;\r\n"
        "    public u16 flags = 3;\r\n"
        "}\r\n";

    CompileResult result(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/assets/binary_header.adhd", arena.arena, result));
    ASSERT_EQ(result.unit.classes.size(), 1u);

    const auto* cls = result.unit.classes[0];
    EXPECT_TRUE(cls->isSerialize);
    EXPECT_EQ(cls->totalAlign, 1u);
    EXPECT_EQ(cls->totalSize, 7u);
    ASSERT_EQ(cls->orderedFields.size(), 3u);
    EXPECT_EQ(cls->orderedFields[0]->name, "magic");
    EXPECT_EQ(cls->orderedFields[0]->resolvedOffset, 0u);
    EXPECT_EQ(cls->orderedFields[1]->name, "version");
    EXPECT_EQ(cls->orderedFields[1]->resolvedOffset, 1u);
    EXPECT_EQ(cls->orderedFields[2]->name, "flags");
    EXPECT_EQ(cls->orderedFields[2]->resolvedOffset, 5u);

    static constexpr MStringView s_SimdOrderSource =
        "[serialize]\r\n"
        "class SimdPayload {\r\n"
        "    public u32 header = 1u;\r\n"
        "    public Array<u32, 3> pad;\r\n"
        "    public f32x4 vector(0.0f, 0.0f, 0.0f, 1.0f);\r\n"
        "    public u32 trailer = 2u;\r\n"
        "}\r\n";

    CompileResult simdResult(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_SimdOrderSource, "impl/assets/simd_payload.adhd", arena.arena, simdResult));
    ASSERT_EQ(simdResult.unit.classes.size(), 1u);

    const auto* simdCls = simdResult.unit.classes[0];
    ASSERT_EQ(simdCls->orderedFields.size(), 4u);
    EXPECT_EQ(simdCls->orderedFields[0]->name, "header");
    EXPECT_EQ(simdCls->orderedFields[1]->name, "pad");
    EXPECT_EQ(simdCls->orderedFields[2]->name, "vector");
    EXPECT_EQ(simdCls->orderedFields[3]->name, "trailer");
}

TEST(AdhdCompiler, EnforcesInterfaceOverrideAndFinalClasses){
    AdhdTestArena arena;

    static constexpr MStringView s_MissingOverrideSource =
        "interface ITask {\r\n"
        "    void execute(in f32 dt);\r\n"
        "}\r\n"
        "class BadTask : ITask {\r\n"
        "    public void execute(in f32 dt) {\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult missingOverrideRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MissingOverrideSource, "impl/task/bad_task.adhd", arena.arena, missingOverrideRes));

    static constexpr MStringView s_ClassInheritanceSource =
        "class BaseClass {\r\n"
        "    public u32 x = 0;\r\n"
        "}\r\n"
        "class DerivedClass : BaseClass {\r\n"
        "    public u32 y = 1;\r\n"
        "}\r\n";

    CompileResult classInheritRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ClassInheritanceSource, "impl/task/derived.adhd", arena.arena, classInheritRes));
}

TEST(AdhdCompiler, EnforcesMandatoryInitializationAndProhibitions){
    AdhdTestArena arena;

    static constexpr MStringView s_UninitLocal =
        "void testFn(in u32 a) {\r\n"
        "    u32 count;\r\n"
        "}\r\n";
    CompileResult res1(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UninitLocal, "impl/test.adhd", arena.arena, res1));

    static constexpr MStringView s_DoubleInitCtor =
        "class DoubleInit {\r\n"
        "    private u32 value = 10;\r\n"
        "    public DoubleInit(in u32 v) {\r\n"
        "        this.value = v;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult res2(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DoubleInitCtor, "impl/test.adhd", arena.arena, res2));

    static constexpr MStringView s_BitfieldClass =
        "class BitfieldBad {\r\n"
        "    public u32 flag : 1;\r\n"
        "}\r\n";
    CompileResult res3(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BitfieldClass, "impl/test.adhd", arena.arena, res3));

    static constexpr MStringView s_DefaultArgFn =
        "void badFn(in u32 x = 5) {\r\n"
        "}\r\n";
    CompileResult res4(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DefaultArgFn, "impl/test.adhd", arena.arena, res4));

    static constexpr MStringView s_MoveCall =
        "void consume(in u32 x) {}\r\n"
        "void caller(in u32 a) {\r\n"
        "    u32 local = a;\r\n"
        "    consume(Move(local));\r\n"
        "}\r\n";
    CompileResult res5(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MoveCall, "impl/test.adhd", arena.arena, res5));

    static constexpr MStringView s_MutateInParam =
        "void badMutate(in u32 a) {\r\n"
        "    a = 10;\r\n"
        "}\r\n";
    CompileResult res6(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutateInParam, "impl/test.adhd", arena.arena, res6));
}

TEST(AdhdCompiler, ReordersTemplateInstantiationFieldsAndRejectsFreeOperatorsOrMissingMethodBodies){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "class Pair<T, U> {\r\n"
        "    public T first = 0;\r\n"
        "    public U second = f32x4(0.0f, 0.0f, 0.0f, 1.0f);\r\n"
        "}\r\n"
        "class Holder {\r\n"
        "    public Pair<u8, f32x4> packedPair = Pair<u8, f32x4>();\r\n"
        "}\r\n";

    CompileResult result(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/render/material_def.adhd", arena.arena, result));
    ASSERT_EQ(result.unit.templateInstantiations.size(), 1u);
    ASSERT_EQ(result.unit.templateInstantiations[0]->orderedFields.size(), 2u);
    EXPECT_EQ(result.unit.templateInstantiations[0]->orderedFields[0]->name, "second");
    EXPECT_EQ(result.unit.templateInstantiations[0]->orderedFields[1]->name, "first");

    static constexpr MStringView s_FreeOpSource =
        "u32 operator+(in u32 a, in u32 b) {\r\n"
        "    return a + b;\r\n"
        "}\r\n";
    CompileResult freeOpResult(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_FreeOpSource, "impl/render/bad_op.adhd", arena.arena, freeOpResult));

    static constexpr MStringView s_UnimplementedMethodSource =
        "class Incomplete {\r\n"
        "    public u32 x = 0;\r\n"
        "    public void missingBody(in u32 v);\r\n"
        "}\r\n";
    CompileResult unimplResult(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UnimplementedMethodSource, "impl/render/incomplete.adhd", arena.arena, unimplResult));
}

TEST(AdhdCompiler, EnforcesMutCallSiteAndParallelConcurrencySafety){
    AdhdTestArena arena;

    static constexpr MStringView s_MutLiteralCall =
        "void increment(mut u32 target) {\r\n"
        "    target += 1;\r\n"
        "}\r\n"
        "void caller(in u32 inputVal) {\r\n"
        "    increment(inputVal);\r\n"
        "}\r\n";
    CompileResult mutRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutLiteralCall, "impl/test_mut.adhd", arena.arena, mutRes));

    static constexpr MStringView s_UnsafeParallelVector =
        "void gatherBad(mut Vector<u32> outItems) {\r\n"
        "    parallel_for(u32 i = 0; i < 16; ++i) {\r\n"
        "        outItems.push_back(i);\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult unsafeParRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UnsafeParallelVector, "impl/test_par_bad.adhd", arena.arena, unsafeParRes));

    static constexpr MStringView s_SafeParallelVector =
        "void gatherGood(mut ParallelVector<u32> outItems, mut Vector<u32> indexedOut) {\r\n"
        "    parallel_for(u32 i = 0; i < 16; ++i) {\r\n"
        "        outItems.push_back(i);\r\n"
        "        indexedOut[i] = i;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult safeParRes(arena.arena);
    EXPECT_TRUE(Compiler::Compile(s_SafeParallelVector, "impl/test_par_good.adhd", arena.arena, safeParRes));
}

TEST(AdhdCompiler, RejectsFlagBitWidthOverflowInvalidSwitchCtorAndAttributeConflicts){
    AdhdTestArena arena;

    static constexpr MStringView s_OverflowFlagEnum =
        "flag<u8> TooManyFlags {\r\n"
        "    F0,\r\n"
        "    F1,\r\n"
        "    F2,\r\n"
        "    F3,\r\n"
        "    F4,\r\n"
        "    F5,\r\n"
        "    F6,\r\n"
        "    F7,\r\n"
        "    F8\r\n"
        "}\r\n";
    CompileResult overflowFlagRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_OverflowFlagEnum, "impl/bad_flag_overflow.adhd", arena.arena, overflowFlagRes));

    static constexpr MStringView s_BadFloatSwitch =
        "void badSwitch(in f32 v) {\r\n"
        "    switch(v) {\r\n"
        "    default:\r\n"
        "        break;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badSwitchRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadFloatSwitch, "impl/bad_float_switch.adhd", arena.arena, badSwitchRes));

    static constexpr MStringView s_DupCaseSwitch =
        "void dupSwitch(in i32 v) {\r\n"
        "    switch(v) {\r\n"
        "    case 1:\r\n"
        "        break;\r\n"
        "    case 1:\r\n"
        "        break;\r\n"
        "    default:\r\n"
        "        break;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult dupCaseRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DupCaseSwitch, "impl/bad_dup_case.adhd", arena.arena, dupCaseRes));

    static constexpr MStringView s_PartialCtorInit =
        "class PartialInit {\r\n"
        "    public i32 x;\r\n"
        "    public PartialInit(in b8 cond) {\r\n"
        "        if(cond) {\r\n"
        "            this.x = 1;\r\n"
        "        }\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult partialCtorRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_PartialCtorInit, "impl/bad_partial_ctor.adhd", arena.arena, partialCtorRes));

    static constexpr MStringView s_CallNonFuncField =
        "class NonCallableField {\r\n"
        "    public i32 value = 0;\r\n"
        "    public void run() {\r\n"
        "        this.value();\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult nonFuncFieldRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_CallNonFuncField, "impl/bad_call_field.adhd", arena.arena, nonFuncFieldRes));

    static constexpr MStringView s_ProhibitedInlineAttr =
        "[inline]\r\n"
        "void badInlineAttr() {}\r\n";
    CompileResult badInlineRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ProhibitedInlineAttr, "impl/bad_inline_attr.adhd", arena.arena, badInlineRes));

    static constexpr MStringView s_ProhibitedNoInlineAttr =
        "[noinline]\r\n"
        "void badNoInlineAttr() {}\r\n";
    CompileResult badNoInlineRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ProhibitedNoInlineAttr, "impl/bad_noinline_attr.adhd", arena.arena, badNoInlineRes));

    static constexpr MStringView s_ProhibitedInlineKeyword =
        "inline void badInlineKeyword() {}\r\n";
    CompileResult badInlineKwRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ProhibitedInlineKeyword, "impl/bad_inline_kw.adhd", arena.arena, badInlineKwRes));

    static constexpr MStringView s_ProhibitedNoInlineKeyword =
        "noinline void badNoInlineKeyword() {}\r\n";
    CompileResult badNoInlineKwRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ProhibitedNoInlineKeyword, "impl/bad_noinline_kw.adhd", arena.arena, badNoInlineKwRes));
}

TEST(AdhdCompiler, RejectsMalformedHexMacroParamsCastsDanglingRefsAndEarlyReturnCtors){
    AdhdTestArena arena;

    static constexpr MStringView s_BadHexLiteral =
        "const u32 kBad = 0x1G;\r\n";
    CompileResult badHexRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadHexLiteral, "impl/bad_hex.adhd", arena.arena, badHexRes));

    static constexpr MStringView s_DupMacroParam =
        "#define BAD_MACRO(x, x) (x)\r\n"
        "const i32 kVal = 1;\r\n";
    CompileResult dupMacroRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DupMacroParam, "impl/bad_macro_param.adhd", arena.arena, dupMacroRes));

    static constexpr MStringView s_UnaryCStyleCast =
        "i32 badCast() {\r\n"
        "    return (i32)-1;\r\n"
        "}\r\n";
    CompileResult unaryCastRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UnaryCStyleCast, "impl/bad_unary_cast.adhd", arena.arena, unaryCastRes));

    static constexpr MStringView s_BitCastSizeMismatch =
        "u64 badBitCast(in f32 x) {\r\n"
        "    return bit_cast<u64>(x);\r\n"
        "}\r\n";
    CompileResult bitCastRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BitCastSizeMismatch, "impl/bad_bit_cast.adhd", arena.arena, bitCastRes));

    static constexpr MStringView s_DanglingLocalReturn =
        "in i32 badReturnRef() {\r\n"
        "    i32 localVal = 7;\r\n"
        "    return localVal;\r\n"
        "}\r\n";
    CompileResult danglingRetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DanglingLocalReturn, "impl/bad_dangling_ret.adhd", arena.arena, danglingRetRes));

    static constexpr MStringView s_RvalueMutArg =
        "void mutateInt(mut i32 v) { v = v + 1; }\r\n"
        "void caller(in i32 a, in i32 b) {\r\n"
        "    mutateInt(a + b);\r\n"
        "}\r\n";
    CompileResult rvalueMutRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_RvalueMutArg, "impl/bad_rvalue_mut.adhd", arena.arena, rvalueMutRes));

    static constexpr MStringView s_EarlyReturnCtor =
        "class EarlyRetClass {\r\n"
        "    public i32 x;\r\n"
        "    public EarlyRetClass(in b8 skip) {\r\n"
        "        if(skip) {\r\n"
        "            return;\r\n"
        "        }\r\n"
        "        this.x = 5;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult earlyRetCtorRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_EarlyReturnCtor, "impl/bad_early_ret_ctor.adhd", arena.arena, earlyRetCtorRes));

    static constexpr MStringView s_ParallelForSet =
        "void badParSet(in Set<i32> items) {\r\n"
        "    parallel_for(in i32 x : items) {\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult parSetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ParallelForSet, "impl/bad_par_set.adhd", arena.arena, parSetRes));
}

TEST(AdhdCompiler, RejectsSurrogateEscapesMalformedAttributesDependentBoundsAndParallelLambdaMutation){
    AdhdTestArena arena;

    static constexpr MStringView s_BadSurrogateEscape =
        "const u32 kSurrogate = '\\uD800';\r\n";
    CompileResult badSurrogateRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadSurrogateEscape, "impl/bad_surrogate.adhd", arena.arena, badSurrogateRes));

    static constexpr MStringView s_BadEmptyAttrParens =
        "[reflect()]\r\n"
        "class BadAttr {\r\n"
        "    public i32 x = 0;\r\n"
        "}\r\n";
    CompileResult badAttrRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadEmptyAttrParens, "impl/bad_attr.adhd", arena.arena, badAttrRes));

    static constexpr MStringView s_BadDupConstMethod =
        "class DupConst {\r\n"
        "    public i32 get()const const { return 0; }\r\n"
        "}\r\n";
    CompileResult badDupConstRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadDupConstMethod, "impl/bad_dup_const.adhd", arena.arena, badDupConstRes));

    static constexpr MStringView s_BadDependentBound =
        "interface IBound {\r\n"
        "    void step();\r\n"
        "}\r\n"
        "class ReqBound<T : IBound> {\r\n"
        "    public T item;\r\n"
        "}\r\n"
        "class UnboundedWrapper<U> {\r\n"
        "    public ReqBound<U> inner;\r\n"
        "}\r\n";
    CompileResult badDepBoundRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadDependentBound, "impl/bad_dep_bound.adhd", arena.arena, badDepBoundRes));

    static constexpr MStringView s_BadInInterfaceMutCall =
        "interface IMutator {\r\n"
        "    void mutate();\r\n"
        "}\r\n"
        "void callInInterface(in IMutator m) {\r\n"
        "    m.mutate();\r\n"
        "}\r\n";
    CompileResult badInIfaceRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadInInterfaceMutCall, "impl/bad_in_iface.adhd", arena.arena, badInIfaceRes));

    static constexpr MStringView s_BadParallelContainerLambda =
        "void badParLambda() {\r\n"
        "    Vector<i32> items;\r\n"
        "    auto pushFn = lambda[mut items](in i32 v) -> void {\r\n"
        "        items.push_back(v);\r\n"
        "    };\r\n"
        "    parallel_for(u32 i = 0u; i < 4u; ++i) {\r\n"
        "        pushFn(cast<i32>(i));\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badParLamRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadParallelContainerLambda, "impl/bad_par_lam.adhd", arena.arena, badParLamRes));

    static constexpr MStringView s_BadBuiltinTypeDotMember =
        "u32 badLimit() { return u32.foo; }\r\n";
    CompileResult badBuiltinDotRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadBuiltinTypeDotMember, "impl/bad_builtin_dot.adhd", arena.arena, badBuiltinDotRes));

    static constexpr MStringView s_BadMutateScalarLimit =
        "void badMutLimit() { u32.max = 0u; }\r\n";
    CompileResult badMutLimitRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadMutateScalarLimit, "impl/bad_mut_limit.adhd", arena.arena, badMutLimitRes));

    static constexpr MStringView s_ScalarLimitsValid =
        "type ShapeId = u32;\r\n"
        "const ShapeId kInvalidShape = ShapeId.max;\r\n"
        "f32 lowestFloat() { return f32.min; }\r\n";
    CompileResult scalarLimitsRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_ScalarLimitsValid, "impl/scalar_limits.adhd", arena.arena, scalarLimitsRes));
    EXPECT_NE(MStringView(scalarLimitsRes.generatedHeader.data(), scalarLimitsRes.generatedHeader.size()).find("::Limit<::u32>::s_Max"), MStringView::npos);
    EXPECT_NE(MStringView(scalarLimitsRes.generatedHeader.data(), scalarLimitsRes.generatedHeader.size()).find("::Limit<::f32>::s_Min"), MStringView::npos);

    static constexpr MStringView s_SingleArgExpectedAndNumericBuiltins =
        "Expected<i64> parseTrimmedInt(in StringView text) {\r\n"
        "    const StringView trimmed = trimView(text);\r\n"
        "    if(trimmed.empty()) {\r\n"
        "        return makeUnexpected(Failure());\r\n"
        "    }\r\n"
        "    StringView number = trimmed;\r\n"
        "    if(trimmed.front() == '+') {\r\n"
        "        number = trimmed.substr(1u);\r\n"
        "    }\r\n"
        "    const Expected<i64> parsed = parseI64(number);\r\n"
        "    if(!parsed.has_value()) {\r\n"
        "        return makeUnexpected(parsed.error());\r\n"
        "    }\r\n"
        "    return parsed.value();\r\n"
        "}\r\n";
    CompileResult expectedBuiltinsRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_SingleArgExpectedAndNumericBuiltins, "impl/expected_builtins.adhd", arena.arena, expectedBuiltinsRes));
    EXPECT_NE(MStringView(expectedBuiltinsRes.generatedHeader.data(), expectedBuiltinsRes.generatedHeader.size()).find("Expected<i64> parseTrimmedInt("), MStringView::npos);
    EXPECT_NE(MStringView(expectedBuiltinsRes.generatedSource.data(), expectedBuiltinsRes.generatedSource.size()).find("::TrimView("), MStringView::npos);
    EXPECT_NE(MStringView(expectedBuiltinsRes.generatedSource.data(), expectedBuiltinsRes.generatedSource.size()).find("::MakeUnexpected("), MStringView::npos);
    EXPECT_NE(MStringView(expectedBuiltinsRes.generatedSource.data(), expectedBuiltinsRes.generatedSource.size()).find("::ParseI64("), MStringView::npos);

    static constexpr MStringView s_BadExpectedThreeArgs =
        "Expected<i32, Failure, u8> badExpected() { return 0; }\r\n";
    CompileResult badExpectedRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadExpectedThreeArgs, "impl/bad_expected_args.adhd", arena.arena, badExpectedRes));

    static constexpr MStringView s_NativeBlockParamNoShadowForwardDecl =
        "native{\r\n"
        "#include <impl/assets_ui_skin/asset.h>\r\n"
        "}\r\n"
        "b8 checkSkin(in UiSkin skin, in Name region) {\r\n"
        "    return skin.hasRegion(region);\r\n"
        "}\r\n";
    CompileResult nativeParamRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_NativeBlockParamNoShadowForwardDecl, "impl/ecs_ui/skin_check.adhd", arena.arena, nativeParamRes));
    EXPECT_EQ(MStringView(nativeParamRes.generatedHeader.data(), nativeParamRes.generatedHeader.size()).find("class UiSkin;"), MStringView::npos);

    static constexpr MStringView s_Utf8DecodeBuiltinsValid =
        "b8 validateUtf8(in StringView text, mut u32 outLastScalar) {\r\n"
        "    outLastScalar = 0u;\r\n"
        "    usize offset = 0u;\r\n"
        "    while(offset < text.size()) {\r\n"
        "        u32 codePoint = 0u;\r\n"
        "        usize byteCount = 0u;\r\n"
        "        if(!tryDecodeUtf8Scalar(text.substr(offset), codePoint, byteCount)) {\r\n"
        "            return false;\r\n"
        "        }\r\n"
        "        if(!tryDecodeUtf8CodePoint(text.substr(offset), outLastScalar, byteCount)) {\r\n"
        "            return false;\r\n"
        "        }\r\n"
        "        if(!isUtf8ScalarBoundary(text, offset)) {\r\n"
        "            return false;\r\n"
        "        }\r\n"
        "        offset += byteCount;\r\n"
        "    }\r\n"
        "    return isUtf8ScalarBoundary(text, offset);\r\n"
        "}\r\n";
    CompileResult utf8DecodeRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Utf8DecodeBuiltinsValid, "impl/utf8_decode.adhd", arena.arena, utf8DecodeRes));
    EXPECT_NE(MStringView(utf8DecodeRes.generatedSource.data(), utf8DecodeRes.generatedSource.size()).find("::TryDecodeUtf8Scalar("), MStringView::npos);
    EXPECT_NE(MStringView(utf8DecodeRes.generatedSource.data(), utf8DecodeRes.generatedSource.size()).find("::TryDecodeUtf8CodePoint("), MStringView::npos);
    EXPECT_NE(MStringView(utf8DecodeRes.generatedSource.data(), utf8DecodeRes.generatedSource.size()).find("::IsUtf8ScalarBoundary("), MStringView::npos);

    static constexpr MStringView s_BadUtf8DecodeConstOut =
        "b8 badUtf8(in StringView text) {\r\n"
        "    const u32 codePoint = 0u;\r\n"
        "    usize byteCount = 0u;\r\n"
        "    return tryDecodeUtf8Scalar(text, codePoint, byteCount);\r\n"
        "}\r\n";
    CompileResult badUtf8ConstRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadUtf8DecodeConstOut, "impl/bad_utf8_const.adhd", arena.arena, badUtf8ConstRes));

    static constexpr MStringView s_BadUtf8BoundaryArg =
        "b8 badUtf8Boundary(in StringView text) {\r\n"
        "    return isUtf8ScalarBoundary(text, 1.0f);\r\n"
        "}\r\n";
    CompileResult badUtf8BoundaryRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadUtf8BoundaryArg, "impl/bad_utf8_boundary.adhd", arena.arena, badUtf8BoundaryRes));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

