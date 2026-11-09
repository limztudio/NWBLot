// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/compiler.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AdhdCompilerTestsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AdhdTestArenaTag{};
using AdhdTestArena = NWB::Tests::TestArena<AdhdTestArenaTag>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MockIncludeResolver final : public Core::Metascript::IIncludeResolver{
public:
    [[nodiscard]] virtual b8 resolveInclude(Core::Metascript::MStringView includePath, b8 isSystem, Core::Metascript::MetaArena& arena, Core::Metascript::MString& outResolvedKey, Core::Metascript::MString& outContent)override;


public:
    u32 resolveCallCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AdhdCompilerTestsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MockIncludeResolver::resolveInclude(const Core::Metascript::MStringView includePath, const b8, Core::Metascript::MetaArena& arena, Core::Metascript::MString& outResolvedKey, Core::Metascript::MString& outContent){
    ++resolveCallCount;
    outResolvedKey = Core::Metascript::MString(includePath.data(), includePath.size(), arena);
    if(includePath == "shared_service.adhd"){
        static constexpr Core::Metascript::MStringView s_ServiceHeader =
            "interface IStore<T> {\r\n"
            "    void put(in T value);\r\n"
            "}\r\n"
            "[reflect]\r\n"
            "class SharedService {\r\n"
            "    private i32 m_total = 0;\r\n"
            "    public i32 accumulate(in i32 delta);\r\n"
            "}\r\n";
        outContent = Core::Metascript::MString(s_ServiceHeader.data(), s_ServiceHeader.size(), arena);
        return true;
    }
    static constexpr Core::Metascript::MStringView s_HeaderCode = "const u32 kSharedLimit = 16;\r\n";
    outContent = Core::Metascript::MString(s_HeaderCode.data(), s_HeaderCode.size(), arena);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        "u32 Compute(in u32 x) {\r\n"
        "    return x + kSharedLimit;\r\n"
        "}\r\n";

    CompileResult result(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_AdhdSource, "impl/math/compute.adhd", &resolver, arena.arena, result));
    EXPECT_EQ(resolver.resolveCallCount, 1u);

    static constexpr MStringView s_NativeRawStringSource =
        "native{\r\n"
        "static constexpr const char* s_Shader = R\"SLANG(struct Out{\r\n"
        "#include \"inner.slangi\"\r\n"
        "#endif\r\n"
        "};\r\n"
        "};\r\n"
        ")SLANG\";\r\n"
        "#include <core/global.h>\r\n"
        "}\r\n"
        "u32 Ok(in u32 x){ return x; }\r\n";
    CompileResult rawStringResult(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_NativeRawStringSource, "impl/math/raw_string.adhd", &resolver, arena.arena, rawStringResult));
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
        "void TestFn(in u32 a) {\r\n"
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
        "void BadFn(in u32 x = 5) {\r\n"
        "}\r\n";
    CompileResult res4(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DefaultArgFn, "impl/test.adhd", arena.arena, res4));

    static constexpr MStringView s_MoveCall =
        "void Consume(in u32 x) {}\r\n"
        "void Caller(in u32 a) {\r\n"
        "    u32 local = a;\r\n"
        "    Consume(Move(local));\r\n"
        "}\r\n";
    CompileResult res5(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MoveCall, "impl/test.adhd", arena.arena, res5));

    static constexpr MStringView s_MutateInParam =
        "void BadMutate(in u32 a) {\r\n"
        "    a = 10;\r\n"
        "}\r\n";
    CompileResult res6(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutateInParam, "impl/test.adhd", arena.arena, res6));

    CompileResult badGlobalNameRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile("void lowerGlobal() {}\r\n", "impl/test.adhd", arena.arena, badGlobalNameRes));
    ASSERT_FALSE(badGlobalNameRes.errors.empty());
    EXPECT_EQ(MStringView(badGlobalNameRes.errors[0u].message), "global function name must start with an uppercase letter");

    CompileResult badClassMethodNameRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile("class BadMethodCase { public void UpperMethod() {} }\r\n", "impl/test.adhd", arena.arena, badClassMethodNameRes));
    ASSERT_FALSE(badClassMethodNameRes.errors.empty());
    EXPECT_EQ(MStringView(badClassMethodNameRes.errors[0u].message), "member function name must start with a lowercase letter");

    CompileResult badIfaceMethodNameRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile("interface IBadCase { void UpperMethod(); }\r\n", "impl/test.adhd", arena.arena, badIfaceMethodNameRes));
    ASSERT_FALSE(badIfaceMethodNameRes.errors.empty());
    EXPECT_EQ(MStringView(badIfaceMethodNameRes.errors[0u].message), "member function name must start with a lowercase letter");
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
        "void Increment(mut u32 target) {\r\n"
        "    target += 1;\r\n"
        "}\r\n"
        "void Caller(in u32 inputVal) {\r\n"
        "    Increment(inputVal);\r\n"
        "}\r\n";
    CompileResult mutRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutLiteralCall, "impl/test_mut.adhd", arena.arena, mutRes));

    static constexpr MStringView s_UnsafeParallelVector =
        "void GatherBad(mut Vector<u32> outItems) {\r\n"
        "    parallel_for(u32 i = 0; i < 16; ++i) {\r\n"
        "        outItems.push_back(i);\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult unsafeParRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UnsafeParallelVector, "impl/test_par_bad.adhd", arena.arena, unsafeParRes));

    static constexpr MStringView s_SafeParallelVector =
        "void GatherGood(mut ParallelVector<u32> outItems, mut Vector<u32> indexedOut) {\r\n"
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
        "void BadSwitch(in f32 v) {\r\n"
        "    switch(v) {\r\n"
        "    default:\r\n"
        "        break;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badSwitchRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadFloatSwitch, "impl/bad_float_switch.adhd", arena.arena, badSwitchRes));

    static constexpr MStringView s_DupCaseSwitch =
        "void DupSwitch(in i32 v) {\r\n"
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
        "void BadInlineAttr() {}\r\n";
    CompileResult badInlineRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ProhibitedInlineAttr, "impl/bad_inline_attr.adhd", arena.arena, badInlineRes));

    static constexpr MStringView s_ProhibitedNoInlineAttr =
        "[noinline]\r\n"
        "void BadNoInlineAttr() {}\r\n";
    CompileResult badNoInlineRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ProhibitedNoInlineAttr, "impl/bad_noinline_attr.adhd", arena.arena, badNoInlineRes));

    static constexpr MStringView s_ProhibitedInlineKeyword =
        "inline void BadInlineKeyword() {}\r\n";
    CompileResult badInlineKwRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ProhibitedInlineKeyword, "impl/bad_inline_kw.adhd", arena.arena, badInlineKwRes));

    static constexpr MStringView s_ProhibitedNoInlineKeyword =
        "noinline void BadNoInlineKeyword() {}\r\n";
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
        "i32 BadCast() {\r\n"
        "    return (i32)-1;\r\n"
        "}\r\n";
    CompileResult unaryCastRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UnaryCStyleCast, "impl/bad_unary_cast.adhd", arena.arena, unaryCastRes));

    static constexpr MStringView s_BitCastSizeMismatch =
        "u64 BadBitCast(in f32 x) {\r\n"
        "    return bit_cast<u64>(x);\r\n"
        "}\r\n";
    CompileResult bitCastRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BitCastSizeMismatch, "impl/bad_bit_cast.adhd", arena.arena, bitCastRes));

    static constexpr MStringView s_DanglingLocalReturn =
        "in i32 BadReturnRef() {\r\n"
        "    i32 localVal = 7;\r\n"
        "    return localVal;\r\n"
        "}\r\n";
    CompileResult danglingRetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DanglingLocalReturn, "impl/bad_dangling_ret.adhd", arena.arena, danglingRetRes));

    static constexpr MStringView s_RvalueMutArg =
        "void MutateInt(mut i32 v) { v = v + 1; }\r\n"
        "void Caller(in i32 a, in i32 b) {\r\n"
        "    MutateInt(a + b);\r\n"
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
        "void BadParSet(in Set<i32> items) {\r\n"
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
        "void CallInInterface(in IMutator m) {\r\n"
        "    m.mutate();\r\n"
        "}\r\n";
    CompileResult badInIfaceRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadInInterfaceMutCall, "impl/bad_in_iface.adhd", arena.arena, badInIfaceRes));

    static constexpr MStringView s_BadParallelContainerLambda =
        "void BadParLambda() {\r\n"
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
        "u32 BadLimit() { return u32.foo; }\r\n";
    CompileResult badBuiltinDotRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadBuiltinTypeDotMember, "impl/bad_builtin_dot.adhd", arena.arena, badBuiltinDotRes));

    static constexpr MStringView s_BadMutateScalarLimit =
        "void BadMutLimit() { u32.max = 0u; }\r\n";
    CompileResult badMutLimitRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadMutateScalarLimit, "impl/bad_mut_limit.adhd", arena.arena, badMutLimitRes));

    static constexpr MStringView s_ScalarLimitsValid =
        "type ShapeId = u32;\r\n"
        "const ShapeId kInvalidShape = ShapeId.max;\r\n"
        "f32 LowestFloat() { return f32.min; }\r\n";
    CompileResult scalarLimitsRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_ScalarLimitsValid, "impl/scalar_limits.adhd", arena.arena, scalarLimitsRes));
    EXPECT_NE(MStringView(scalarLimitsRes.generatedHeader.data(), scalarLimitsRes.generatedHeader.size()).find("::Limit<::u32>::s_Max"), MStringView::npos);
    EXPECT_NE(MStringView(scalarLimitsRes.generatedHeader.data(), scalarLimitsRes.generatedHeader.size()).find("::Limit<::f32>::s_Min"), MStringView::npos);

    static constexpr MStringView s_SingleArgExpectedAndNumericBuiltins =
        "Expected<i64> ParseTrimmedInt(in StringView text) {\r\n"
        "    const StringView trimmed = TrimView(text);\r\n"
        "    if(trimmed.empty()) {\r\n"
        "        return MakeUnexpected(Failure());\r\n"
        "    }\r\n"
        "    StringView number = trimmed;\r\n"
        "    if(trimmed.front() == '+') {\r\n"
        "        number = trimmed.substr(1u);\r\n"
        "    }\r\n"
        "    const Expected<i64> parsed = ParseI64(number);\r\n"
        "    if(!parsed.has_value()) {\r\n"
        "        return MakeUnexpected(parsed.error());\r\n"
        "    }\r\n"
        "    return parsed.value();\r\n"
        "}\r\n";
    CompileResult expectedBuiltinsRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_SingleArgExpectedAndNumericBuiltins, "impl/expected_builtins.adhd", arena.arena, expectedBuiltinsRes));
    EXPECT_NE(MStringView(expectedBuiltinsRes.generatedHeader.data(), expectedBuiltinsRes.generatedHeader.size()).find("Expected<i64> ParseTrimmedInt("), MStringView::npos);
    EXPECT_NE(MStringView(expectedBuiltinsRes.generatedSource.data(), expectedBuiltinsRes.generatedSource.size()).find("::TrimView("), MStringView::npos);
    EXPECT_NE(MStringView(expectedBuiltinsRes.generatedSource.data(), expectedBuiltinsRes.generatedSource.size()).find("::MakeUnexpected("), MStringView::npos);
    EXPECT_NE(MStringView(expectedBuiltinsRes.generatedSource.data(), expectedBuiltinsRes.generatedSource.size()).find("::ParseI64("), MStringView::npos);

    static constexpr MStringView s_BadExpectedThreeArgs =
        "Expected<i32, Failure, u8> BadExpected() { return 0; }\r\n";
    CompileResult badExpectedRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadExpectedThreeArgs, "impl/bad_expected_args.adhd", arena.arena, badExpectedRes));

    static constexpr MStringView s_NativeBlockParamNoShadowForwardDecl =
        "native{\r\n"
        "#include <impl/assets_ui_skin/asset.h>\r\n"
        "}\r\n"
        "b8 CheckSkin(in UiSkin skin, in Name region) {\r\n"
        "    return skin.hasRegion(region);\r\n"
        "}\r\n";
    CompileResult nativeParamRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_NativeBlockParamNoShadowForwardDecl, "impl/ecs_ui/skin_check.adhd", arena.arena, nativeParamRes));
    EXPECT_EQ(MStringView(nativeParamRes.generatedHeader.data(), nativeParamRes.generatedHeader.size()).find("class UiSkin;"), MStringView::npos);

    static constexpr MStringView s_Utf8DecodeBuiltinsValid =
        "b8 ValidateUtf8(in StringView text, mut u32 outLastScalar) {\r\n"
        "    outLastScalar = 0u;\r\n"
        "    usize offset = 0u;\r\n"
        "    while(offset < text.size()) {\r\n"
        "        u32 codePoint = 0u;\r\n"
        "        usize byteCount = 0u;\r\n"
        "        if(!TryDecodeUtf8Scalar(text.substr(offset), codePoint, byteCount)) {\r\n"
        "            return false;\r\n"
        "        }\r\n"
        "        if(!TryDecodeUtf8CodePoint(text.substr(offset), outLastScalar, byteCount)) {\r\n"
        "            return false;\r\n"
        "        }\r\n"
        "        if(!IsUtf8ScalarBoundary(text, offset)) {\r\n"
        "            return false;\r\n"
        "        }\r\n"
        "        offset += byteCount;\r\n"
        "    }\r\n"
        "    return IsUtf8ScalarBoundary(text, offset);\r\n"
        "}\r\n";
    CompileResult utf8DecodeRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Utf8DecodeBuiltinsValid, "impl/utf8_decode.adhd", arena.arena, utf8DecodeRes));
    EXPECT_NE(MStringView(utf8DecodeRes.generatedSource.data(), utf8DecodeRes.generatedSource.size()).find("::TryDecodeUtf8Scalar("), MStringView::npos);
    EXPECT_NE(MStringView(utf8DecodeRes.generatedSource.data(), utf8DecodeRes.generatedSource.size()).find("::TryDecodeUtf8CodePoint("), MStringView::npos);
    EXPECT_NE(MStringView(utf8DecodeRes.generatedSource.data(), utf8DecodeRes.generatedSource.size()).find("::IsUtf8ScalarBoundary("), MStringView::npos);

    static constexpr MStringView s_BadUtf8DecodeConstOut =
        "b8 BadUtf8(in StringView text) {\r\n"
        "    const u32 codePoint = 0u;\r\n"
        "    usize byteCount = 0u;\r\n"
        "    return TryDecodeUtf8Scalar(text, codePoint, byteCount);\r\n"
        "}\r\n";
    CompileResult badUtf8ConstRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadUtf8DecodeConstOut, "impl/bad_utf8_const.adhd", arena.arena, badUtf8ConstRes));

    static constexpr MStringView s_BadUtf8BoundaryArg =
        "b8 BadUtf8Boundary(in StringView text) {\r\n"
        "    return IsUtf8ScalarBoundary(text, 1.0f);\r\n"
        "}\r\n";
    CompileResult badUtf8BoundaryRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadUtf8BoundaryArg, "impl/bad_utf8_boundary.adhd", arena.arena, badUtf8BoundaryRes));

    static constexpr MStringView s_BadTempSubstrViewVar =
        "usize BadTempSubstr(in String text) {\r\n"
        "    StringView view = TrimView(text.substr(1u));\r\n"
        "    return view.size();\r\n"
        "}\r\n";
    CompileResult badTempSubstrRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadTempSubstrViewVar, "impl/bad_temp_substr.adhd", arena.arena, badTempSubstrRes));

    static constexpr MStringView s_BadTempMemberViewVar =
        "class TempOwner { public CompactString label = CompactString(\"hi\"); }\r\n"
        "TempOwner MakeOwner() { return TempOwner(); }\r\n"
        "usize BadTempMemberView() {\r\n"
        "    auto view = TrimLeftView(MakeOwner().label.view());\r\n"
        "    return view.size();\r\n"
        "}\r\n";
    CompileResult badTempMemberRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadTempMemberViewVar, "impl/bad_temp_member.adhd", arena.arena, badTempMemberRes));

    static constexpr MStringView s_BadReturnLocalViewAlias =
        "StringView BadReturnAlias() {\r\n"
        "    CompactString local = CompactString(\"  scoped  \");\r\n"
        "    StringView trimmed = TrimView(local);\r\n"
        "    return trimmed.substr(1u);\r\n"
        "}\r\n";
    CompileResult badRetAliasRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadReturnLocalViewAlias, "impl/bad_ret_alias.adhd", arena.arena, badRetAliasRes));

    static constexpr MStringView s_BadShorterBlockViewAssign =
        "usize BadOuterAssign(in StringView fallback) {\r\n"
        "    StringView outer = fallback;\r\n"
        "    {\r\n"
        "        String inner = \"block\";\r\n"
        "        outer = TrimView(inner);\r\n"
        "    }\r\n"
        "    return outer.size();\r\n"
        "}\r\n";
    CompileResult badOuterAssignRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadShorterBlockViewAssign, "impl/bad_outer_assign.adhd", arena.arena, badOuterAssignRes));

    static constexpr MStringView s_BadMutateStringViewFront =
        "void BadMutateView(in StringView v) {\r\n"
        "    v.front() = 65u;\r\n"
        "}\r\n";
    CompileResult badMutViewRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadMutateStringViewFront, "impl/bad_mut_view.adhd", arena.arena, badMutViewRes));
}


