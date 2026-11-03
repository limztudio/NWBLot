// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


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
        "i32 leak(in SecretBox b) {\r\n"
        "    return b.hidden;\r\n"
        "}\r\n";
    CompileResult privFieldRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_PrivateFieldSource, "impl/sim/priv_field.adhd", arena.arena, privFieldRes));

    static constexpr MStringView s_PrivateMethodSource =
        "class SecretBox {\r\n"
        "    private i32 calc()const { return 7; }\r\n"
        "}\r\n"
        "i32 leak(in SecretBox b) {\r\n"
        "    return b.calc();\r\n"
        "}\r\n";
    CompileResult privMethodRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_PrivateMethodSource, "impl/sim/priv_method.adhd", arena.arena, privMethodRes));

    static constexpr MStringView s_PrivateCtorSource =
        "class SecretBox {\r\n"
        "    public i32 v = 0;\r\n"
        "    private SecretBox(in i32 x) { this.v = x; }\r\n"
        "}\r\n"
        "void make() {\r\n"
        "    SecretBox b(5);\r\n"
        "}\r\n";
    CompileResult privCtorRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_PrivateCtorSource, "impl/sim/priv_ctor.adhd", arena.arena, privCtorRes));

    static constexpr MStringView s_AutoInterfaceByValueSource =
        "interface INode {\r\n"
        "    i32 id()const;\r\n"
        "}\r\n"
        "void badAuto(in INode n) {\r\n"
        "    auto copy = n;\r\n"
        "}\r\n";
    CompileResult autoIfaceRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_AutoInterfaceByValueSource, "impl/sim/auto_iface.adhd", arena.arena, autoIfaceRes));

    static constexpr MStringView s_MutRebindConstSource =
        "interface INode {\r\n"
        "    void step();\r\n"
        "}\r\n"
        "class NodeImpl : INode {\r\n"
        "    public void step() override {}\r\n"
        "}\r\n"
        "void badRebind(mut NodeImpl a, in NodeImpl b) {\r\n"
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
        "void usePacket(in Packet<Vector<u32>> p) {}\r\n";
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
        "u32 consumeIds(in Vector<u32> ids) {\r\n"
        "    return cast<u32>(ids.size());\r\n"
        "}\r\n"
        "\r\n"
        "u32 testMutLambdaCaptureSuppressesMove() {\r\n"
        "    Vector<u32> captured;\r\n"
        "    auto mutator = lambda[mut captured]() -> void {\r\n"
        "        captured.push_back(1u);\r\n"
        "    };\r\n"
        "    return consumeIds(captured);\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/alias_ool.adhd", arena.arena, res));

    const MStringView srcView(res.generatedSource.data(), res.generatedSource.size());
    EXPECT_NE(srcView.find("return consumeIds(captured);"), MStringView::npos);
    EXPECT_EQ(srcView.find("return consumeIds(::Move(captured));"), MStringView::npos);

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
        "i32 readFrom<T : ISource>(in T src) {\r\n"
        "    return src.read();\r\n"
        "}\r\n"
        "void badCall(in Unrelated u) {\r\n"
        "    readFrom(u);\r\n"
        "}\r\n";
    CompileResult badDeducedRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DeducedBoundViolationSource, "impl/sim/bad_deduced.adhd", arena.arena, badDeducedRes));

    static constexpr MStringView s_WrongArgCountSource =
        "i32 addPair(in i32 a, in i32 b) {\r\n"
        "    return a + b;\r\n"
        "}\r\n"
        "void badArity() {\r\n"
        "    addPair(1);\r\n"
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
        "u32 evalFlags(in RenderFlags f) {\r\n"
        "    Array<u32, (kBaseCount + 2u)> lut = Array<u32, (kBaseCount + 2u)>();\r\n"
        "    RenderFlags combined = f | RenderFlags::Opaque;\r\n"
        "    combined |= RenderFlags::Shadow;\r\n"
        "    return cast<u32>(combined) + kSepDec + kSepHex + kSepBin + cast<u32>(lut.size());\r\n"
        "}\r\n";
    CompileResult validRes(arena.arena);
    EXPECT_TRUE(Compiler::Compile(s_ValidEdgeCasesSource, "impl/sim/valid_edge_cases.adhd", arena.arena, validRes));

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
        "void mixFlags() {\r\n"
        "    auto x = FlagA::Bit0 | FlagB::Bit0;\r\n"
        "}\r\n";
    CompileResult badFlagMixRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadFlagMismatchSource, "impl/sim/bad_flag_mix.adhd", arena.arena, badFlagMixRes));

    static constexpr MStringView s_BadEnumIncrementSource =
        "enum<u32> Step { One = 1u, Two = 2u }\r\n"
        "void incEnum() {\r\n"
        "    Step s = Step::One;\r\n"
        "    ++s;\r\n"
        "}\r\n";
    CompileResult badEnumIncRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadEnumIncrementSource, "impl/sim/bad_enum_inc.adhd", arena.arena, badEnumIncRes));

    static constexpr MStringView s_BadDupConstCaseSource =
        "i32 badSwitch(in i32 v) {\r\n"
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
        "void badInBind(in i32 a, in i32 b) {\r\n"
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
        "b8 checkFirst<T : IComparable<T>>(in Span<T> items, in T target) {\r\n"
        "    if(items.empty())\r\n"
        "        return false;\r\n"
        "    return items[0u].equals(target);\r\n"
        "}\r\n"
        "b8 useDeducedSpan(in Span<Item> items, in Item target) {\r\n"
        "    return checkFirst(items, target) && (kHexFloat < kTrailingDot);\r\n"
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
        "void makeSecret() {\r\n"
        "    Secret s;\r\n"
        "}\r\n";
    CompileResult badPrivCtorRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadPrivateDefaultCtorSource, "impl/sim/bad_priv_ctor.adhd", arena.arena, badPrivCtorRes));

    static constexpr MStringView s_BadConflictingDeducedSource =
        "void sameType<T>(in T a, in T b) {}\r\n"
        "void callMismatch() {\r\n"
        "    sameType(1, \"hello\");\r\n"
        "}\r\n";
    CompileResult badConflictRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_BadConflictingDeducedSource, "impl/sim/bad_conflict.adhd", arena.arena, badConflictRes));

    static constexpr MStringView s_BadSwitchContainerSource =
        "void badSwitchVec(in Vector<i32> v) {\r\n"
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
        "Box<T> wrapInBox<T>(in T v) {\r\n"
        "    return Box<T>(v);\r\n"
        "}\r\n"
        "[reflect]\r\n"
        "class NameTable {\r\n"
        "    public Vector<Name> names;\r\n"
        "    public HashMap<Name, u32> lookup;\r\n"
        "}\r\n"
        "usize hashSimdAndWrap(in f32x4 a, in f32x4 b) {\r\n"
        "    auto wrapped = wrapInBox(42u);\r\n"
        "    f32x4 sum = a + b;\r\n"
        "    return hashCombineValue(usize(wrapped.value), sum);\r\n"
        "}\r\n";
    CompileResult instSimdRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_DeducedInstantiationAndSimdHashSource, "impl/sim/inst_simd.adhd", arena.arena, instSimdRes));
    const MStringView instHdr(instSimdRes.generatedHeader.data(), instSimdRes.generatedHeader.size());
    const MStringView instSrc(instSimdRes.generatedSource.data(), instSimdRes.generatedSource.size());
    EXPECT_NE(instHdr.find("template<>\r\nclass Box<u32> final"), MStringView::npos);
    EXPECT_NE(instSrc.find("::StoreFloat(__adhd_simd_sum, sum);"), MStringView::npos);
    EXPECT_NE(instSrc.find("::HashCombineValue(usize(wrapped.value), sum)"), MStringView::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