TEST(AdhdCompiler, LiteralOnlyUnitsAvoidEngineDependenciesAndComplexUnitsKeepThem){
    AdhdTestArena arena;
    constexpr MStringView s_NameOnly = "const Name s_First = \"first\";\r\nconst Name s_Second = \"second\";\r\n";
    CompileResult names(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_NameOnly, "core/input/names.adhd", arena.arena, names));
    const MStringView namesHeader(names.generatedHeader.data(), names.generatedHeader.size());
    const MStringView namesSource(names.generatedSource.data(), names.generatedSource.size());
    EXPECT_NE(namesHeader.find("#include <global/name.h>"), MStringView::npos);
    EXPECT_EQ(namesHeader.find("#include <core/"), MStringView::npos);
    EXPECT_EQ(namesSource.find("#include <core/"), MStringView::npos);

    constexpr MStringView s_Scalars = "const u8 s_Byte = 255u;\r\nconst i32 s_Negative = -17;\r\nconst f32 s_Float = -1.25f;\r\nconst b8 s_Flag = true;\r\nconst u64 s_Size = 64u * 1024u * 1024u;\r\nconst u32 s_Base = 8u;\r\nconst u32 s_Mask = s_Base - 1u;\r\nconst StringView s_Text = \"a\\tb\";\r\n";
    CompileResult scalars(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Scalars, "core/input/scalars.adhd", arena.arena, scalars));
    const MStringView scalarHeader(scalars.generatedHeader.data(), scalars.generatedHeader.size());
    const MStringView scalarSource(scalars.generatedSource.data(), scalars.generatedSource.size());
    EXPECT_NE(scalarHeader.find("#include <global/type.h>"), MStringView::npos);
    EXPECT_EQ(scalarHeader.find("#include <core/"), MStringView::npos);
    EXPECT_EQ(scalarSource.find("#include <core/"), MStringView::npos);

    constexpr MStringView s_Limit = "const u32 s_Limit = u32.max;\r\n";
    CompileResult limit(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Limit, "core/input/limit.adhd", arena.arena, limit));
    const MStringView limitHeader(limit.generatedHeader.data(), limit.generatedHeader.size());
    const MStringView limitSource(limit.generatedSource.data(), limit.generatedSource.size());
    EXPECT_NE(limitHeader.find("#include <core/assets/ref.h>"), MStringView::npos);
    EXPECT_NE(limitSource.find("#include <core/task/cpu/scheduler.h>"), MStringView::npos);

    constexpr MStringView s_Cast = "const u32 s_Cast = cast<u32>(1u);\r\n";
    CompileResult cast(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Cast, "core/input/cast.adhd", arena.arena, cast));
    const MStringView castHeader(cast.generatedHeader.data(), cast.generatedHeader.size());
    EXPECT_NE(castHeader.find("#include <core/assets/ref.h>"), MStringView::npos);

    constexpr MStringView s_NameAndScalar = "const Name s_First = \"first\";\r\nconst u32 s_Value = 1u;\r\n";
    CompileResult nameAndScalar(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_NameAndScalar, "core/input/name_and_scalar.adhd", arena.arena, nameAndScalar));
    const MStringView nameAndScalarHeader(nameAndScalar.generatedHeader.data(), nameAndScalar.generatedHeader.size());
    EXPECT_NE(nameAndScalarHeader.find("#include <core/assets/ref.h>"), MStringView::npos);

    constexpr MStringView s_Mixed = "const Name s_First = \"first\";\r\nu32 Value(){ return 1u; }\r\n";
    CompileResult mixed(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Mixed, "core/input/mixed.adhd", arena.arena, mixed));
    const MStringView mixedHeader(mixed.generatedHeader.data(), mixed.generatedHeader.size());
    const MStringView mixedSource(mixed.generatedSource.data(), mixed.generatedSource.size());
    EXPECT_NE(mixedHeader.find("#include <core/assets/ref.h>"), MStringView::npos);
    EXPECT_NE(mixedSource.find("#include <core/task/cpu/scheduler.h>"), MStringView::npos);

    constexpr MStringView s_Constructed = "const Name s_First = Name(\"first\");\r\n";
    CompileResult constructed(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Constructed, "core/input/constructed.adhd", arena.arena, constructed));
    const MStringView constructedHeader(constructed.generatedHeader.data(), constructed.generatedHeader.size());
    EXPECT_NE(constructedHeader.find("#include <core/assets/ref.h>"), MStringView::npos);
}


TEST(AdhdCompiler, TerminateInvariantBuiltinLowersAndRejectsArguments){
    AdhdTestArena arena;
    constexpr MStringView s_TerminateInvariant =
        "void CheckInvariant(in b8 ok){\r\n"
        "    if(!ok)\r\n"
        "        TerminateInvariant();\r\n"
        "}\r\n";
    CompileResult result(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_TerminateInvariant, "impl/invariant.adhd", arena.arena, result));
    const MStringView header(result.generatedHeader.data(), result.generatedHeader.size());
    const MStringView source(result.generatedSource.data(), result.generatedSource.size());
    EXPECT_NE(header.find("#include <global/termination.h>"), MStringView::npos);
    EXPECT_NE(source.find("::TerminateInvariant()"), MStringView::npos);

    constexpr MStringView s_BadTerminateInvariantArg =
        "void BadInvariant(){\r\n"
        "    TerminateInvariant(1u);\r\n"
        "}\r\n";
    CompileResult badResult(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadTerminateInvariantArg, "impl/bad_invariant.adhd", arena.arena, badResult));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_reflection_nwb_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::AdhdCompilerTestsDetail;
namespace Compiler = NWB::Core::Metascript::Adhd::Compiler;
using NWB::Core::Metascript::Adhd::CompileResult;
using NWB::Core::Metascript::MString;
using NWB::Core::Metascript::MStringView;

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
        "i32 ComputeLiteralWithMacros() {\r\n"
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
        "void DupAttr() {}\r\n";
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
        "void BadNative(in i32 a, mut i32 b) {\r\n"
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
        "void BadMutateGlobal() {\r\n"
        "    kImmutable = 20u;\r\n"
        "}\r\n";
    CompileResult mutGlobalRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutateGlobalConstSource, "impl/sim/bad_mut_global.adhd", arena.arena, mutGlobalRes));

    static constexpr MStringView s_MissingReturnSource =
        "i32 MissingReturn(in i32 x) {\r\n"
        "    i32 y = x + 1;\r\n"
        "}\r\n";
    CompileResult missingRetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MissingReturnSource, "impl/sim/bad_missing_ret.adhd", arena.arena, missingRetRes));

    static constexpr MStringView s_MutateParallelForIndexSource =
        "void BadParIndexMut() {\r\n"
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
        "u32 BadScope() {\r\n"
        "    return Dummy::x;\r\n"
        "}\r\n";
    CompileResult badScopeRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_InvalidNonEnumScopeAccessSource, "impl/sim/bad_non_enum_scope.adhd", arena.arena, badScopeRes));
}

TEST(AdhdCompiler, RejectsConstReferenceReturnViolationsAndInvalidContainerTemplateArity){
    AdhdTestArena arena;

    static constexpr MStringView s_ReturnInParamAsMutRefSource =
        "mut i32 BadMutReturn(in i32 x) {\r\n"
        "    return x;\r\n"
        "}\r\n";
    CompileResult badMutRetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ReturnInParamAsMutRefSource, "impl/sim/bad_mut_ret.adhd", arena.arena, badMutRetRes));

    static constexpr MStringView s_MutateInReturnValueSource =
        "in i32 GetConstRef(in i32 x) {\r\n"
        "    return x;\r\n"
        "}\r\n"
        "void BadMutateConstRef(in i32 a) {\r\n"
        "    GetConstRef(a) = 5;\r\n"
        "}\r\n";
    CompileResult badMutConstCallRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutateInReturnValueSource, "impl/sim/bad_mut_const_call.adhd", arena.arena, badMutConstCallRes));

    static constexpr MStringView s_InvalidVectorAritySource =
        "void BadVecArity() {\r\n"
        "    Vector<i32, i32> v;\r\n"
        "}\r\n";
    CompileResult badVecArityRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_InvalidVectorAritySource, "impl/sim/bad_vec_arity.adhd", arena.arena, badVecArityRes));
}

TEST(AdhdCompiler, RejectsVoidVariablesByValueInterfaceContainersAndInvalidParallelWaits){
    AdhdTestArena arena;

    static constexpr MStringView s_VoidVariableSource =
        "void BadVoidVar() {\r\n"
        "    void x = 0;\r\n"
        "}\r\n";
    CompileResult badVoidVarRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_VoidVariableSource, "impl/sim/bad_void_var.adhd", arena.arena, badVoidVarRes));

    static constexpr MStringView s_ByValueInterfaceTemplateArgSource =
        "interface IJob {\r\n"
        "    void tick();\r\n"
        "}\r\n"
        "void BadIfaceVec() {\r\n"
        "    Vector<IJob> jobs;\r\n"
        "}\r\n";
    CompileResult badIfaceVecRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ByValueInterfaceTemplateArgSource, "impl/sim/bad_iface_vec.adhd", arena.arena, badIfaceVecRes));

    static constexpr MStringView s_WaitInsideParallelSource =
        "void BadParallelWait() {\r\n"
        "    parallel TaskA {\r\n"
        "        wait();\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badParWaitRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_WaitInsideParallelSource, "impl/sim/bad_par_wait.adhd", arena.arena, badParWaitRes));

    static constexpr MStringView s_DuplicateWaitTargetSource =
        "void BadDupWait() {\r\n"
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
        "f32 ConsumeFloat<S : IStream<f32>>(in S s) {\r\n"
        "    return s.read();\r\n"
        "}\r\n"
        "f32 BadCall(in IntStream is) {\r\n"
        "    return ConsumeFloat<IntStream>(is);\r\n"
        "}\r\n";
    CompileResult wrongBoundRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_WrongParameterizedBoundSource, "impl/sim/wrong_param_bound.adhd", arena.arena, wrongBoundRes));

    static constexpr MStringView s_DoubleRefAliasSource =
        "interface IFoo {\r\n"
        "    void tick();\r\n"
        "}\r\n"
        "type FooRef = in IFoo;\r\n"
        "void BadDoubleRef(in FooRef f) {\r\n"
        "    mut FooRef local = f;\r\n"
        "}\r\n";
    CompileResult doubleRefRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DoubleRefAliasSource, "impl/sim/double_ref_alias.adhd", arena.arena, doubleRefRes));

    static constexpr MStringView s_EscapingMutLambdaSource =
        "Func<i32()> BadReturnMutLambda() {\r\n"
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_sema_validation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::AdhdCompilerTestsDetail;
namespace Compiler = NWB::Core::Metascript::Adhd::Compiler;
using NWB::Core::Metascript::Adhd::CompileResult;
using NWB::Core::Metascript::MStringView;

TEST(AdhdCompiler, TopologicallyOrdersOutOfOrderDeclarationsAndRejectsAccessViolations){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "#define VER_GE(maj, min) (((maj) * 10 + (min)) >= 21)\r\n"
        "#if VER_GE(2, 1)\r\n"
        "type AliasB = AliasA;\r\n"
        "type AliasA = i32;\r\n"
        "#else\r\n"
        "type AliasB = i64;\r\n"
        "#endif\r\n"
        "\r\n"
        "interface IDerivedNode : IBaseNode {\r\n"
        "    i32 derivedId()const;\r\n"
        "}\r\n"
        "interface IBaseNode {\r\n"
        "    i32 baseId()const;\r\n"
        "}\r\n"
        "\r\n"
        "class ConcreteNode : IDerivedNode {\r\n"
        "    private i32 secret = 42;\r\n"
        "    public i32 baseId()const override { return this.secret; }\r\n"
        "    public i32 derivedId()const override { return this.secret + 1; }\r\n"
        "}\r\n"
        "\r\n"
        "class GrandOuter {\r\n"
        "    public u8 tag = 1;\r\n"
        "    public MidOuter mid = MidOuter();\r\n"
        "}\r\n"
        "class MidOuter {\r\n"
        "    public u8 flag = 2;\r\n"
        "    public LeafInner leaf = LeafInner();\r\n"
        "}\r\n"
        "class LeafInner {\r\n"
        "    public u64 payload = 99;\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/topo_layout_access.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());

    const usize aliasAPos = hdrView.find("using AliasA = i32;");
    const usize aliasBPos = hdrView.find("using AliasB = AliasA;");
    ASSERT_NE(aliasAPos, MStringView::npos);
    ASSERT_NE(aliasBPos, MStringView::npos);
    EXPECT_LT(aliasAPos, aliasBPos);

    const usize baseIfacePos = hdrView.find("class IBaseNode{");
    const usize derivedIfacePos = hdrView.find("class IDerivedNode : public IBaseNode{");
    ASSERT_NE(baseIfacePos, MStringView::npos);
    ASSERT_NE(derivedIfacePos, MStringView::npos);
    EXPECT_LT(baseIfacePos, derivedIfacePos);

    const usize leafPos = hdrView.find("class LeafInner final{");
    const usize midPos = hdrView.find("class MidOuter final{");
    const usize grandPos = hdrView.find("class GrandOuter final{");
    ASSERT_NE(leafPos, MStringView::npos);
    ASSERT_NE(midPos, MStringView::npos);
    ASSERT_NE(grandPos, MStringView::npos);
    EXPECT_LT(leafPos, midPos);
    EXPECT_LT(midPos, grandPos);

    ASSERT_EQ(res.unit.classes.size(), 4u);
    EXPECT_EQ(res.unit.classes[1]->name, "GrandOuter");
    EXPECT_EQ(res.unit.classes[1]->totalAlign, 8u);
    EXPECT_EQ(res.unit.classes[1]->totalSize, 24u);

    static constexpr MStringView s_PrivateFieldSource =
        "class SecretBox {\r\n"
        "    private i32 hidden = 7;\r\n"
        "}\r\n"
        "i32 Leak(in SecretBox b) {\r\n"
        "    return b.hidden;\r\n"
        "}\r\n";
    CompileResult privFieldRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_PrivateFieldSource, "impl/sim/priv_field.adhd", arena.arena, privFieldRes));

    static constexpr MStringView s_PrivateMethodSource =
        "class SecretBox {\r\n"
        "    private i32 calc()const { return 7; }\r\n"
        "}\r\n"
        "i32 Leak(in SecretBox b) {\r\n"
        "    return b.calc();\r\n"
        "}\r\n";
    CompileResult privMethodRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_PrivateMethodSource, "impl/sim/priv_method.adhd", arena.arena, privMethodRes));

    static constexpr MStringView s_PrivateCtorSource =
        "class SecretBox {\r\n"
        "    public i32 v = 0;\r\n"
        "    private SecretBox(in i32 x) { this.v = x; }\r\n"
        "}\r\n"
        "void Make() {\r\n"
        "    SecretBox b(5);\r\n"
        "}\r\n";
    CompileResult privCtorRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_PrivateCtorSource, "impl/sim/priv_ctor.adhd", arena.arena, privCtorRes));

    static constexpr MStringView s_AutoInterfaceByValueSource =
        "interface INode {\r\n"
        "    i32 id()const;\r\n"
        "}\r\n"
        "void BadAuto(in INode n) {\r\n"
        "    auto copy = n;\r\n"
        "}\r\n";
    CompileResult autoIfaceRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_AutoInterfaceByValueSource, "impl/sim/auto_iface.adhd", arena.arena, autoIfaceRes));

    static constexpr MStringView s_MutRebindConstSource =
        "interface INode {\r\n"
        "    void step();\r\n"
        "}\r\n"
        "class NodeImpl : INode {\r\n"
        "    public void step()override {}\r\n"
        "}\r\n"
        "void BadRebind(mut NodeImpl a, in NodeImpl b) {\r\n"
        "    mut INode ref = a;\r\n"
        "    ref = b;\r\n"
        "}\r\n";
    CompileResult mutRebindRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutRebindConstSource, "impl/sim/mut_rebind_const.adhd", arena.arena, mutRebindRes));
}

TEST(AdhdCompiler, ExcludesKCountFromEnumReflectionOrdersDeferredTemplateSpecsAndRejectsInvalidTemplates){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "enum<u8> Mode {\r\n"
        "    Idle,\r\n"
        "    Active,\r\n"
        "    kCount,\r\n"
        "}\r\n"
        "\r\n"
        "class Box<T> {\r\n"
        "    public T item = T();\r\n"
        "    public u8 tag = 0;\r\n"
        "}\r\n"
        "\r\n"
        "class Wrapper {\r\n"
        "    public Box<Payload> boxed = Box<Payload>();\r\n"
        "}\r\n"
        "\r\n"
        "class Payload {\r\n"
        "    public i32 count = 0;\r\n"
        "    public Mode mode = Mode::Idle;\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/named_enum_spec.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());

    EXPECT_NE(hdrView.find("constexpr usize reflectedEnumeratorCount()noexcept{ return 2u; }"), MStringView::npos);
    EXPECT_EQ(hdrView.find("{ \"kCount\", kCount },"), MStringView::npos);

    const usize boxPrimaryPos = hdrView.find("template<typename T>\r\nclass Box final{");
    const usize payloadClassPos = hdrView.find("class Payload final{");
    const usize boxSpecPos = hdrView.find("template<>\r\nclass Box<Payload> final{");
    const usize wrapperClassPos = hdrView.find("class Wrapper final{");
    ASSERT_NE(boxPrimaryPos, MStringView::npos);
    ASSERT_NE(payloadClassPos, MStringView::npos);
    ASSERT_NE(boxSpecPos, MStringView::npos);
    ASSERT_NE(wrapperClassPos, MStringView::npos);
    EXPECT_LT(boxPrimaryPos, boxSpecPos);
    EXPECT_LT(payloadClassPos, boxSpecPos);
    EXPECT_LT(boxSpecPos, wrapperClassPos);

    static constexpr MStringView s_TemplateRecSource =
        "class Box<T> {\r\n"
        "    public T item;\r\n"
        "}\r\n"
        "class BadRec {\r\n"
        "    public Box<BadRec> next;\r\n"
        "}\r\n";
    CompileResult recRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_TemplateRecSource, "impl/sim/template_rec.adhd", arena.arena, recRes));

    static constexpr MStringView s_TemplateSerializeBadSource =
        "[serialize]\r\n"
        "class Packet<T> {\r\n"
        "    public T data;\r\n"
        "}\r\n"
        "void UsePacket(in Packet<Vector<u32>> p) {}\r\n";
    CompileResult serRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_TemplateSerializeBadSource, "impl/sim/template_ser_bad.adhd", arena.arena, serRes));

    static constexpr MStringView s_BadEnumCountSource =
        "enum<u8> BadMode {\r\n"
        "    Idle,\r\n"
        "    Count,\r\n"
        "}\r\n";
    CompileResult badEnumCountRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadEnumCountSource, "impl/sim/bad_enum_count.adhd", arena.arena, badEnumCountRes));
}

TEST(AdhdCompiler, SuppressesLastUseMoveForMutCapturedLocalsAndRejectsMismatchedOolTemplateParams){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "u32 ConsumeIds(in Vector<u32> ids) {\r\n"
        "    return cast<u32>(ids.size());\r\n"
        "}\r\n"
        "\r\n"
        "u32 TestMutLambdaCaptureSuppressesMove() {\r\n"
        "    Vector<u32> captured;\r\n"
        "    auto mutator = lambda[mut captured]() -> void {\r\n"
        "        captured.push_back(1u);\r\n"
        "    };\r\n"
        "    return ConsumeIds(captured);\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/alias_ool.adhd", arena.arena, res));

    const MStringView srcView(res.generatedSource.data(), res.generatedSource.size());
    EXPECT_NE(srcView.find("return ConsumeIds(captured);"), MStringView::npos);
    EXPECT_EQ(srcView.find("return ConsumeIds(::Move(captured));"), MStringView::npos);

    static constexpr MStringView s_BadOolTemplateParamsSource =
        "class Pair<T, U> {\r\n"
        "    public T a;\r\n"
        "    public U b;\r\n"
        "    public void reset(in T x, in U y);\r\n"
        "}\r\n"
        "void Pair<X, Y>::reset(in X x, in Y y) {\r\n"
        "    this.a = x;\r\n"
        "    this.b = y;\r\n"
        "}\r\n";
    CompileResult badOolRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadOolTemplateParamsSource, "impl/sim/bad_ool_tparams.adhd", arena.arena, badOolRes));
}

TEST(AdhdCompiler, RejectsDeducedBoundViolationsWrongArityConstRefMutationInCtorAndProhibitedBool){
    AdhdTestArena arena;

    static constexpr MStringView s_DeducedBoundViolationSource =
        "interface ISource {\r\n"
        "    i32 read()const;\r\n"
        "}\r\n"
        "class Unrelated {\r\n"
        "    public i32 x = 0;\r\n"
        "}\r\n"
        "i32 ReadFrom<T : ISource>(in T src) {\r\n"
        "    return src.read();\r\n"
        "}\r\n"
        "void BadCall(in Unrelated u) {\r\n"
        "    ReadFrom(u);\r\n"
        "}\r\n";
    CompileResult badDeducedRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DeducedBoundViolationSource, "impl/sim/bad_deduced.adhd", arena.arena, badDeducedRes));

    static constexpr MStringView s_WrongArgCountSource =
        "i32 AddPair(in i32 a, in i32 b) {\r\n"
        "    return a + b;\r\n"
        "}\r\n"
        "void BadArity() {\r\n"
        "    AddPair(1);\r\n"
        "}\r\n";
    CompileResult badArityRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_WrongArgCountSource, "impl/sim/bad_arity.adhd", arena.arena, badArityRes));

    static constexpr MStringView s_MutateInRefInCtorSource =
        "interface ISource {\r\n"
        "    void write(in i32 v);\r\n"
        "}\r\n"
        "class BadHolder {\r\n"
        "    public in ISource roRef;\r\n"
        "    public BadHolder(in ISource initRef) {\r\n"
        "        this.roRef = initRef;\r\n"
        "        this.roRef.write(1);\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badCtorMutRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutateInRefInCtorSource, "impl/sim/bad_ctor_in_mut.adhd", arena.arena, badCtorMutRes));

    static constexpr MStringView s_ProhibitedBoolSource =
        "class BadFlagHolder {\r\n"
        "    public bo" "ol enabled = false;\r\n"
        "}\r\n";
    CompileResult prohibitedBoolRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ProhibitedBoolSource, "impl/sim/prohibited_b8.adhd", arena.arena, prohibitedBoolRes));
}

TEST(AdhdCompiler, ValidatesDigitSeparatorsEnumForwardRefsFlagOpsSwitchConstEvalAndInRefRvalueBind){
    AdhdTestArena arena;

    static constexpr MStringView s_ValidEdgeCasesSource =
        "const usize kBaseCount = 2u;\r\n"
        "const u32 kSepDec = 1'000'000u;\r\n"
        "const u32 kSepHex = 0xFF'00u;\r\n"
        "const u32 kSepBin = 0b1010'0101u;\r\n"
        "const f32 kSepFloat = 3.141'592f;\r\n"
        "flag<u32> RenderFlags {\r\n"
        "    None = 0u,\r\n"
        "    Opaque,\r\n"
        "    Shadow,\r\n"
        "    Both = Opaque | Shadow,\r\n"
        "}\r\n"
        "u32 EvalFlags(in RenderFlags f) {\r\n"
        "    Array<u32, (kBaseCount + 2u)> lut = Array<u32, (kBaseCount + 2u)>();\r\n"
        "    RenderFlags combined = f | RenderFlags::Opaque;\r\n"
        "    combined |= RenderFlags::Shadow;\r\n"
        "    return cast<u32>(combined) + kSepDec + kSepHex + kSepBin + cast<u32>(lut.size());\r\n"
        "}\r\n";
    CompileResult validRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_ValidEdgeCasesSource, "impl/sim/valid_edge_cases.adhd", arena.arena, validRes));
    const MStringView validHdrView(validRes.generatedHeader.data(), validRes.generatedHeader.size());
    EXPECT_NE(validHdrView.find("enum Flag : ::u32"), MStringView::npos);
    EXPECT_NE(validHdrView.find("Opaque = (static_cast<::u32>(1) << 0u)"), MStringView::npos);
    EXPECT_NE(validHdrView.find("Shadow = (static_cast<::u32>(1) << 1u)"), MStringView::npos);
    EXPECT_NE(validHdrView.find("const RenderFlags::Flag f"), MStringView::npos);

    static constexpr MStringView s_BadAdjacentSeparatorSource =
        "const u32 kBad = 1''000u;\r\n";
    CompileResult badAdjSepRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadAdjacentSeparatorSource, "impl/sim/bad_adj_sep.adhd", arena.arena, badAdjSepRes));

    static constexpr MStringView s_BadPrefixSeparatorSource =
        "const u32 kBad = 0x'FFu;\r\n";
    CompileResult badPfxSepRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadPrefixSeparatorSource, "impl/sim/bad_pfx_sep.adhd", arena.arena, badPfxSepRes));

    static constexpr MStringView s_BadEnumForwardRefSource =
        "enum<u32> BadOrder {\r\n"
        "    First = Second,\r\n"
        "    Second = 1u,\r\n"
        "}\r\n";
    CompileResult badEnumFwdRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadEnumForwardRefSource, "impl/sim/bad_enum_fwd.adhd", arena.arena, badEnumFwdRes));

    static constexpr MStringView s_BadFlagMismatchSource =
        "flag<u32> FlagA { None = 0u, Bit0 }\r\n"
        "flag<u32> FlagB { None = 0u, Bit0 }\r\n"
        "void MixFlags() {\r\n"
        "    auto x = FlagA::Bit0 | FlagB::Bit0;\r\n"
        "}\r\n";
    CompileResult badFlagMixRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadFlagMismatchSource, "impl/sim/bad_flag_mix.adhd", arena.arena, badFlagMixRes));

    static constexpr MStringView s_BadEnumIncrementSource =
        "enum<u32> Step { One = 1u, Two = 2u }\r\n"
        "void IncEnum() {\r\n"
        "    Step s = Step::One;\r\n"
        "    ++s;\r\n"
        "}\r\n";
    CompileResult badEnumIncRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadEnumIncrementSource, "impl/sim/bad_enum_inc.adhd", arena.arena, badEnumIncRes));

    static constexpr MStringView s_BadDupConstCaseSource =
        "i32 BadSwitch(in i32 v) {\r\n"
        "    switch(v) {\r\n"
        "    case 3:\r\n"
        "        return 1;\r\n"
        "    case 1 + 2:\r\n"
        "        return 2;\r\n"
        "    default:\r\n"
        "        return 0;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badSwitchCaseRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadDupConstCaseSource, "impl/sim/bad_switch_case.adhd", arena.arena, badSwitchCaseRes));

    static constexpr MStringView s_BadZeroArrayExprSource =
        "class BadArr {\r\n"
        "    public Array<i32, (2 - 2)> data;\r\n"
        "}\r\n";
    CompileResult badZeroArrRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadZeroArrayExprSource, "impl/sim/bad_zero_arr.adhd", arena.arena, badZeroArrRes));

    static constexpr MStringView s_BadInRefRvalueSource =
        "void BadInBind(in i32 a, in i32 b) {\r\n"
        "    in i32 sumRef = a + b;\r\n"
        "}\r\n";
    CompileResult badInBindRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadInRefRvalueSource, "impl/sim/bad_in_bind.adhd", arena.arena, badInBindRes));
}

TEST(AdhdCompiler, ValidatesHexFloatsSnakeCaseEnumParsingNestedDeductionAndStrictTypeRules){
    AdhdTestArena arena;

    static constexpr MStringView s_ValidModernFeaturesSource =
        "#define THRESHOLD 1'000\r\n"
        "#if THRESHOLD == 1'000\r\n"
        "const f32 kHexFloat = 0x1.5p-4f;\r\n"
        "const f32 kTrailingDot = 1.f;\r\n"
        "#endif\r\n"
        "enum<u8> BlendStep {\r\n"
        "    StepTowards,\r\n"
        "    HoldValue,\r\n"
        "}\r\n"
        "interface IComparable<T> {\r\n"
        "    b8 equals(in T other)const;\r\n"
        "}\r\n"
        "class Item : IComparable<Item> {\r\n"
        "    public i32 id = 0;\r\n"
        "    public b8 equals(in Item other)const override {\r\n"
        "        return this.id == other.id;\r\n"
        "    }\r\n"
        "}\r\n"
        "b8 CheckFirst<T : IComparable<T>>(in Span<T> items, in T target) {\r\n"
        "    if(items.empty())\r\n"
        "        return false;\r\n"
        "    return items[0u].equals(target);\r\n"
        "}\r\n"
        "b8 UseDeducedSpan(in Span<Item> items, in Item target) {\r\n"
        "    return CheckFirst(items, target) && (kHexFloat < kTrailingDot);\r\n"
        "}\r\n";
    CompileResult validRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_ValidModernFeaturesSource, "impl/sim/modern_features.adhd", arena.arena, validRes));
    const MStringView hdrView(validRes.generatedHeader.data(), validRes.generatedHeader.size());
    EXPECT_NE(hdrView.find("{ \"StepTowards\", StepTowards },"), MStringView::npos);
    EXPECT_NE(hdrView.find("{ \"step_towards\", StepTowards },"), MStringView::npos);

    static constexpr MStringView s_BadNwbIncludeSource =
        "#include \"some_asset.nwb\"\r\n"
        "const i32 kVal = 1;\r\n";
    CompileResult badNwbIncRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadNwbIncludeSource, "impl/sim/bad_nwb_inc.adhd", arena.arena, badNwbIncRes));

    static constexpr MStringView s_BadDefineDefinedSource =
        "#define defined 1\r\n"
        "const i32 kVal = 1;\r\n";
    CompileResult badDefDefinedRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadDefineDefinedSource, "impl/sim/bad_def_defined.adhd", arena.arena, badDefDefinedRes));

    static constexpr MStringView s_BadPrivateDefaultCtorSource =
        "class Secret {\r\n"
        "    public i32 x = 0;\r\n"
        "    private Secret() {}\r\n"
        "}\r\n"
        "void MakeSecret() {\r\n"
        "    Secret s;\r\n"
        "}\r\n";
    CompileResult badPrivCtorRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadPrivateDefaultCtorSource, "impl/sim/bad_priv_ctor.adhd", arena.arena, badPrivCtorRes));

    static constexpr MStringView s_BadConflictingDeducedSource =
        "void SameType<T>(in T a, in T b) {}\r\n"
        "void CallMismatch() {\r\n"
        "    SameType(1, \"hello\");\r\n"
        "}\r\n";
    CompileResult badConflictRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadConflictingDeducedSource, "impl/sim/bad_conflict.adhd", arena.arena, badConflictRes));

    static constexpr MStringView s_BadSwitchContainerSource =
        "void BadSwitchVec(in Vector<i32> v) {\r\n"
        "    switch(v) {\r\n"
        "    default:\r\n"
        "        break;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badSwitchVecRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadSwitchContainerSource, "impl/sim/bad_switch_vec.adhd", arena.arena, badSwitchVecRes));

    static constexpr MStringView s_BadComparisonReturnSource =
        "class BadCmp {\r\n"
        "    public i32 x = 0;\r\n"
        "    public i32 operator==(in BadCmp rhs)const {\r\n"
        "        return 1;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badCmpRetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadComparisonReturnSource, "impl/sim/bad_cmp_ret.adhd", arena.arena, badCmpRetRes));

    static constexpr MStringView s_DeducedInstantiationAndSimdHashSource =
        "class Box<T> {\r\n"
        "    public T value;\r\n"
        "    public Box(in T v) {\r\n"
        "        this.value = v;\r\n"
        "    }\r\n"
        "}\r\n"
        "Box<T> WrapInBox<T>(in T v) {\r\n"
        "    return Box<T>(v);\r\n"
        "}\r\n"
        "[reflect]\r\n"
        "class NameTable {\r\n"
        "    public Vector<Name> names;\r\n"
        "    public HashMap<Name, u32> lookup;\r\n"
        "}\r\n"
        "usize HashSimdAndWrap(in VectorSingle a, in VectorSingle b) {\r\n"
        "    auto wrapped = WrapInBox(42u);\r\n"
        "    f32x4 sum(a + b);\r\n"
        "    return HashCombineValue(usize(wrapped.value), sum);\r\n"
        "}\r\n";
    CompileResult instSimdRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_DeducedInstantiationAndSimdHashSource, "impl/sim/inst_simd.adhd", arena.arena, instSimdRes));
    const MStringView instHdr(instSimdRes.generatedHeader.data(), instSimdRes.generatedHeader.size());
    const MStringView instSrc(instSimdRes.generatedSource.data(), instSimdRes.generatedSource.size());
    EXPECT_NE(instHdr.find("template<>\r\nclass Box<u32> final"), MStringView::npos);
    EXPECT_NE(instSrc.find("::StoreFloat(::VectorAdd(a, b), sum);"), MStringView::npos);
    EXPECT_NE(instSrc.find("::HashCombineValue(usize(wrapped.value), sum)"), MStringView::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        "void BadWait() {\r\n"
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
        "void InspectNative(mut String text) {\r\n"
        "    native(mut s = text) {\r\n"
        "        if(!s.empty() && s[0] == '{') {\r\n"
        "            s[0] = '}';\r\n"
        "        }\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    EXPECT_TRUE(Compiler::Compile(s_NativeBraceCharSource, "impl/data/indexed_bucket.adhd", arena.arena, res));

    static constexpr MStringView s_VariadicNativeMacroSource =
        "#define EXPECTED_COUNT 1u\r\n"
        "#define EXPECT_EQ(...) native{ EXPECT_EQ(...) }\r\n"
        "#define EXPECT_NEAR_VAL(val, ...) native{ EXPECT_NEAR(val, __VA_ARGS__) }\r\n"
        "void FillBytes(mut Span<u8> bytes) {\r\n"
        "    for(usize i = 0u; i < bytes.size(); ++i)\r\n"
        "        bytes[i] = cast<u8>(i);\r\n"
        "}\r\n"
        "void ConsumeVec(in Vector<i32> items) {}\r\n"
        "void VerifyItems(in f32 sample) {\r\n"
        "    Vector<i32> items;\r\n"
        "    items.push_back(42);\r\n"
        "    ConsumeVec(items);\r\n"
        "    if(!items.empty())\r\n"
        "        EXPECT_EQ(items.size(), EXPECTED_COUNT);\r\n"
        "    else\r\n"
        "        EXPECT_NEAR_VAL(sample, 0.0f, 1e-4f);\r\n"
        "}\r\n";
    CompileResult variadicMacroRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_VariadicNativeMacroSource, "tests/unit/sample_check.adhd", arena.arena, variadicMacroRes));
    const MStringView variadicHdrView(variadicMacroRes.generatedHeader.data(), variadicMacroRes.generatedHeader.size());
    const MStringView variadicSrcView(variadicMacroRes.generatedSource.data(), variadicMacroRes.generatedSource.size());
    EXPECT_NE(variadicHdrView.find("void FillBytes(const Span<u8> bytes)noexcept;"), MStringView::npos);
    EXPECT_NE(variadicSrcView.find("void FillBytes(const Span<u8> bytes)noexcept{"), MStringView::npos);
    EXPECT_NE(variadicSrcView.find("EXPECT_EQ(items.size(), 1u);"), MStringView::npos);
    EXPECT_NE(variadicSrcView.find("EXPECT_NEAR(sample, 0.0f, 1e-4f);"), MStringView::npos);
    EXPECT_NE(variadicSrcView.find("ConsumeVec(items);"), MStringView::npos);
    EXPECT_EQ(variadicSrcView.find("Move(items)"), MStringView::npos);

    static constexpr MStringView s_CtorMemberLiftSource =
        "class Child {\r\n"
        "    private mut i32 m_target;\r\n"
        "    public Child(mut i32 target) {\r\n"
        "        this.m_target = target;\r\n"
        "    }\r\n"
        "}\r\n"
        "class Parent {\r\n"
        "    private Child m_child;\r\n"
        "    public Parent(mut i32 target) {\r\n"
        "        this.m_child = Child(target);\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult ctorLiftRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_CtorMemberLiftSource, "impl/data/ctor_lift.adhd", arena.arena, ctorLiftRes));
    const MStringView ctorLiftHdrView(ctorLiftRes.generatedHeader.data(), ctorLiftRes.generatedHeader.size());
    const MStringView ctorLiftSrcView(ctorLiftRes.generatedSource.data(), ctorLiftRes.generatedSource.size());
    EXPECT_NE(ctorLiftHdrView.find("Child m_child;"), MStringView::npos);
    EXPECT_EQ(ctorLiftHdrView.find("Child m_child{};"), MStringView::npos);
    EXPECT_NE(ctorLiftHdrView.find("Parent(i32& target)noexcept : m_child(target){"), MStringView::npos);
    EXPECT_EQ(ctorLiftHdrView.find("this->m_child = Child(target)"), MStringView::npos);
    EXPECT_EQ(ctorLiftSrcView.find("this->m_child = Child(target)"), MStringView::npos);

    static constexpr MStringView s_ExternalIfaceAndTrailingNativeSource =
        "native{\r\n"
        "interface IExternalCallback;\r\n"
        "}\r\n"
        "class CallbackRunner : IExternalCallback {\r\n"
        "    public ~CallbackRunner() {}\r\n"
        "    public b8 onTick(in f32 dt)override {\r\n"
        "        RegisterSelf(this);\r\n"
        "        return dt > 0.0f && ExternalEnum::Active != 0;\r\n"
        "    }\r\n"
        "}\r\n"
        "native{\r\n"
        "void RegisterRunner(CallbackRunner& runner);\r\n"
        "}\r\n";
    CompileResult extIfaceRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_ExternalIfaceAndTrailingNativeSource, "impl/data/ext_iface.adhd", arena.arena, extIfaceRes));
    const MStringView extIfaceHdrView(extIfaceRes.generatedHeader.data(), extIfaceRes.generatedHeader.size());
    const MStringView extIfaceSrcView(extIfaceRes.generatedSource.data(), extIfaceRes.generatedSource.size());
    EXPECT_NE(extIfaceHdrView.find("class CallbackRunner final : public IExternalCallback{"), MStringView::npos);
    EXPECT_NE(extIfaceHdrView.find("virtual ~CallbackRunner()noexcept override"), MStringView::npos);
    EXPECT_NE(extIfaceHdrView.find("virtual b8 onTick(const f32 dt)noexcept override"), MStringView::npos);
    EXPECT_NE(extIfaceSrcView.find("RegisterSelf((*this));"), MStringView::npos);
    EXPECT_NE(extIfaceSrcView.find("ExternalEnum::Active"), MStringView::npos);
    EXPECT_EQ(extIfaceHdrView.find("void RegisterRunner(CallbackRunner& runner);"), MStringView::npos);
    EXPECT_NE(extIfaceSrcView.find("void RegisterRunner(CallbackRunner& runner);"), MStringView::npos);

    static constexpr MStringView s_BadVariadicParamSource =
        "#define BAD_VARIADIC(..., x) (x)\r\n"
        "const i32 kVal = 1;\r\n";
    CompileResult badVariadicRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadVariadicParamSource, "impl/data/bad_variadic.adhd", arena.arena, badVariadicRes));

    static constexpr MStringView s_DuplicateFreeFnSource =
        "i32 DupFn(in i32 x) { return x; }\r\n"
        "i32 DupFn(in i32 x) { return x + 1; }\r\n";
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
        "MatrixSingle MakeSplatMatrix(in f32 s) {\r\n"
        "    MatrixSingle m = MatrixSingle(s);\r\n"
        "    return m;\r\n"
        "}\r\n";
    CompileResult validMatRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_ValidMatrixSplatSource, "impl/sim/mat_splat.adhd", arena.arena, validMatRes));
    const MStringView matSrcView(validMatRes.generatedSource.data(), validMatRes.generatedSource.size());
    EXPECT_NE(matSrcView.find("::MatrixSet("), MStringView::npos);

    static constexpr MStringView s_StorageOnlySimdParamSource =
        "VectorSingle CombinePlane(in VectorSingle rawNormal, in VectorSingle cameraPosition) {\r\n"
        "    return rawNormal + cameraPosition;\r\n"
        "}\r\n"
        "VectorSingle BuildSidePlane(in VectorSingle forward, in VectorSingle side, in VectorSingle tanHalfAngle, in VectorSingle cameraPosition) {\r\n"
        "    const VectorSingle rawNormal = forward * tanHalfAngle + side;\r\n"
        "    return CombinePlane(VectorSingle(rawNormal.x, rawNormal.y, rawNormal.z, 0.0f), cameraPosition);\r\n"
        "}\r\n";
    CompileResult storageOnlyRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_StorageOnlySimdParamSource, "impl/sim/storage_only_param.adhd", arena.arena, storageOnlyRes));
    const MStringView storageOnlyView(storageOnlyRes.generatedSource.data(), storageOnlyRes.generatedSource.size());
    const usize buildSidePos = storageOnlyView.find("BuildSidePlane(");
    ASSERT_NE(buildSidePos, MStringView::npos);
    EXPECT_EQ(storageOnlyView.substr(buildSidePos).find("::LoadFloat(cameraPosition)"), MStringView::npos);

    static constexpr MStringView s_BadStorageSimdParamSource =
        "void BadStorageParam(in f32x4 v) {\r\n"
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
        "void BadCalcContainer() {\r\n"
        "    Array<VectorSingle, 3u> positions;\r\n"
        "}\r\n";
    CompileResult badSimdCalcContainerRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadSimdCalcContainerSource, "impl/sim/bad_calc_container.adhd", arena.arena, badSimdCalcContainerRes));

    static constexpr MStringView s_BadSimdCalcGlobalConstSource =
        "const VectorSingle s_DefaultVec(0.0f, 0.0f, 0.0f, 0.0f);\r\n";
    CompileResult badSimdCalcGlobalConstRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadSimdCalcGlobalConstSource, "impl/sim/bad_calc_const.adhd", arena.arena, badSimdCalcGlobalConstRes));

    static constexpr MStringView s_BadMixedSimdSplatSource =
        "void BadMixedSplat() {\r\n"
        "    f32x3_i32 v = f32x3_i32(1.0f);\r\n"
        "}\r\n";
    CompileResult badMixedSplatRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadMixedSimdSplatSource, "impl/sim/bad_mixed_splat.adhd", arena.arena, badMixedSplatRes));

    static constexpr MStringView s_BadMixedSimdIndexSource =
        "f32 BadMixedIndex() {\r\n"
        "    f32x3_i32 v(1.0f, 2.0f, 3.0f, 4);\r\n"
        "    return v[0];\r\n"
        "}\r\n";
    CompileResult badMixedIdxRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadMixedSimdIndexSource, "impl/sim/bad_mixed_idx.adhd", arena.arena, badMixedIdxRes));

    static constexpr MStringView s_BadSimdAritySource =
        "void BadSimdArity() {\r\n"
        "    f32x4 v = f32x4(1.0f, 2.0f);\r\n"
        "}\r\n";
    CompileResult badSimdArityRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadSimdAritySource, "impl/sim/bad_simd_arity.adhd", arena.arena, badSimdArityRes));

    static constexpr MStringView s_BadParStepZeroSource =
        "void BadParStep() {\r\n"
        "    parallel_for(usize i = 0u; i < 8u; i += 0u) {\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badParStepRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadParStepZeroSource, "impl/sim/bad_par_step.adhd", arena.arena, badParStepRes));

    static constexpr MStringView s_BadParFloatIndexSource =
        "void BadParFloatIdx() {\r\n"
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
        "b8 TryLoadVec(mut VectorSingle outVec) {\r\n"
        "    outVec = VectorSingle(1.0f, 2.0f, 3.0f, 4.0f);\r\n"
        "    return true;\r\n"
        "}\r\n"
        "f32 ComputeFromCondAndMat(in MatrixSingle mat) {\r\n"
        "    VectorSingle trackMin = VectorSingle(0.0f, 0.0f, 0.0f, 0.0f);\r\n"
        "    if(!TryLoadVec(trackMin))\r\n"
        "        return 0.0f;\r\n"
        "    const VectorSingle clamped = Saturate(trackMin);\r\n"
        "    return clamped.x + mat[0].x;\r\n"
        "}\r\n";
    CompileResult mutCondRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_SimdMutCondSaturateMatRowSource, "impl/sim/mut_cond_mat.adhd", arena.arena, mutCondRes));
    const MStringView mutCondView(mutCondRes.generatedSource.data(), mutCondRes.generatedSource.size());
    EXPECT_NE(mutCondView.find("::VectorSaturate(trackMin)"), MStringView::npos);
    EXPECT_NE(mutCondView.find("mat.v[0]"), MStringView::npos);

    static constexpr MStringView s_SimdLaneInsertAndPermuteSource =
        "VectorSingle NormalizeTangentWithSign(in VectorSingle tangent, in VectorSingle normal, in f32x3u posIn, mut f32x3u outPos) {\r\n"
        "    const VectorSingle loadedPos = VectorSingle(posIn.x, posIn.y, posIn.z, 0.0f);\r\n"
        "    outPos = f32x3u(loadedPos.x, loadedPos.y, loadedPos.z);\r\n"
        "    if(LengthSq(tangent.xyz) > 0.0001f) {\r\n"
        "        const VectorSingle normalized = Normalize(tangent.xyz);\r\n"
        "        f32 sign = 1.0f;\r\n"
        "        if(tangent.w < 0.0f)\r\n"
        "            sign = -1.0f;\r\n"
        "        return VectorSingle(normalized.x, normalized.y, normalized.z, sign);\r\n"
        "    }\r\n"
        "    const VectorSingle fallback = Normalize(normal.xyz);\r\n"
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
        "VectorSingle OptimizeSimdOps(in VectorSingle a, in VectorSingle b, in VectorDouble d, in MatrixSingle m, in BoxData box, in f32 scale, mut BoxData outBox) {\r\n"
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
        "    const f32 dot3ZeroW = Dot(a, VectorSingle(1.0f, 2.0f, 3.0f, 0.0f));\r\n"
        "    const f32 sumAll = Dot(a, VectorSingle(1.0f));\r\n"
        "    const VectorSingle satClamp = Clamp(a, 0.0f, 1.0f);\r\n"
        "    const VectorSingle satMinMax = Min(Max(b, 0.0f), 1.0f);\r\n"
        "    const VectorSingle storageSwiz = VectorSingle(box.minPt.z, box.minPt.y, box.minPt.x, 0.0f);\r\n"
        "    const VectorSingle storageBinOp = VectorSingle(box.minPt.x + box.extent4.x, box.minPt.y + box.extent4.y, box.minPt.z + box.extent4.z, 0.0f);\r\n"
        "    const VectorSingle storageNeg3 = VectorSingle(-box.minPt.x, -box.minPt.y, -box.minPt.z, 0.0f);\r\n"
        "    const VectorSingle laneMathMin = VectorSingle(Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z), Min(a.w, b.w));\r\n"
        "    const VectorSingle altMaskSplat = VectorSingle(scale, 0.0f, scale, 0.0f);\r\n"
        "    const VectorSingle zeroSubAdd = (0.0f - a) + (b - 0.0f);\r\n"
        "    const VectorSingle lerpZero = Lerp(a, b, 0.0f);\r\n"
        "    const f32 swizGetX = (a.zyx).x;\r\n"
        "    const f32 max3 = Max(a.x, Max(a.y, a.z));\r\n"
        "    const VectorSingle max4Splat = VectorSingle(Max(Max(a.x, a.y), Max(a.z, a.w)));\r\n"
        "    const u32 bitsY = bit_cast<u32>(a.y);\r\n"
        "    const VectorSingle swizFromDouble = VectorSingle(cast<f32>(d.z), cast<f32>(d.y), cast<f32>(d.x), cast<f32>(d.w));\r\n"
        "    const MatrixSingle mulTrans = Transpose(m * scaledMat);\r\n"
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
        "    const VectorSingle proj = a * Dot(a.xyz, b.xyz);\r\n"
        "    const VectorSingle scaled = b * VectorSingle(scale);\r\n"
        "    const VectorSingle invLen = a * Rsqrt(LengthSq(a.xyz));\r\n"
        "    const VectorSingle pt = Mul(VectorSingle(box.minPt, s_AffineW), scaledMat);\r\n"
        "    const VectorSingle pt2 = Mul(VectorSingle(a.x, a.y, 0.0f, 1.0f), projMat);\r\n"
        "    const VectorSingle nrm = Mul(VectorSingle(b, 0.0f), m) + unitMul;\r\n"
        "    const MatrixSingle transposed = Transpose(m);\r\n"
        "    const VectorSingle transRow0 = Mul(VectorSingle(1.0f, 0.0f, 0.0f, 0.0f), transposed);\r\n"
        "    const VectorSingle transRow2 = Mul(VectorSingle(0.0f, 0.0f, 1.0f, 0.0f), Transpose(m));\r\n"
        "    const VectorSingle matCol0 = Mul(VectorSingle(1.0f, 0.0f, 0.0f, 0.0f), m);\r\n"
        "    const VectorSingle norm3 = Normalize(a.xyz);\r\n"
        "    const VectorSingle norm3ZeroW = VectorSingle(norm3, 0.0f);\r\n"
        "    const VectorSingle scaledNorm3 = norm3 * 2.0f;\r\n"
        "    const VectorSingle scaledNorm3Swiz = VectorSingle(scaledNorm3.y, scaledNorm3.z, scaledNorm3.x, 0.0f);\r\n"
        "    const VectorDouble mixedBounds = VectorDouble(Min(d.x, 1.0), Min(d.y, 2.0), Max(d.z, 3.0), Max(d.w, 4.0));\r\n"
        "    const VectorSingle nonUniformAdd = VectorSingle(a.x + scale, a.y + 1.0f, a.z + 2.0f, a.w + 3.0f);\r\n"
        "    const VectorSingle mixedAddSub = VectorSingle(a.x + b.x, a.y - b.y, a.z + b.z, a.w - b.w);\r\n"
        "    const VectorSingle uniformLaneMul = VectorSingle(a.x * b.y, a.y * b.y, a.z * b.y, a.w * b.y);\r\n"
        "    const VectorSingle planeEq = VectorSingle(norm3, -Dot(norm3.xyz, b.xyz));\r\n"
        "    const VectorSingle negNorm3ZeroW = VectorSingle(-norm3, 0.0f);\r\n"
        "    const VectorSingle negScaleFma = a * (-scale) + b;\r\n"
        "    const VectorDouble minD(cast<f64>(a.x), cast<f64>(a.y), 0.0, 0.0);\r\n"
        "    const VectorDouble divD = VectorDouble(d.x, d.y, 0.0, 0.0) / xyUnitZW;\r\n"
        "    const VectorDouble maxD = Max(minD, divD);\r\n"
        "    const VectorSingle narrowXY = VectorSingle(cast<f32>(maxD.x), cast<f32>(maxD.y), 0.0f, 0.0f);\r\n"
        "    const VectorSingle negAxisZ = VectorSingle(0.0f, 0.0f, -1.0f, 0.0f);\r\n"
        "    const VectorSingle unitXW = VectorSingle(1.0f, 0.0f, 0.0f, 1.0f);\r\n"
        "    const VectorSingle negZPosW = VectorSingle(0.0f, 0.0f, -1.0f, 1.0f);\r\n"
        "    const VectorSingle withUnitW = VectorSingle(a, 1.0f);\r\n"
        "    const f32 idxGetZ = a[2u];\r\n"
        "    outBox.extent4 = f32x4(box.minPt.x, box.minPt.y, box.minPt.z, 0.0f);\r\n"
        "    outBox.extent4 = f32x4(a, b.w);\r\n"
        "    if(!IsFinite(box.extent4[0u]) || !IsFinite(box.extent4[1u]) || !IsFinite(box.extent4[2u]) || !IsFinite(box.extent4[3u]))\r\n"
        "        return negAxisZ;\r\n"
        "    if(!IsFinite(d.x) || d.x > 10.0 || !IsFinite(d.y) || d.y > 10.0)\r\n"
        "        return constZero;\r\n"
        "    if(Abs(box.minPt.x) > 1.0f || Abs(box.minPt.y) > 1.0f || Abs(box.minPt.z) > 1.0f)\r\n"
        "        return constAxisY;\r\n"
        "    if(!IsFinite(a.x) || !IsFinite(a.y) || !IsFinite(a.z) || !IsFinite(a.w) || !IsFinite(a.x + a.z) || !IsFinite(a.y + a.w) || a.x < 0.0f || a.y < 0.0f || a.z < 0.0f || a.w < 0.0f)\r\n"
        "        return unitVec;\r\n"
        "    if(scale >= a.x && 2.0f >= a.y && scale < a.z && 2.0f < a.w)\r\n"
        "        return planeEq;\r\n"
        "    if(a.x <= a.z && a.y <= a.w && IsFinite(b.xyz) && a.x == b.x && a.y == b.y && a.z == b.z)\r\n"
        "        return halfVec + axisW + VectorSingle(xyUnitZW) + VectorSingle(mixedBounds);\r\n"
        "    return masked3 + clearX + flipYW + storageMaskXY + negRep + neg3KeepW + laneBinOp + axisYWithW + splatZ + foldedSwiz + identitySwiz + fromDouble + swizFromDouble + max4Splat + proj + scaled + invLen + pt + pt2 + nrm + transRow0 + transRow2 + matCol0 + norm3ZeroW + scaledNorm3Swiz + nonUniformAdd + mixedAddSub + uniformLaneMul + planeEq + negNorm3ZeroW + negScaleFma + narrowXY + negAxisZ + unitXW + negZPosW + withUnitW + negFma + splatMul + pad3 + signFlipSplat + satClamp + satMinMax + storageSwiz + storageBinOp + storageNeg3 + laneMathMin + altMaskSplat + zeroSubAdd + lerpZero + Mul(a, mulTrans) + VectorSingle(dot3ZeroW + sumAll + swizGetX + idxGetZ + max3 + cast<f32>(bitsY) + zeroStorageVec.x + zeroStorageMat[0][0]);\r\n"
        "}\r\n";

    CompileResult optRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_SimdOptSource, "impl/sim/simd_opt.adhd", arena.arena, optRes));
    const MStringView optView(optRes.generatedSource.data(), optRes.generatedSource.size());
    EXPECT_NE(optView.find("#pragma STDC FENV_ACCESS ON\r\n"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle negAxisZ = ::VectorAndInt(::s_SIMDNegativeOne, ::s_SIMDMaskZ);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle unitXW = ::VectorOrInt(::s_SIMDIdentityR0, ::s_SIMDIdentityR3);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle negZPosW = ::VectorPermute<0u, 1u, 6u, 3u>(::s_SIMDIdentityR3, ::s_SIMDNegativeOne);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle withUnitW = ::VectorPermute<0u, 1u, 2u, 7u>(a, ::s_SIMDIdentityR3);"), MStringView::npos);
    EXPECT_NE(optView.find("const f32 idxGetZ = ::VectorGetZ(a);"), MStringView::npos);
    EXPECT_NE(optView.find("::StoreFloat(::LoadFloat(box.minPt), outBox.extent4);"), MStringView::npos);
    EXPECT_NE(optView.find("((!::IsFinite(box.extent4.raw[0u]) || !::IsFinite(box.extent4.raw[1u])) || !::IsFinite(box.extent4.raw[2u])) || !::IsFinite(box.extent4.raw[3u])"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle scaledNorm3Swiz = ::VectorSwizzle<1u, 2u, 0u, 3u>(scaledNorm3);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorDouble mixedBounds = ::VectorDoublePermute<0u, 1u, 6u, 7u>(::VectorDoubleMin(d, ::VectorDoubleSet(1.0, 2.0, 3.0, 4.0)), ::VectorDoubleMax(d, ::VectorDoubleSet(1.0, 2.0, 3.0, 4.0)));"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle nonUniformAdd = ::VectorAdd(a, ::VectorSet(scale, 1.0f, 2.0f, 3.0f));"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle mixedAddSub = ::VectorAdd(a, ::VectorXorInt(b, ::s_SIMDFlipYW));"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle uniformLaneMul = ::VectorMultiply(a, ::VectorSplatY(b));"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle planeEq = ::VectorPermute<0u, 1u, 2u, 7u>(norm3, ::VectorNegate(::Vector3Dot(norm3, b)));"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle negNorm3ZeroW = ::VectorXorInt(norm3, ::s_SIMDNegate3);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle negScaleFma = ::VectorNegativeMultiplySubtract(a, ::VectorReplicate(scale), b);"), MStringView::npos);
    EXPECT_NE(optView.find("!::IsFinite((::VectorGetX(a) + ::VectorGetZ(a))) || !::IsFinite((::VectorGetY(a) + ::VectorGetW(a)))"), MStringView::npos);
    EXPECT_NE(optView.find("(scale >= ::VectorGetX(a)) && (2.0f >= ::VectorGetY(a)) && (scale < ::VectorGetZ(a)) && (2.0f < ::VectorGetW(a))"), MStringView::npos);

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
    EXPECT_NE(optView.find("const f32 max3 = ::VectorGetX(::Vector3MaxComponent(a));"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle max4Splat = ::Vector4MaxComponent(a);"), MStringView::npos);
    EXPECT_NE(optView.find("const u32 bitsY = ::VectorGetIntY(a);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle swizFromDouble = ::VectorSwizzle<2u, 1u, 0u, 3u>(::ConvertVectorDoubleToSingle(d));"), MStringView::npos);
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
    EXPECT_NE(optView.find("::VectorIsFinite(b, ::VectorComponentMask::s_XYZ)"), MStringView::npos);
    EXPECT_EQ(optView.find("::MatrixTranspose(m)"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle transRow0 = m.v[0u];"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle transRow2 = m.v[2u];"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle matCol0 = ::Vector4Transform(::s_SIMDIdentityR0, m);"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle norm3ZeroW = norm3;"), MStringView::npos);
    EXPECT_NE(optView.find("const ::SIMDVectorSingle narrowXY = ::ConvertVectorDoubleToSingle(maxD);"), MStringView::npos);
    EXPECT_EQ(optView.find("::VectorDoubleIsFinite(d,"), MStringView::npos);
    EXPECT_EQ(optView.find("::VectorMoveMask(::VectorGreater(::VectorAbs(::LoadFloat(box.minPt)),"), MStringView::npos);
    EXPECT_EQ(optView.find("::VectorIsFinite(a,"), MStringView::npos);
    EXPECT_EQ(optView.find("::VectorMoveMask(::VectorLess(a,"), MStringView::npos);
    EXPECT_EQ(optView.find("::Vector2LessOrEqual(a,"), MStringView::npos);
    EXPECT_EQ(optView.find("::Vector3Equal(a, b)"), MStringView::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        "void BadParallelNative(mut IntVec items) {\r\n"
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
        "void BadUninitRef() {\r\n"
        "    mut String s;\r\n"
        "}\r\n";
    CompileResult badRefRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UninitRefLocalSource, "impl/sim/bad_ref.adhd", arena.arena, badRefRes));

    static constexpr MStringView s_UnknownLambdaCaptureSource =
        "void BadCapture() {\r\n"
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
        "void BadSwitch(in u32 x) {\r\n"
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
        "void BadSwizzle() {\r\n"
        "    f32x3_i32 v(1.0f, 2.0f, 3.0f, 4);\r\n"
        "    f32x3_i32 s = v.xyzw;\r\n"
        "}\r\n";
    CompileResult badSwizzleRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MixedSwizzleSource, "impl/sim/bad_mixed_swizzle.adhd", arena.arena, badSwizzleRes));

    static constexpr MStringView s_WrongBuiltinAritySource =
        "f32 BadDot(in VectorSingle a) {\r\n"
        "    return Dot(a);\r\n"
        "}\r\n";
    CompileResult badBuiltinArityRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_WrongBuiltinAritySource, "impl/sim/bad_builtin_arity.adhd", arena.arena, badBuiltinArityRes));
}


TEST(AdhdCompiler, LowerBoundRejectsNonUsizeSpanArguments){
    AdhdTestArena arena;
    CompileResult valid(arena.arena);
    ASSERT_TRUE(Compiler::Compile("usize F(in Span<usize> b){ return LowerBoundIndex(b.subspan(0u, b.size()), 0u); }", "valid.adhd", arena.arena, valid));
    EXPECT_TRUE(MStringView(valid.generatedHeader).find("::LowerBoundIndex(") != MStringView::npos || MStringView(valid.generatedSource).find("::LowerBoundIndex(") != MStringView::npos);

    constexpr Array<MStringView, 2> s_InvalidSources{
        "usize F(){ return LowerBoundIndex(3u, 0u); }",
        "usize F(in Span<u32> b){ return LowerBoundIndex(b, 0u); }",
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
        "Expected<usize> Align(in usize value){ return AlignUpChecked(value, 4u); }\n"
        "void Store(in f32 value, mut FixedVector<u8, 8u> bytes){ WriteF32LittleEndianAt(value, 0u, bytes); }\n";
    ASSERT_TRUE(Compiler::Compile(s_Valid, "byte_builtins.adhd", arena.arena, valid));
    EXPECT_TRUE(MStringView(valid.generatedHeader).find("::AlignUpCheckedSize(") != MStringView::npos || MStringView(valid.generatedSource).find("::AlignUpCheckedSize(") != MStringView::npos);
    constexpr Array<MStringView, 3> s_InvalidSources{
        "u32 F(in Span<u32> b){ return ReadPartialU32LittleEndian(b, 0u); }",
        "void F(in f32 v, mut FixedVector<u32, 8u> b){ WriteF32LittleEndianAt(v, 0u, b); }",
        "void F(in f32 v, in FixedVector<u8, 8u> b){ WriteF32LittleEndianAt(v, 0u, b); }",
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
        "u8 Integer(in StringView s){ return ClassifyIntegerGrammar(s); }\n"
        "b8 Incomplete(in StringView s){ return ClassifyFloatGrammar(s) == s_NumericGrammarIncomplete; }\n"
        "f32 Bound(in f64 v, in b8 upper){ return RoundF64ToF32Bound(v, upper); }\n"
        "f64 Step(in f64 v, in f64 lo, in f64 hi, in f64 delta, in b8 up){ return StepF64Bounded(v, lo, hi, delta, up); }\n"
        "void Advance(mut u64 value){ AdvanceWrappingNonzeroU64(value); }\n"
        "u32 Width(in u32 value){ return BitWidthLowU32(value, 8u); }\n";
    ASSERT_TRUE(Compiler::Compile(s_Valid, "numeric_builtins.adhd", arena.arena, valid));
    const MStringView source(valid.generatedSource);
    const MStringView header(valid.generatedHeader);
    EXPECT_NE(header.find("#include <global/binary_big_endian.h>"), MStringView::npos);
    EXPECT_NE(header.find("#include <global/binary_little_endian.h>"), MStringView::npos);
    EXPECT_NE(header.find("#include <global/numeric_range.h>"), MStringView::npos);
    for(const MStringView name : { MStringView("::ClassifyIntegerGrammar("), MStringView("::ClassifyFloatGrammar("), MStringView("::RoundF64ToF32Bound("), MStringView("::StepF64Bounded("), MStringView("::AdvanceWrappingNonzero("), MStringView("::BitWidthLowU32("), MStringView("::s_NumericGrammarIncomplete") })
        EXPECT_TRUE(source.find(name) != MStringView::npos || header.find(name) != MStringView::npos);
    constexpr Array<MStringView, 8> s_Invalid{
        "u8 F(in u32 value){ return ClassifyIntegerGrammar(value); }",
        "f32 F(in f32 value, in b8 upper){ return RoundF64ToF32Bound(value, upper); }",
        "f32 F(in f64 value, in u32 upper){ return RoundF64ToF32Bound(value, upper); }",
        "f64 F(in f64 v, in b8 up){ return StepF64Bounded(v, v, v, v); }",
        "f64 F(in f64 v, in u32 up){ return StepF64Bounded(v, v, v, v, up); }",
        "void F(in u64 v){ AdvanceWrappingNonzeroU64(v); }",
        "void F(mut u32 v){ AdvanceWrappingNonzeroU64(v); }",
        "u32 F(in u64 v){ return BitWidthLowU32(v, 8u); }",
    };
    for(const MStringView text : s_Invalid){
        CompileResult invalid(arena.arena);
        EXPECT_FALSE(Compiler::Compile(text, "bad_numeric_builtins.adhd", arena.arena, invalid));
    }
}


TEST(AdhdCompiler, RawSimdHashKeepsRegisterAndMutableSeed){
    AdhdTestArena arena;
    CompileResult valid(arena.arena);
    ASSERT_TRUE(Compiler::Compile("void F(in VectorSingle v, mut usize seed){ HashCombineF32x4(v, seed); } b8 Eq(in VectorSingle a, in VectorSingle b){ return EqualF32x4Bits(a, b); }", "hash.adhd", arena.arena, valid));
    const MStringView header(valid.generatedHeader);
    const MStringView source(valid.generatedSource);
    const MStringView generated = source.find("::HashCombineF32x4(") != MStringView::npos ? source : header;
    EXPECT_NE(generated.find("::HashCombineF32x4(v, seed)"), MStringView::npos);
    EXPECT_EQ(generated.find("::LoadFloat("), MStringView::npos);
    constexpr Array<MStringView, 3> s_InvalidSources{
        "void F(in VectorSingle v){ HashCombineF32x4(v, 1u); }",
        "void F(in VectorSingle v, in usize seed){ HashCombineF32x4(v, seed); }",
        "void F(in VectorDouble v, mut usize seed){ HashCombineF32x4(v, seed); }",
    };
    for(const MStringView text : s_InvalidSources){
        CompileResult invalid(arena.arena);
        EXPECT_FALSE(Compiler::Compile(text, "bad_hash.adhd", arena.arena, invalid));
    }
    CompileResult shadowed(arena.arena);
    ASSERT_TRUE(Compiler::Compile("void HashCombineF32x4(in u32 v, mut usize seed){ seed = cast<usize>(v); } void F(in u32 v, mut usize seed){ HashCombineF32x4(v, seed); }", "shadow_hash.adhd", arena.arena, shadowed));
}


TEST(AdhdCompiler, RectangleIntrinsicsKeepRegistersAndRejectWrongTypes){
    AdhdTestArena arena;
    constexpr MStringView s_Valid =
        "type RectVec = VectorSingle;\n"
        "VectorSingle Combine(in RectVec a, in RectVec b){ return IntersectRectVec(IntersectRectBoundsVec(a, b), b); }\n"
        "b8 Visible(in RectVec a, in RectVec b){ return HasVisibleRectIntersectionVec(a, b); }\n"
        "VectorSingle Inset(in RectVec a, in RectVec b){ return InsetRectVec(a, b); }\n"
        "void Take(in RectVec v){} void Call(in RectVec a, in RectVec b){ Take(IntersectRectVec(a, b)); }\n";
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
        "VectorSingle F(in VectorSingle a){ return IntersectRectVec(a, 1.0f); }",
        "VectorSingle F(in VectorDouble a, in VectorSingle b){ return IntersectRectVec(a, b); }",
        "VectorSingle F(in MatrixSingle a, in VectorSingle b){ return IntersectRectVec(a, b); }",
    };
    for(const MStringView text : s_Invalid){
        CompileResult invalid(arena.arena);
        EXPECT_FALSE(Compiler::Compile(text, "invalid_rect.adhd", arena.arena, invalid));
        ASSERT_FALSE(invalid.errors.empty());
        EXPECT_EQ(MStringView(invalid.errors[0u].message), "built-in function argument must be VectorSingle");
    }
    constexpr Array<MStringView, 2> s_MutationTargets{
        "void Take(mut VectorSingle v){} void F(in VectorSingle a){ Take(IntersectRectVec(a, a)); }",
        "void Take(mut b8 v){} void F(in VectorSingle a){ Take(HasVisibleRectIntersectionVec(a, a)); }",
    };
    for(const MStringView text : s_MutationTargets){
        CompileResult invalid(arena.arena);
        EXPECT_FALSE(Compiler::Compile(text, "mut_rect.adhd", arena.arena, invalid));
        ASSERT_FALSE(invalid.errors.empty());
        EXPECT_EQ(MStringView(invalid.errors[0u].message), "cannot mutate or pass temporary rvalue return value to a 'mut' target");
    }

    CompileResult shadowed(arena.arena);
    constexpr MStringView s_Shadow = "VectorSingle IntersectRectVec(in VectorSingle a, in VectorSingle b){ return a; } VectorSingle F(in VectorSingle a, in VectorSingle b){ return IntersectRectVec(a, b); }";
    ASSERT_TRUE(Compiler::Compile(s_Shadow, "shadow_rect.adhd", arena.arena, shadowed));
    EXPECT_EQ(MStringView(shadowed.generatedHeader).find("::IntersectRectValuePreservingExtent("), MStringView::npos);
    EXPECT_EQ(MStringView(shadowed.generatedSource).find("::IntersectRectValuePreservingExtent("), MStringView::npos);

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_templates_interfaces_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::AdhdCompilerTestsDetail;
namespace Compiler = NWB::Core::Metascript::Adhd::Compiler;
using NWB::Core::Metascript::Adhd::CompileResult;
using NWB::Core::Metascript::MString;
using NWB::Core::Metascript::MStringView;
using NWB::Core::Metascript::MetaArena;

TEST(AdhdCompiler, RejectsTemplateBoundViolationsAndInterfaceDowncasts){
    AdhdTestArena arena;

    static constexpr MStringView s_InvalidBoundArgSource =
        "interface ITask {\r\n"
        "    void execute(in f32 dt);\r\n"
        "}\r\n"
        "class PlainData {\r\n"
        "    public u32 value = 0;\r\n"
        "}\r\n"
        "class TaskRunner<T : ITask> {\r\n"
        "    public T task = T();\r\n"
        "}\r\n"
        "void BadUse() {\r\n"
        "    TaskRunner<PlainData> runner = TaskRunner<PlainData>();\r\n"
        "}\r\n";

    CompileResult invalidBoundRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_InvalidBoundArgSource, "impl/bad_bound.adhd", arena.arena, invalidBoundRes));

    static constexpr MStringView s_DowncastInterfaceSource =
        "interface ITask {\r\n"
        "    void execute(in f32 dt);\r\n"
        "}\r\n"
        "class PhysicsTask : ITask {\r\n"
        "    public void execute(in f32 dt) override {}\r\n"
        "}\r\n"
        "void BadDowncast(in ITask task) {\r\n"
        "    PhysicsTask p = cast<PhysicsTask>(task);\r\n"
        "}\r\n";
    CompileResult downcastRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DowncastInterfaceSource, "impl/bad_downcast.adhd", arena.arena, downcastRes));
}

TEST(AdhdCompiler, RejectsNonConstMethodCallsOnInParameters){
    AdhdTestArena arena;

    static constexpr MStringView s_ConstViolationSource =
        "class Counter {\r\n"
        "    private u32 count = 0;\r\n"
        "    public void bump() {\r\n"
        "        this.count += 1;\r\n"
        "    }\r\n"
        "}\r\n"
        "void BadConstCall(in Counter c) {\r\n"
        "    c.bump();\r\n"
        "}\r\n";
    CompileResult constRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ConstViolationSource, "impl/bad_const.adhd", arena.arena, constRes));
}

TEST(AdhdCompiler, RejectsUnboundedMethodCallsCStyleArraysRawCppIncludesAndByValueInterfaces){
    AdhdTestArena arena;

    static constexpr MStringView s_UnboundedMethodCall =
        "void BadUnbounded<T>(mut T comp) {\r\n"
        "    comp.step(1.0f);\r\n"
        "}\r\n";
    CompileResult unboundedRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UnboundedMethodCall, "impl/bad_unbounded.adhd", arena.arena, unboundedRes));

    static constexpr MStringView s_CStyleArraySource =
        "void BadArray() {\r\n"
        "    i32 values[4] = 0;\r\n"
        "}\r\n";
    CompileResult cArrayRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_CStyleArraySource, "impl/bad_array.adhd", arena.arena, cArrayRes));

    static constexpr MStringView s_CppIncludeOutsideNative =
        "#include <vector>\r\n"
        "void Nop() {}\r\n";
    CompileResult cppIncRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_CppIncludeOutsideNative, "impl/bad_inc.adhd", arena.arena, cppIncRes));

    static constexpr MStringView s_ByValueInterfaceVar =
        "interface IComponent {\r\n"
        "    void step(in f32 dt);\r\n"
        "}\r\n"
        "class Comp : IComponent {\r\n"
        "    public void step(in f32 dt) override {}\r\n"
        "}\r\n"
        "void BadByValue() {\r\n"
        "    Comp c = Comp();\r\n"
        "    IComponent val = c;\r\n"
        "}\r\n";
    CompileResult byValIfaceRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ByValueInterfaceVar, "impl/bad_val_iface.adhd", arena.arena, byValIfaceRes));

    static constexpr MStringView s_MismatchedOverrideConst =
        "interface IComponent {\r\n"
        "    u32 id()const;\r\n"
        "}\r\n"
        "class BadComp : IComponent {\r\n"
        "    public u32 id() override {\r\n"
        "        return 0;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult mismatchOverrideRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MismatchedOverrideConst, "impl/bad_override_sig.adhd", arena.arena, mismatchOverrideRes));
}

TEST(AdhdCompiler, OmitsIncludedDeclarationsFromGeneratedOutputAndRejectsInvalidEnumOperatorsAndControlFlow){
    AdhdTestArena arena;
    MockIncludeResolver resolver;

    static constexpr MStringView s_SplitImplSource =
        "#include \"shared_service.adhd\"\r\n"
        "i32 SharedService::accumulate(in i32 delta) {\r\n"
        "    this.m_total += delta;\r\n"
        "    return this.m_total;\r\n"
        "}\r\n";

    CompileResult splitRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_SplitImplSource, "impl/service/shared_service_impl.adhd", &resolver, arena.arena, splitRes));
    const MStringView hdrView(splitRes.generatedHeader.data(), splitRes.generatedHeader.size());
    const MStringView cppView(splitRes.generatedSource.data(), splitRes.generatedSource.size());
    EXPECT_NE(hdrView.find("#include \"shared_service.adhd.h\""), MStringView::npos);
    EXPECT_EQ(hdrView.find("class SharedService final"), MStringView::npos);
    EXPECT_EQ(cppView.find("SharedService::reflectedFieldCount"), MStringView::npos);

    static constexpr MStringView s_EnumBitwiseOp =
        "enum<u8> Mode {\r\n"
        "    Idle = 0,\r\n"
        "    Active = 1,\r\n"
        "}\r\n"
        "Mode BadBitwise() {\r\n"
        "    return Mode::Idle | Mode::Active;\r\n"
        "}\r\n";
    CompileResult badEnumBitwiseRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_EnumBitwiseOp, "impl/bad_enum_bitwise.adhd", arena.arena, badEnumBitwiseRes));

    static constexpr MStringView s_InvalidOperatorArity =
        "class Mask {\r\n"
        "    public u32 bits = 0;\r\n"
        "    public Mask operator~(in Mask other)const {\r\n"
        "        return Mask();\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badOpArityRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_InvalidOperatorArity, "impl/bad_op_arity.adhd", arena.arena, badOpArityRes));

    static constexpr MStringView s_MismatchedTemplateArgOverride =
        "interface IBatch {\r\n"
        "    void process(in Span<i32> items);\r\n"
        "}\r\n"
        "class BadBatch : IBatch {\r\n"
        "    public void process(in Span<f32> items) override {}\r\n"
        "}\r\n";
    CompileResult badTemplateOverrideRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MismatchedTemplateArgOverride, "impl/bad_tpl_override.adhd", arena.arena, badTemplateOverrideRes));

    static constexpr MStringView s_DuplicateFieldSource =
        "class DupClass {\r\n"
        "    public u32 value = 0;\r\n"
        "    private u32 value = 1;\r\n"
        "}\r\n";
    CompileResult dupFieldRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DuplicateFieldSource, "impl/dup_field.adhd", arena.arena, dupFieldRes));

    static constexpr MStringView s_UnknownThisMemberSource =
        "class KnownClass {\r\n"
        "    public u32 count = 0;\r\n"
        "    public u32 readBad()const {\r\n"
        "        return this.missingField;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult unknownThisRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_UnknownThisMemberSource, "impl/unknown_this.adhd", arena.arena, unknownThisRes));

    static constexpr MStringView s_InterfaceMethodByValueReturn =
        "interface IFactory {\r\n"
        "    IFactory cloneSelf();\r\n"
        "}\r\n";
    CompileResult ifaceValRetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_InterfaceMethodByValueReturn, "impl/bad_iface_ret.adhd", arena.arena, ifaceValRetRes));

    static constexpr MStringView s_TemplateArgCountMismatchSource =
        "class Pair<T, U> {\r\n"
        "    public T first = 0;\r\n"
        "    public U second = 0;\r\n"
        "}\r\n"
        "void BadPairUse() {\r\n"
        "    Pair<u8> p;\r\n"
        "}\r\n";
    CompileResult badTplArityRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_TemplateArgCountMismatchSource, "impl/bad_tpl_arity.adhd", arena.arena, badTplArityRes));

    static constexpr MStringView s_DuplicateEnumeratorSource =
        "enum<u8> DupMode {\r\n"
        "    Alpha,\r\n"
        "    Alpha\r\n"
        "}\r\n";
    CompileResult dupEnumRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DuplicateEnumeratorSource, "impl/dup_enum.adhd", arena.arena, dupEnumRes));

    static constexpr MStringView s_DuplicateLocalVarSource =
        "void BadScope(in u32 x) {\r\n"
        "    u32 x = 1u;\r\n"
        "}\r\n";
    CompileResult dupLocalRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DuplicateLocalVarSource, "impl/dup_local.adhd", arena.arena, dupLocalRes));

    static constexpr MStringView s_BreakOutsideLoopSource =
        "void BadBreak() {\r\n"
        "    break;\r\n"
        "}\r\n";
    CompileResult badBreakRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BreakOutsideLoopSource, "impl/bad_break.adhd", arena.arena, badBreakRes));

    static constexpr MStringView s_ReturnInParallelSource =
        "u32 BadParReturn() {\r\n"
        "    parallel_for (u32 i = 0u; i < 4u; ++i) {\r\n"
        "        return i;\r\n"
        "    }\r\n"
        "    return 0u;\r\n"
        "}\r\n";
    CompileResult badParRetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ReturnInParallelSource, "impl/bad_par_ret.adhd", arena.arena, badParRetRes));

    static constexpr MStringView s_VoidReturnValueSource =
        "void BadVoidRet() {\r\n"
        "    return 42;\r\n"
        "}\r\n";
    CompileResult badVoidRetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_VoidReturnValueSource, "impl/bad_void_ret.adhd", arena.arena, badVoidRetRes));
}

TEST(AdhdCompiler, PopsIncludeScopesAndOmitsTransitiveHeaderReemission){
    AdhdTestArena arena;

    class TransitiveSplitResolver final : public NWB::Core::Metascript::IIncludeResolver{
    public:
        [[nodiscard]] virtual b8 resolveInclude(const MStringView includePath, const b8, MetaArena& rArena, MString& outResolvedKey, MString& outContent)override{
            if(includePath == "split_decl.adhd"){
                outResolvedKey = MString("/abs/split_decl.adhd", rArena);
                static constexpr MStringView s_DeclCode =
                    "#include \"sub/leaf.adhd\"\r\n"
                    "i32 ComputeCrossFile(in i32 x);\r\n"
                    "class CrossBox<T> {\r\n"
                    "    public T m_item;\r\n"
                    "    public T fetch()const;\r\n"
                    "}\r\n";
                outContent = MString(s_DeclCode.data(), s_DeclCode.size(), rArena);
                return true;
            }
            if(includePath == "sub/leaf.adhd"){
                outResolvedKey = MString("/abs/sub/leaf.adhd", rArena);
                static constexpr MStringView s_LeafCode =
                    "const i32 s_LeafBias = 7;\r\n";
                outContent = MString(s_LeafCode.data(), s_LeafCode.size(), rArena);
                return true;
            }
            return false;
        }

        virtual void popIncludeScope()noexcept override{
            ++popCount;
        }


    public:
        u32 popCount = 0u;
    };

    static constexpr MStringView s_ImplSource =
        "#include \"split_decl.adhd\"\r\n"
        "i32 ComputeCrossFile(in i32 x) {\r\n"
        "    return x + s_LeafBias;\r\n"
        "}\r\n"
        "T CrossBox<T>::fetch()const {\r\n"
        "    return this.m_item;\r\n"
        "}\r\n";

    TransitiveSplitResolver resolver;
    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_ImplSource, "impl/split_impl.adhd", &resolver, arena.arena, res));

    EXPECT_EQ(resolver.popCount, 2u);
    ASSERT_EQ(res.unit.includedAdhdFiles.size(), 1u);
    EXPECT_EQ(MStringView(res.unit.includedAdhdFiles[0].data(), res.unit.includedAdhdFiles[0].size()), MStringView("split_decl.adhd"));
    ASSERT_EQ(res.unit.resolvedIncludedFiles.size(), 2u);
    EXPECT_EQ(MStringView(res.unit.resolvedIncludedFiles[0].data(), res.unit.resolvedIncludedFiles[0].size()), MStringView("/abs/split_decl.adhd"));
    EXPECT_EQ(MStringView(res.unit.resolvedIncludedFiles[1].data(), res.unit.resolvedIncludedFiles[1].size()), MStringView("/abs/sub/leaf.adhd"));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    EXPECT_NE(hdrView.find("#include \"split_decl.adhd.h\""), MStringView::npos);
    EXPECT_EQ(hdrView.find("#include \"sub/leaf.adhd.h\""), MStringView::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_index_edge_tests{


using namespace NWB::AdhdCompilerTestsDetail;
namespace Compiler = NWB::Core::Metascript::Adhd::Compiler;
using NWB::Core::Metascript::Adhd::CompileResult;
using NWB::Core::Metascript::MStringView;


TEST(AdhdCompiler, KeepsDynamicLaneIndicesDynamicWhileFoldingConstantIndices){
    AdhdTestArena arena;
    static constexpr MStringView s_Source =
        "f32 Lanes(mut VectorSingle value, in usize index) {\r\n"
        "    value[2u] = 1.0f;\r\n"
        "    value[index] = 2.0f;\r\n"
        "    return value[2u] + value[index];\r\n"
        "}\r\n";
    CompileResult result(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/lane_index_edges.adhd", arena.arena, result));
    const MStringView generated(result.generatedSource.data(), result.generatedSource.size());
    EXPECT_NE(generated.find("::VectorSetZ("), MStringView::npos);
    EXPECT_NE(generated.find("::VectorSetByIndex("), MStringView::npos);
    EXPECT_NE(generated.find("::VectorGetZ("), MStringView::npos);
    EXPECT_NE(generated.find("::VectorGetByIndex("), MStringView::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/lexer.h>
#include <core/metascript/literal_decoder.h>
#include <core/metascript/parser.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_AssetVariable = "asset";
constexpr AStringView s_NameField = "name";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Document = NWB::Core::Metascript::Document;
using Value = NWB::Core::Metascript::Value;
using MStringView = NWB::Core::Metascript::MStringView;
using AString = NWB::Tests::TestAString;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DestinationArenaTag{};

using DestinationArena = NWB::Tests::TestArena<DestinationArenaTag>;

static void CheckParseFailsWithMessage(const MStringView source, MStringView expectedMessage){
    DestinationArena arena;
    Document document(arena.arena);

    const b8 parsed = document.parse(source);
    EXPECT_FALSE(parsed);
    ASSERT_TRUE(document.hasErrors());
    ASSERT_FALSE(document.errors().empty());

    const auto& error = document.errors()[0u];
    EXPECT_EQ(MStringView(error.message), expectedMessage);
}

TEST(Metascript, EmptyAndStructOnlyDocumentsHaveNoImplicitVariables){
    DestinationArena arena;
    Document document(arena.arena);
    ASSERT_TRUE(document.parse(LiteralView("")));
    EXPECT_TRUE(document.declarations().empty());
    EXPECT_TRUE(document.structs().empty());
    EXPECT_EQ(document.findVariable(s_AssetVariable), nullptr);

    ASSERT_TRUE(document.parse(LiteralView("[custom(\"argument\")] struct Description{ [field] float value; };")));
    EXPECT_TRUE(document.declarations().empty());
    EXPECT_EQ(document.findVariable(s_AssetVariable), nullptr);
    EXPECT_EQ(document.findVariable("Description"), nullptr);
    ASSERT_EQ(document.structs().size(), 1u);
    EXPECT_NE(document.structs().find(LiteralView("Description")), document.structs().end());
}

TEST(Metascript, StructNamesAndVariablesUseSeparateNamespaces){
    DestinationArena arena;
    Document document(arena.arena);
    ASSERT_TRUE(document.parse(LiteralView("number Parameters = 3; struct Parameters{ float value; }; Parameters runtime; runtime.value = 4;")));
    ASSERT_EQ(document.structs().size(), 1u);
    ASSERT_EQ(document.declarations().size(), 2u);
    EXPECT_EQ(document.findVariable("Parameters")->asInteger(), 3);
    const auto* declaration = document.findDeclaration("runtime");
    ASSERT_NE(declaration, nullptr);
    EXPECT_EQ(declaration->type, "Parameters");
    EXPECT_EQ(declaration->variable, "runtime");
    const Value* runtime = document.findVariable("runtime");
    ASSERT_NE(runtime, nullptr);
    ASSERT_TRUE(runtime->isMap());
    EXPECT_EQ(runtime->findField("value")->asInteger(), 4);
    EXPECT_EQ(document.findVariable(s_AssetVariable), nullptr);
    EXPECT_EQ(document.findVariable("structs"), nullptr);
    EXPECT_EQ(document.findVariable("instances"), nullptr);
}

TEST(Metascript, StructMetadataOwnsSourceAndSurvivesVariableStorageGrowth){
    DestinationArena arena;
    Document document(arena.arena);
    {
        AString source("struct Description{ float value; }; Description runtime;\n");
        for(usize index = 0u; index < 128u; ++index){
            Array<char, s_DecimalTextBufferBytes> indexText = {};
            const AStringView indexView = FormatDecimal(index, indexText);
            source.append("float extra");
            source.append(indexView.data(), indexView.size());
            source.append(";\n");
        }
        ASSERT_TRUE(document.parse(source));
    }
    ASSERT_EQ(document.declarations().size(), 129u);
    const auto structure = document.structs().find(LiteralView("Description"));
    ASSERT_NE(structure, document.structs().end());
    const Value* fields = structure.value().findField("fields");
    ASSERT_NE(fields, nullptr);
    ASSERT_TRUE(fields->isList());
    ASSERT_EQ(fields->asList().size(), 1u);
    const Value* fieldName = fields->asList()[0u].findField(s_NameField);
    ASSERT_NE(fieldName, nullptr);
    EXPECT_EQ(fieldName->asString(), "value");
    EXPECT_EQ(document.findDeclaration("runtime")->type, "Description");
    ASSERT_NE(document.findVariable("runtime"), nullptr);
    EXPECT_TRUE(document.findVariable("runtime")->isNull());
}

TEST(Metascript, StructAndVariableDuplicatesAreRejectedInTheirOwnNamespaces){
    CheckParseFailsWithMessage("struct Duplicate{ float value; float value; };", "duplicate struct field declaration");
    CheckParseFailsWithMessage("struct Duplicate{}; struct Duplicate{};", "duplicate struct declaration");
    CheckParseFailsWithMessage("struct Description{}; Description runtime; Description runtime;", "duplicate variable declaration");
    CheckParseFailsWithMessage("number value; text value;", "duplicate variable declaration");
}

TEST(Metascript, ReparsingClearsStructsVariablesDeclarationsAndErrors){
    DestinationArena arena;
    Document document(arena.arena);
    ASSERT_TRUE(document.parse(LiteralView("struct Old{ float value; }; Old previous;")));
    ASSERT_FALSE(document.parse(LiteralView("struct Partial{}; number value; number value;")));
    ASSERT_TRUE(document.hasErrors());
    ASSERT_TRUE(document.parse(LiteralView("text current = \"new\";")));
    EXPECT_FALSE(document.hasErrors());
    EXPECT_TRUE(document.structs().empty());
    ASSERT_EQ(document.declarations().size(), 1u);
    EXPECT_EQ(document.findDeclaration("previous"), nullptr);
    EXPECT_EQ(document.findVariable("previous"), nullptr);
    EXPECT_EQ(document.findVariable("value"), nullptr);
    EXPECT_EQ(document.findVariable("current")->asString(), "new");
    ASSERT_TRUE(document.parse(LiteralView("")));
    EXPECT_TRUE(document.declarations().empty());
    EXPECT_EQ(document.findVariable("current"), nullptr);
}

TEST(Metascript, InvalidAssignmentTargetsPreserveDiagnosticsAndDeclaredValue){
    struct InvalidTargetCase{
        MStringView source;
        MStringView message;
        i64 retainedValue;
    };
    const Array<InvalidTargetCase, 2u> cases = {
        InvalidTargetCase{ "float asset = 7;\nmissing.value = 2;", "assignments must target a declared variable", 7 },
        InvalidTargetCase{ "float asset = 9;\nasset.value = 2;", "cannot access field on non-map value", 9 },
    };
    for(const auto& testCase : cases){
        SCOPED_TRACE(testCase.message.data());
        DestinationArena arena;
        Document document(arena.arena);
        EXPECT_FALSE(document.parse(testCase.source));
        ASSERT_EQ(document.errors().size(), 1u);
        EXPECT_EQ(document.errors()[0u].line, 2u);
        EXPECT_EQ(document.errors()[0u].column, 1u);
        EXPECT_EQ(MStringView(document.errors()[0u].message), testCase.message);
        const Value* asset = document.findVariable(s_AssetVariable);
        ASSERT_NE(asset, nullptr);
        ASSERT_TRUE(asset->isInteger());
        EXPECT_EQ(asset->asInteger(), testCase.retainedValue);
        EXPECT_EQ(document.findVariable("missing"), nullptr);
    }
}


TEST(Metascript, UnicodeCharLiteralRejectsWideValueBeforeNarrowing){
    using NWB::Core::Metascript::LiteralDecoder;
    constexpr b8 rejectsWide = []()noexcept{
        i64 value = 7;
        return !LiteralDecoder::TryDecodeCharLiteral("'\\U100000000'", value) && value == 7;
    }();
    static_assert(rejectsWide);

    i64 value = 7;
    EXPECT_FALSE(LiteralDecoder::TryDecodeCharLiteral("'\\UD800'", value));
    EXPECT_EQ(value, 7);
    EXPECT_FALSE(LiteralDecoder::TryDecodeCharLiteral("'\\U110000'", value));
    EXPECT_EQ(value, 7);
    EXPECT_TRUE(LiteralDecoder::TryDecodeCharLiteral("'\\U10FFFF'", value));
    EXPECT_EQ(value, 0x10FFFF);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/parser.h>

#include <tests/common/test_context.h>
#include <tests/unit/metascript/generated/compact_string.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_value_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ItemsField = NWB::Tests::s_MetascriptValueItemsField;
using Value = NWB::Core::Metascript::Value;
using MStringView = NWB::Core::Metascript::MStringView;
using AString = NWB::Tests::TestAString;
struct SourceArenaTag{};
struct DestinationArenaTag{};
using SourceArena = NWB::Tests::TestArena<SourceArenaTag>;
using DestinationArena = NWB::Tests::TestArena<DestinationArenaTag>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CheckStringListElement(const Value& value, const MStringView expected){
    ASSERT_TRUE(value.isString());
    EXPECT_EQ(value.asString(), expected);
}

static void CheckSingleStringListValue(const Value& value, const MStringView text){
    ASSERT_TRUE(value.isList());
    ASSERT_EQ(value.asList().size(), 1u);
    CheckStringListElement(value.asList()[0u], text);
}

template<typename ArenaT>
static void MakeSingleStringList(Value& list, ArenaT& arena, const MStringView text){
    list.makeList();
    list.append(Value(text, arena.arena));
}

TEST(Metascript, CrossArenaMoveAssignmentCopiesIntoDestinationArena){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'm');

    Value source(text, sourceArena.arena);
    Value destination(destinationArena.arena);

    destination = Move(source);

    EXPECT_TRUE(source.isNull());
    EXPECT_EQ(destination.asString(), text);
}

TEST(Metascript, CrossArenaCopyAssignmentCopiesNestedValuesIntoDestinationArena){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'c');

    Value source(sourceArena.arena);
    source.makeMap();
    Value& list = source.field(s_ItemsField);
    MakeSingleStringList(list, sourceArena, text);

    Value destination(destinationArena.arena);

    destination = source;

    EXPECT_TRUE(source.isMap());
    const Value* copiedList = destination.findField(s_ItemsField);
    ASSERT_NE(copiedList, nullptr);
    CheckSingleStringListValue(*copiedList, text);
}

TEST(Metascript, CrossArenaListConcatCopiesIntoResultArena){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'p');

    Value source(sourceArena.arena);
    MakeSingleStringList(source, sourceArena, text);

    Value destination(destinationArena.arena);
    destination.makeList();

    Value result = destination + source;

    CheckSingleStringListValue(result, text);
}

TEST(Metascript, CrossArenaAppendCopiesIntoDestinationArena){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'a');

    Value source(text, sourceArena.arena);
    Value destination(destinationArena.arena);
    destination.makeList();

    destination.append(Move(source));

    EXPECT_TRUE(source.isNull());
    CheckSingleStringListValue(destination, text);
}

TEST(Metascript, ListSelfAppendCopiesOriginalValues){
    DestinationArena arena;
    const AString text(128u, 's');

    Value list(arena.arena);
    MakeSingleStringList(list, arena, text);

    list += list;

    ASSERT_EQ(list.asList().size(), 2u);
    CheckStringListElement(list.asList()[0u], text);
    CheckStringListElement(list.asList()[1u], text);
}

TEST(Metascript, AppendSelfMoveCopiesOriginalValue){
    DestinationArena arena;
    const AString text(128u, 'v');

    Value list(arena.arena);
    MakeSingleStringList(list, arena, text);

    list.append(Move(list));

    ASSERT_TRUE(list.isList());
    ASSERT_EQ(list.asList().size(), 2u);
    CheckStringListElement(list.asList()[0u], text);
    ASSERT_TRUE(list.asList()[1u].isList());
    ASSERT_EQ(list.asList()[1u].asList().size(), 1u);
    const Value& nestedText = list.asList()[1u].asList()[0u];
    CheckStringListElement(nestedText, text);
}

TEST(Metascript, AppendExistingListElementMoveCopiesBeforeDestroy){
    DestinationArena arena;
    const AString text(128u, 'e');

    Value list(arena.arena);
    MakeSingleStringList(list, arena, text);

    list.append(Move(list.asList()[0u]));

    ASSERT_TRUE(list.isList());
    ASSERT_EQ(list.asList().size(), 2u);
    EXPECT_TRUE(list.asList()[0u].isNull());
    CheckStringListElement(list.asList()[1u], text);
}

TEST(Metascript, ListAppendExistingElementCopiesBeforeReallocation){
    DestinationArena arena;
    const AString text(128u, 'r');

    Value list(arena.arena);
    MakeSingleStringList(list, arena, text);

    list += list.asList()[0u];

    ASSERT_TRUE(list.isList());
    ASSERT_EQ(list.asList().size(), 2u);
    CheckStringListElement(list.asList()[0u], text);
    CheckStringListElement(list.asList()[1u], text);
}

TEST(Metascript, AppendsFirstMiddleAndLastElementsAcrossReallocation){
    constexpr usize s_InitialCount = NWB::Tests::s_MetascriptValueInitialCount;
    const Array<usize, 3u> sourceIndices = { 0u, 3u, 7u };
    for(const usize sourceIndex : sourceIndices){
        for(const b8 moveSource : { false, true }){
            DestinationArena arena;
            Value list(arena.arena);
            list.makeList();
            list.asList().reserve(s_InitialCount);
            const usize originalCount = list.asList().capacity();
            for(usize index = 0u; index < originalCount; ++index)
                list.append(Value(static_cast<i64>(index), arena.arena));

            if(moveSource)
                list.append(Move(list.asList()[sourceIndex]));
            else
                list += list.asList()[sourceIndex];

            ASSERT_EQ(list.asList().size(), originalCount + 1u);
            EXPECT_EQ(list.asList().back().asInteger(), static_cast<i64>(sourceIndex));
            for(usize index = 0u; index < originalCount; ++index){
                if(moveSource && index == sourceIndex)
                    EXPECT_TRUE(list.asList()[index].isNull());
                else
                    EXPECT_EQ(list.asList()[index].asInteger(), static_cast<i64>(index));
            }
        }
    }
}

TEST(Metascript, ConcatenatesNestedListElementsAcrossReallocation){
    constexpr usize s_InitialCount = NWB::Tests::s_MetascriptValueInitialCount;
    const Array<usize, 3u> sourceIndices = { 0u, 3u, 7u };
    const AString text(128u, 'n');
    for(const usize sourceIndex : sourceIndices){
        DestinationArena arena;
        Value list(arena.arena);
        list.makeList();
        list.asList().reserve(s_InitialCount);
        const usize originalCount = list.asList().capacity();
        for(usize index = 0u; index < originalCount; ++index)
            list.append(Value(static_cast<i64>(index), arena.arena));
        MakeSingleStringList(list.asList()[sourceIndex], arena, text);

        list += list.asList()[sourceIndex];

        ASSERT_EQ(list.asList().size(), originalCount + 1u);
        EXPECT_GT(list.asList().capacity(), originalCount);
        CheckStringListElement(list.asList().back(), text);
        for(usize index = 0u; index < originalCount; ++index){
            if(index == sourceIndex)
                CheckSingleStringListValue(list.asList()[index], text);
            else
                EXPECT_EQ(list.asList()[index].asInteger(), static_cast<i64>(index));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/parser.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_reader_failure_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Metascript;

struct ReaderArenaTag{};
using ReaderArena = NWB::Tests::TestArena<ReaderArenaTag>;

class UnexpectedReadFailure final : public GeneralException{
public:
    [[nodiscard]] virtual const char* what()const noexcept override{ return "unexpected reader failure"; }
};

class ThrowingReader final : public IMetaReader{
public:
    [[nodiscard]] virtual Expected<usize> read(MChar*, usize)override{ throw UnexpectedReadFailure{}; }
};

class EndOfFileReader final : public IMetaReader{
public:
    [[nodiscard]] virtual Expected<usize> read(MChar*, usize)noexcept override{ return 0u; }
};

class RejectedReader final : public IMetaReader{
public:
    explicit RejectedReader(const b8 oversized)noexcept
        : m_oversized(oversized)
    {}


public:
    [[nodiscard]] virtual Expected<usize> read(MChar*, const usize maxBytes)noexcept override{
        if(m_oversized)
            return maxBytes + 1u;
        return MakeUnexpected(Failure{});
    }


private:
    b8 m_oversized;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Metascript, UnexpectedReaderExceptionsPropagateWithoutBecomingParseErrors){
    ReaderArena arena;
    Document document(arena.arena);
    ThrowingReader reader;

    EXPECT_THROW(EXPECT_TRUE(document.parse(reader)), UnexpectedReadFailure);
    EXPECT_FALSE(document.hasErrors());
}

TEST(Metascript, ReportedReaderFailureRemainsAnExplicitParseError){
    ReaderArena arena;
    Document document(arena.arena);
    RejectedReader reader(false);

    EXPECT_FALSE(document.parse(reader));
    ASSERT_EQ(document.errors().size(), 1u);
    EXPECT_EQ(document.errors()[0u].message, "read error");
}

TEST(Metascript, SuccessfulZeroReaderCountIsEndOfFileInsteadOfReadFailure){
    ReaderArena arena;
    Document document(arena.arena);
    EndOfFileReader reader;

    ASSERT_TRUE(document.parse(LiteralView("struct Previous{}; number prior;")));
    EXPECT_TRUE(document.parse(reader));
    EXPECT_FALSE(document.hasErrors());
    EXPECT_TRUE(document.declarations().empty());
    EXPECT_TRUE(document.structs().empty());
    EXPECT_EQ(document.findVariable("prior"), nullptr);
}

TEST(Metascript, OversizedReaderResultIsRejectedBeforeUsingItsBuffer){
    ReaderArena arena;
    Document document(arena.arena);
    RejectedReader reader(true);

    EXPECT_FALSE(document.parse(reader));
    ASSERT_EQ(document.errors().size(), 1u);
    EXPECT_EQ(document.errors()[0u].message, "reader returned more bytes than requested");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_METASCRIPT_GENERATED_TESTS)

#include <tests/unit/metascript/generated/reflection_container.adhd.h>

#include <core/metascript/value.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_container_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AdhdReflection, RejectsInvalidContainerEntriesWithoutPublishingEmptyNames){
    NWB::AdhdCompilerTestsDetail::AdhdTestArena arena;
    using NWB::Core::Metascript::MStringView;
    using NWB::Core::Metascript::Value;
    constexpr Array<char, 3u> invalidBytes = { 'b', '\0', 'd' };
    const MStringView invalid(invalidBytes.data(), invalidBytes.size());
    Value input(arena.arena);
    auto& names = input.field("names");
    names.append(Value(MStringView("first"), arena.arena));
    names.append(Value(invalid, arena.arena));
    names.append(Value(i64(7), arena.arena));
    names.append(Value(MStringView("last"), arena.arena));
    auto& fixed = input.field("fixedNames");
    fixed.append(Value(MStringView("fixed/first"), arena.arena));
    fixed.append(Value(invalid, arena.arena));
    fixed.append(Value(MStringView("fixed/last"), arena.arena));
    auto& bounded = input.field("boundedNames");
    bounded.append(Value(invalid, arena.arena));
    bounded.append(Value(MStringView("bounded/first"), arena.arena));
    bounded.append(Value(MStringView("bounded/last"), arena.arena));
    auto& counts = input.field("counts");
    counts.field("valid").setInteger(9);
    counts.field(invalid).setInteger(3);
    counts.field("wrong_type").setString("not a number");
    auto& assets = input.field("assets");
    assets.field("valid").setString("project/asset");
    assets.field(invalid).setString("project/ignored");
    assets.field("invalid_value").setString(invalid);
    assets.field("wrong_type").setInteger(3);
    auto& enumAssets = input.field("enumAssets");
    enumAssets.field("Valid").setString("project/enum_asset");
    enumAssets.field("unknown").setString("project/ignored_enum");

    NWB::Tests::ReflectionContainerFixture value;
    value.fixedNames[1u] = Name("fixed/retained");
    ASSERT_TRUE(value.deserializeNwb(input));
    ASSERT_EQ(value.names.size(), 2u);
    EXPECT_EQ(value.names[0u], Name("first"));
    EXPECT_EQ(value.names[1u], Name("last"));
    EXPECT_EQ(value.fixedNames[0u], Name("fixed/first"));
    EXPECT_EQ(value.fixedNames[1u], Name("fixed/retained"));
    EXPECT_EQ(value.fixedNames[2u], Name("fixed/last"));
    ASSERT_EQ(value.boundedNames.size(), 2u);
    EXPECT_EQ(value.boundedNames[0u], Name("bounded/first"));
    EXPECT_EQ(value.boundedNames[1u], Name("bounded/last"));
    ASSERT_EQ(value.counts.size(), 1u);
    const auto count = value.counts.find(Name("valid"));
    ASSERT_NE(count, value.counts.end());
    EXPECT_EQ(count.value(), 9u);
    ASSERT_EQ(value.assets.size(), 1u);
    const auto asset = value.assets.find(Name("valid"));
    ASSERT_NE(asset, value.assets.end());
    EXPECT_EQ(asset.value().name(), Name("project/asset"));
    ASSERT_EQ(value.enumAssets.size(), 1u);
    Vector<Name, NWB::Core::Metascript::MetaArena> dependencies(arena.arena);
    ASSERT_TRUE(NWB::Tests::ReflectionContainerFixture::extractNwbAssetDependencies(input, [&](const Name& name){ dependencies.push_back(name); }));
    ASSERT_EQ(dependencies.size(), 2u);
    EXPECT_NE(FindIf(dependencies.begin(), dependencies.end(), [](const Name& name)noexcept{ return name == Name("project/asset"); }), dependencies.end());
    EXPECT_NE(FindIf(dependencies.begin(), dependencies.end(), [](const Name& name)noexcept{ return name == Name("project/enum_asset"); }), dependencies.end());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/metascript/generated/compact_string.adhd.h>
#include <tests/unit/metascript/generated/builtin_shadow_global.adhd.h>

#include <global/floating_point_environment.h>
#include <tests/common/floating_point_environment_guard.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AdhdCompactString, SearchAndBoundariesUseCanonicalStoredText){
    const ACompactString value("Find\\NEEDLE");
    EXPECT_EQ(NWB::Tests::FindCompact(value), 5u);
    EXPECT_EQ(NWB::Tests::FirstCompact(value), static_cast<u8>('f'));
    EXPECT_EQ(NWB::Tests::LastCompact(value), static_cast<u8>('e'));
    EXPECT_EQ(NWB::Tests::FindCompact(ACompactString("missing")), ACompactString::s_NPos);
}


TEST(AdhdCompiler, ScalarLerpEvaluatesSideEffectOnce){
    i32 counter = 1;
    EXPECT_FLOAT_EQ(NWB::Tests::LerpWithCounter(counter), 6.0f);
    EXPECT_EQ(counter, 2);
}


struct LerpMoveOnly{
    i32 value = 10;
    LerpMoveOnly() = default;
    explicit LerpMoveOnly(i32 v) : value(v){}
    LerpMoveOnly(const LerpMoveOnly&) = delete;
    LerpMoveOnly(LerpMoveOnly&&) = default;
    LerpMoveOnly operator-(const LerpMoveOnly& rhs)&&{ return LerpMoveOnly(value - rhs.value); }
    LerpMoveOnly operator*(LerpMoveOnly&& rhs)&&{ return LerpMoveOnly(value * rhs.value); }
    LerpMoveOnly operator+(LerpMoveOnly&& rhs)&{ return LerpMoveOnly(value + rhs.value); }
    LerpMoveOnly operator+(LerpMoveOnly&& rhs)&&{ return LerpMoveOnly(value + rhs.value + 100); }
};

TEST(AdhdCompiler, ScalarLerpPreservesMoveOnlyOperandCategories){
    LerpMoveOnly value(2);
    const LerpMoveOnly result = NWB::Tests::LerpMoveOnly(value);
    EXPECT_EQ(result.value, 82);
    EXPECT_EQ(value.value, 2);
}


TEST(AdhdCompiler, UserCallableShadowsBuiltinInGeneratedCode){
    EXPECT_EQ(NWB::Tests::CallShadowedMax(3, 7), -4);
}


TEST(AdhdCompiler, UserCallableShadowsBuiltinForSimd){
    EXPECT_FLOAT_EQ(NWB::Tests::CallShadowedVectorMax(), -4.0f);
}


TEST(AdhdCompiler, ResolvedBuiltinStillUsesSimdImplementation){
    EXPECT_FLOAT_EQ(NWB::Tests::CallBuiltinVectorMin(), 3.0f);
}


TEST(AdhdCompiler, LocalCallableShadowsBuiltin){
    EXPECT_EQ(NWB::Tests::CallShadowedLocalMin(), 17);
}


TEST(AdhdCompiler, NestedSimdCallKeepsUserCallableIdentity){
    EXPECT_FLOAT_EQ(NWB::Tests::CallNestedShadowedVector(), -4.0f);
}


TEST(AdhdCompiler, TransposeDoesNotFuseUserDefinedMultiply){
    EXPECT_FLOAT_EQ(NWB::Tests::CallShadowedMatrixMul(), 3.0f);
}


TEST(AdhdCompiler, LocalCallableShadowsSimdReduction){
    EXPECT_FLOAT_EQ(NWB::Tests::CallShadowedLocalDot(), -4.0f);
}


TEST(AdhdCompiler, LocalParameterShadowsBuiltinConstant){
    EXPECT_EQ(NWB::Tests::CallShadowedEntityConstant(37), 37);
}


TEST(AdhdCompiler, UnshadowedBuiltinConstantStillResolves){
    EXPECT_TRUE(NWB::Tests::ReadBuiltinEntityConstant() == NWB::Core::ECS::s_InvalidEntityId);
}


TEST(AdhdCompiler, GlobalConstantShadowsBuiltinInTypeInference){
    EXPECT_EQ(NWB::Tests::ReadShadowedGlobalConstant(), 44);
}


TEST(AdhdCompiler, ScalarLerpKeepsFirstTemporaryCategory){
    const LerpMoveOnly result = NWB::Tests::LerpTemporary<LerpMoveOnly>();
    EXPECT_EQ(result.value, 110);
}


TEST(AdhdCompiler, TrimViewBorrowsLocalCompactString){
    EXPECT_EQ(NWB::Tests::TrimLocalCompact(), 7u);
}


TEST(AdhdCompiler, StringAndCompactSubstrPreserveOwnershipAndSingleEvaluation){
    i32 recvCounter = 0;
    i32 posCounter = 0;
    i32 countCounter = 0;
    const AString result = NWB::Tests::ExtractLongStringSubstrOnce(recvCounter, posCounter, countCounter);
    EXPECT_EQ(recvCounter, 1);
    EXPECT_EQ(posCounter, 1);
    EXPECT_EQ(countCounter, 1);
    EXPECT_EQ(result.size(), 72u);
    EXPECT_EQ(AStringView(result.data(), result.size()), AStringView("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_tail_segm"));

    const ACompactString compact("Header/SubstrPayload/Tail");
    const ACompactString sliced = NWB::Tests::ExtractCompactSubstr(compact, 7u, 13u);
    EXPECT_EQ(sliced.view(), AStringView("substrpayload"));
}


TEST(AdhdCompiler, HighByteFrontBackAndIndexMaintainUnsignedSemanticsAndMutation){
    static constexpr char s_ViewBytes[] = { static_cast<char>(0x80u), static_cast<char>(0x7fu), static_cast<char>(0xffu) };
    static constexpr char s_CompactBytes[] = { static_cast<char>(0x80u), static_cast<char>(0xfeu), '\0' };
    const AStringView viewValue(s_ViewBytes, sizeof(s_ViewBytes));
    const ACompactString compactValue(s_CompactBytes);

    const u32 expected = (0x80u + 0xffu + 0x80u + 0xfeu)
        + static_cast<u32>(1000 + 0x80)
        + static_cast<u32>(2000 + 0xfe)
        + (0x40u + 0x81u + 0x80u);
    EXPECT_EQ(NWB::Tests::ExerciseHighByteStringOps(viewValue, compactValue), expected);
    EXPECT_EQ(NWB::Tests::BorrowLocalStringAcrossValueCall(), 76u + 72u);
}


TEST(AdhdCompiler, SimdLaneComparisonAndFiniteChecksIgnoreInactiveAndShortCircuitedSignalingNaNs){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    const NWB::Tests::FloatingPointEnvironmentGuard restoreEnvironment(*originalEnvironment);

    volatile u32 snan32 = 0x7f800123u;
    volatile u64 snan64 = 0x7ff0000000000123ull;
    const SIMDVectorSingle inactive32 = VectorSetInt(0u, 0u, 0u, snan32);
    const SIMDVectorSingle shortCircuit32 = VectorSetInt(0x3f800000u, snan32, 0u, snan32);
    const SIMDVectorSingle active32 = VectorSetInt(0u, snan32, 0u, 0u);
    const SIMDVectorDouble inactive64 = VectorDoubleSetInt(0ull, 0ull, 0ull, snan64);
    const SIMDVectorDouble shortCircuit64 = VectorDoubleSetInt(0x3ff0000000000000ull, snan64, 0ull, snan64);
    const SIMDVectorDouble active64 = VectorDoubleSetInt(0ull, snan64, 0ull, 0ull);

    for(const i32 roundingMode : { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO }){
        ASSERT_TRUE(SetFloatingPointRoundingMode(roundingMode));

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_FALSE(NWB::Tests::AnyLaneGreater3Single(inactive32, VectorZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_TRUE(NWB::Tests::AnyLaneGreater3Single(shortCircuit32, VectorZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_FALSE(NWB::Tests::AnyLaneGreater3Single(active32, VectorZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT) & FE_INVALID, FE_INVALID);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_FALSE(NWB::Tests::AnyLaneGreater3Double(inactive64, VectorDoubleZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_TRUE(NWB::Tests::AnyLaneGreater3Double(shortCircuit64, VectorDoubleZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_FALSE(NWB::Tests::AnyLaneGreater3Double(active64, VectorDoubleZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT) & FE_INVALID, FE_INVALID);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_TRUE(NWB::Tests::AnyLaneGreater2Single(shortCircuit32, VectorZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_TRUE(NWB::Tests::FiniteLane3Single(inactive32));
        EXPECT_FALSE(NWB::Tests::FiniteLane3Single(active32));
        EXPECT_TRUE(NWB::Tests::FiniteLane3Double(inactive64));
        EXPECT_FALSE(NWB::Tests::FiniteLane3Double(active64));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif
