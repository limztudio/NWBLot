// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AdhdInplaceFunction, StatelessZeroByteAndStatefulTrampolines){
    using StatelessFn = InplaceFunction<0u, i32(i32, i32)>;
    static_assert(sizeof(StatelessFn) == sizeof(void*), "Stateless InplaceFunction<0> must be 1 pointer wide");

    StatelessFn addFn([](const i32 a, const i32 b) -> i32{ return a + b; });
    ASSERT_TRUE(static_cast<b8>(addFn));
    EXPECT_EQ(addFn(10, 22), 32);

    const StatelessFn copiedStateless = addFn;
    EXPECT_EQ(copiedStateless(5, 7), 12);

    i32(*rawPtr)(i32, i32) = addFn.rawFn();
    ASSERT_NE(rawPtr, nullptr);
    EXPECT_EQ(rawPtr(3, 4), 7);

    auto cFirst = addFn.cTrampolineFirst();
    EXPECT_EQ(cFirst(addFn.userData(), 8, 9), 17);

    i32 accumulator = 100;
    InplaceFunction<64u, i32(i32)> statefulFn([&accumulator](const i32 delta) -> i32{
        accumulator += delta;
        return accumulator;
    });
    ASSERT_TRUE(static_cast<b8>(statefulFn));
    EXPECT_EQ(statefulFn(15), 115);

    auto statefulTrampoline = statefulFn.cTrampolineLast();
    EXPECT_EQ(statefulTrampoline(10, statefulFn.userData()), 125);
}

TEST(AdhdPreprocessor, DeduplicatesIncludesAndSupportsMacrosInNwbAndAdhd){
    AdhdTestArena arena;

    static constexpr MStringView s_NwbSource =
        "#define ASSET_SCALE 4\r\n"
        "#if ASSET_SCALE == 4\r\n"
        "number scale = ASSET_SCALE;\r\n"
        "#endif\r\n";

    NWB::Core::Metascript::Document doc(arena.arena);
    ASSERT_TRUE(doc.parse(s_NwbSource));
    ASSERT_EQ(doc.declarations().size(), 1u);
    ASSERT_NE(doc.findVariable("scale"), nullptr);
    EXPECT_EQ(doc.findVariable("scale")->asInteger(), 4);

    MockIncludeResolver resolver;
    static constexpr MStringView s_AdhdSource =
        "#include \"shared_defs.adhd\"\r\n"
        "#include \"shared_defs.adhd\"\r\n"
        "#define MUL_ADD(a, b, c) ((a) * (b) + (c))\r\n"
        "u32 compute(in u32 x) {\r\n"
        "    return MUL_ADD(x, 2, kSharedLimit);\r\n"
        "}\r\n";

    CompileResult result(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_AdhdSource, "impl/math/compute.adhd", arena.arena, result, &resolver));
    EXPECT_EQ(resolver.resolveCallCount, 1u);
}

TEST(AdhdCompiler, ReordersClassFieldsByDescendingAlignmentAndSize){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "enum<u8> NodeFlags {\r\n"
        "    flag Active,\r\n"
        "    flag Visible,\r\n"
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
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/scene/transform_node.adhd", arena.arena, result));
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
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/assets/binary_header.adhd", arena.arena, result));
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
    ASSERT_TRUE(Compiler::compile(s_SimdOrderSource, "impl/assets/simd_payload.adhd", arena.arena, simdResult));
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

    static constexpr MStringView s_ValidSource =
        "interface ITask {\r\n"
        "    void execute(in f32 dt);\r\n"
        "}\r\n"
        "class PhysicsTask : ITask {\r\n"
        "    private f32 scale = 1.0f;\r\n"
        "    public void execute(in f32 dt) override {\r\n"
        "        this.scale = dt;\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult validRes(arena.arena);
    EXPECT_TRUE(Compiler::compile(s_ValidSource, "impl/task/physics_task.adhd", arena.arena, validRes));

    static constexpr MStringView s_MissingOverrideSource =
        "interface ITask {\r\n"
        "    void execute(in f32 dt);\r\n"
        "}\r\n"
        "class BadTask : ITask {\r\n"
        "    public void execute(in f32 dt) {\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult missingOverrideRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_MissingOverrideSource, "impl/task/bad_task.adhd", arena.arena, missingOverrideRes));

    static constexpr MStringView s_ClassInheritanceSource =
        "class BaseClass {\r\n"
        "    public u32 x = 0;\r\n"
        "}\r\n"
        "class DerivedClass : BaseClass {\r\n"
        "    public u32 y = 1;\r\n"
        "}\r\n";

    CompileResult classInheritRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_ClassInheritanceSource, "impl/task/derived.adhd", arena.arena, classInheritRes));
}

TEST(AdhdCompiler, EnforcesMandatoryInitializationAndProhibitions){
    AdhdTestArena arena;

    static constexpr MStringView s_UninitLocal =
        "void testFn(in u32 a) {\r\n"
        "    u32 count;\r\n"
        "}\r\n";
    CompileResult res1(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_UninitLocal, "impl/test.adhd", arena.arena, res1));

    static constexpr MStringView s_DoubleInitCtor =
        "class DoubleInit {\r\n"
        "    private u32 value = 10;\r\n"
        "    public DoubleInit(in u32 v) {\r\n"
        "        this.value = v;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult res2(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DoubleInitCtor, "impl/test.adhd", arena.arena, res2));

    static constexpr MStringView s_BitfieldClass =
        "class BitfieldBad {\r\n"
        "    public u32 flag : 1;\r\n"
        "}\r\n";
    CompileResult res3(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_BitfieldClass, "impl/test.adhd", arena.arena, res3));

    static constexpr MStringView s_DefaultArgFn =
        "void badFn(in u32 x = 5) {\r\n"
        "}\r\n";
    CompileResult res4(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DefaultArgFn, "impl/test.adhd", arena.arena, res4));

    static constexpr MStringView s_MoveCall =
        "void consume(in u32 x) {}\r\n"
        "void caller(in u32 a) {\r\n"
        "    u32 local = a;\r\n"
        "    consume(Move(local));\r\n"
        "}\r\n";
    CompileResult res5(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_MoveCall, "impl/test.adhd", arena.arena, res5));

    static constexpr MStringView s_MutateInParam =
        "void badMutate(in u32 a) {\r\n"
        "    a = 10;\r\n"
        "}\r\n";
    CompileResult res6(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_MutateInParam, "impl/test.adhd", arena.arena, res6));
}

TEST(AdhdCompiler, InfersFuncCaptureStorageAndLowersParallelAndDiagnostics){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "void runFrame(in StringView mode, mut u32 counter) {\r\n"
        "    Func<i32(in i32)> stateless = lambda(in i32 x) -> i32 {\r\n"
        "        return x + 1;\r\n"
        "    };\r\n"
        "    Func<void(in u32)> stateful = lambda[mut counter](in u32 step) -> void {\r\n"
        "        counter += step;\r\n"
        "    };\r\n"
        "    [core(Heavy), priority(Critical)]\r\n"
        "    parallel TaskA {\r\n"
        "        counter += 1;\r\n"
        "    }\r\n"
        "    parallel TaskB : TaskA {\r\n"
        "        counter += 2;\r\n"
        "    }\r\n"
        "    for(u32 i = 0; i < 4; ++i) {\r\n"
        "        [core(Light), priority(Normal)]\r\n"
        "        parallel stateful(i);\r\n"
        "    }\r\n"
        "    switch(mode) {\r\n"
        "    case \"fast\":\r\n"
        "        [essential]\r\n"
        "        info(\"Fast mode active\");\r\n"
        "        break;\r\n"
        "    default:\r\n"
        "        assert(counter > 0, \"counter must be positive\");\r\n"
        "        break;\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult result(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/frame/runner.adhd", arena.arena, result));

    const MStringView cppView(result.generatedSource.data(), result.generatedSource.size());
    EXPECT_NE(cppView.find("InplaceFunction<0u,"), MStringView::npos);
    EXPECT_NE(cppView.find("InplaceFunction<64u,"), MStringView::npos);
    EXPECT_NE(cppView.find("__adhd_task_scope.submit("), MStringView::npos);
    EXPECT_NE(cppView.find("::NWB::Core::CpuTaskCost::Heavy"), MStringView::npos);
    EXPECT_NE(cppView.find("::NWB::Core::CpuTaskPriority::Critical"), MStringView::npos);
    EXPECT_NE(cppView.find("NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT(\"Fast mode active\"))"), MStringView::npos);
    EXPECT_NE(cppView.find("NWB_ASSERT_MSG((counter > 0), NWB_TEXT(\"counter must be positive\"))"), MStringView::npos);
}

TEST(AdhdCompiler, SupportsEnumFlagKeywordsBitwiseOperatorsAndSimdLowering){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "enum<u32> RenderMask {\r\n"
        "    enum None = 0,\r\n"
        "    flag Opaque = 1 << 0,\r\n"
        "    flag Shadow = 1 << 1,\r\n"
        "}\r\n"
        "class Pair<T, U> {\r\n"
        "    public T first = 0;\r\n"
        "    public U second = f32x4(0.0f, 0.0f, 0.0f, 1.0f);\r\n"
        "}\r\n"
        "[reflect, asset(\"material_def\")]\r\n"
        "class MaterialDef {\r\n"
        "    public f32x4 baseColor = f32x4(1.0f, 1.0f, 1.0f, 1.0f);\r\n"
        "    public AssetRef<TextureAsset> albedoMap = AssetRef<TextureAsset>();\r\n"
        "    public Pair<u8, f32x4> packedPair = Pair<u8, f32x4>();\r\n"
        "    public RenderMask mask = RenderMask::Opaque;\r\n"
        "    public RenderMask operator|(in RenderMask extra)const {\r\n"
        "        return this.mask | extra;\r\n"
        "    }\r\n"
        "    public f32 saturateWeight(in f32 w)const;\r\n"
        "}\r\n"
        "f32 MaterialDef::saturateWeight(in f32 w)const {\r\n"
        "    return clamp(w, 0.0f, 1.0f);\r\n"
        "}\r\n";

    CompileResult result(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/render/material_def.adhd", arena.arena, result, nullptr, "NWB::CustomRender"));
    ASSERT_EQ(result.unit.templateInstantiations.size(), 1u);
    ASSERT_EQ(result.unit.templateInstantiations[0]->orderedFields.size(), 2u);
    EXPECT_EQ(result.unit.templateInstantiations[0]->orderedFields[0]->name, "second");
    EXPECT_EQ(result.unit.templateInstantiations[0]->orderedFields[1]->name, "first");

    const MStringView hdrView(result.generatedHeader.data(), result.generatedHeader.size());
    const MStringView cppView(result.generatedSource.data(), result.generatedSource.size());
    EXPECT_NE(hdrView.find("namespace NWB::CustomRender"), MStringView::npos);
    EXPECT_NE(hdrView.find("using Flag = Enum;"), MStringView::npos);
    EXPECT_NE(hdrView.find("Float4 baseColor = Float4("), MStringView::npos);
    EXPECT_NE(hdrView.find("template<>\r\nclass Pair<u8, Float4> final"), MStringView::npos);
    EXPECT_NE(hdrView.find("assetDependencyFieldCount()"), MStringView::npos);
    EXPECT_NE(hdrView.find("deserializeNwb("), MStringView::npos);
    EXPECT_NE(cppView.find("return \"albedoMap\";"), MStringView::npos);
    EXPECT_NE(cppView.find("::Clamp(w, 0.0f, 1.0f)"), MStringView::npos);

    static constexpr MStringView s_FreeOpSource =
        "u32 operator+(in u32 a, in u32 b) {\r\n"
        "    return a + b;\r\n"
        "}\r\n";
    CompileResult freeOpResult(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_FreeOpSource, "impl/render/bad_op.adhd", arena.arena, freeOpResult));

    static constexpr MStringView s_UnimplementedMethodSource =
        "class Incomplete {\r\n"
        "    public u32 x = 0;\r\n"
        "    public void missingBody(in u32 v);\r\n"
        "}\r\n";
    CompileResult unimplResult(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_UnimplementedMethodSource, "impl/render/incomplete.adhd", arena.arena, unimplResult));
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
    EXPECT_FALSE(Compiler::compile(s_MutLiteralCall, "impl/test_mut.adhd", arena.arena, mutRes));

    static constexpr MStringView s_UnsafeParallelVector =
        "void gatherBad(mut Vector<u32> outItems) {\r\n"
        "    parallel_for(u32 i = 0; i < 16; ++i) {\r\n"
        "        outItems.push_back(i);\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult unsafeParRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_UnsafeParallelVector, "impl/test_par_bad.adhd", arena.arena, unsafeParRes));

    static constexpr MStringView s_SafeParallelVector =
        "void gatherGood(mut ParallelVector<u32> outItems, mut Vector<u32> indexedOut) {\r\n"
        "    parallel_for(u32 i = 0; i < 16; ++i) {\r\n"
        "        outItems.push_back(i);\r\n"
        "        indexedOut[i] = i;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult safeParRes(arena.arena);
    EXPECT_TRUE(Compiler::compile(s_SafeParallelVector, "impl/test_par_good.adhd", arena.arena, safeParRes));
}

TEST(AdhdCompiler, SupportsCharLiteralsLineContinuationsInlineAttributesAndOverloadResolution){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "#define MAKE_PAIR(a, \\\r\n"
        "    b) ((a) + (b))\r\n"
        "#if 'A' == 65 && '\\n' == 10 && '\\x10' == 16\r\n"
        "const u32 s_CharConst = 'Z';\r\n"
        "#endif\r\n"
        "[inline]\r\n"
        "i32 pickOverload(in i32 v) {\r\n"
        "    return MAKE_PAIR(\r\n"
        "        v,\r\n"
        "        cast<i32>('!')\r\n"
        "    );\r\n"
        "}\r\n"
        "[noinline]\r\n"
        "f32 pickOverload(in f32 v) {\r\n"
        "    return v + 1.0f;\r\n"
        "}\r\n"
        "class OverloadProbe {\r\n"
        "    public i32 m_val;\r\n"
        "    [inline]\r\n"
        "    public OverloadProbe(in i32 v) {\r\n"
        "        this.m_val = v;\r\n"
        "    }\r\n"
        "    [noinline]\r\n"
        "    public OverloadProbe(in f32 v) {\r\n"
        "        this.m_val = cast<i32>(v);\r\n"
        "    }\r\n"
        "    [inline]\r\n"
        "    public i32 eval(in i32 x)const {\r\n"
        "        return pickOverload(x);\r\n"
        "    }\r\n"
        "    [noinline]\r\n"
        "    public f32 eval(in f32 x)const {\r\n"
        "        return pickOverload(x);\r\n"
        "    }\r\n"
        "}\r\n"
        "void runProbe(mut f32x4 vec) {\r\n"
        "    OverloadProbe pInt(10);\r\n"
        "    OverloadProbe pFlt(2.5f);\r\n"
        "    i32 ri = pInt.eval(4);\r\n"
        "    f32 rf = pFlt.eval(1.5f);\r\n"
        "    f32x3u splatU(2.0f);\r\n"
        "    parallel_for(usize i = 0u; i < 16u; i += 4u) {\r\n"
        "    }\r\n"
        "}\r\n"
        "in f32x4 borrowSimdRef(in f32x4 v) {\r\n"
        "    return v;\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/test_char_inline_overload.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("inline constexpr u32 s_CharConst = 'Z';"), MStringView::npos);
    EXPECT_NE(hdrView.find("NWB_FORCE_INLINE inline i32 pickOverload(const i32 v)"), MStringView::npos);
    EXPECT_NE(hdrView.find("NWB_FORCE_INLINE OverloadProbe(const i32 v)noexcept"), MStringView::npos);
    EXPECT_NE(hdrView.find("NWB_FORCE_INLINE i32 eval(const i32 x)const noexcept"), MStringView::npos);
    EXPECT_NE(cppView.find("NWB_NOINLINE f32 pickOverload(const f32 v)"), MStringView::npos);
    EXPECT_NE(cppView.find("NWB_NOINLINE OverloadProbe::OverloadProbe(const f32 v)noexcept"), MStringView::npos);
    EXPECT_NE(cppView.find("NWB_NOINLINE f32 OverloadProbe::eval(const f32 x)const noexcept"), MStringView::npos);
    EXPECT_NE(cppView.find("Float3U splatU(2.0f, 2.0f, 2.0f);"), MStringView::npos);
    EXPECT_NE(cppView.find("static_cast<usize>(16u), static_cast<usize>(4u), [&](const usize __adhd_idx)noexcept"), MStringView::npos);
    EXPECT_NE(cppView.find("return ::MakeNotNull(&(v));"), MStringView::npos);
}

TEST(AdhdCompiler, ValidatesNewSemanticConstraintsAndExtendedReflection){
    AdhdTestArena arena;

    static constexpr MStringView s_ReflectSerializeSource =
        "enum<u8> SlotKey {\r\n"
        "    enum Primary = 0u,\r\n"
        "    enum Secondary = 1u\r\n"
        "}\r\n"
        "[serialize]\r\n"
        "class SubEntry {\r\n"
        "    public u32 id = 0u;\r\n"
        "}\r\n"
        "class AssetBox<T> {\r\n"
        "    public T item;\r\n"
        "}\r\n"
        "class TexAsset {}\r\n"
        "[serialize, reflect]\r\n"
        "class BundlePayload {\r\n"
        "    public Array<Name, 2> tags;\r\n"
        "    public Array<SubEntry, 2> subs;\r\n"
        "}\r\n"
        "[reflect]\r\n"
        "class SceneTable {\r\n"
        "    public Map<SlotKey, i32> slotWeights;\r\n"
        "    public AssetBox<AssetRef<TexAsset>> boxedTex;\r\n"
        "}\r\n";

    CompileResult reflectRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_ReflectSerializeSource, "impl/test_bundle_reflect.adhd", arena.arena, reflectRes));
    const MStringView reflectHdr(reflectRes.generatedHeader.data(), reflectRes.generatedHeader.size());
    const MStringView reflectCpp(reflectRes.generatedSource.data(), reflectRes.generatedSource.size());

    EXPECT_NE(reflectHdr.find("(sizeof(tags) / sizeof(tags[0])) * sizeof(::NameHash)"), MStringView::npos);
    EXPECT_NE(reflectHdr.find("(sizeof(subs) / sizeof(subs[0])) * SubEntry::serializedBinarySize()"), MStringView::npos);
    EXPECT_NE(reflectCpp.find("for(const auto& __elem : this->tags)"), MStringView::npos);
    EXPECT_NE(reflectCpp.find("for(auto& __elem : this->subs)"), MStringView::npos);
    EXPECT_NE(reflectHdr.find("if(const auto __ek = SlotKey::parseNamedEnum(__k)){ this->slotWeights[*__ek] = static_cast<i32>(__v.asInteger()); }"), MStringView::npos);
    EXPECT_NE(reflectHdr.find("AssetBox<::NWB::Core::Assets::AssetRef<TexAsset>>::extractNwbAssetDependencies(*__f, callback)"), MStringView::npos);

    static constexpr MStringView s_OverflowFlagEnum =
        "enum<u8> TooManyFlags {\r\n"
        "    flag F0,\r\n"
        "    flag F1,\r\n"
        "    flag F2,\r\n"
        "    flag F3,\r\n"
        "    flag F4,\r\n"
        "    flag F5,\r\n"
        "    flag F6,\r\n"
        "    flag F7,\r\n"
        "    flag F8\r\n"
        "}\r\n";
    CompileResult overflowFlagRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_OverflowFlagEnum, "impl/bad_flag_overflow.adhd", arena.arena, overflowFlagRes));

    static constexpr MStringView s_BadFloatSwitch =
        "void badSwitch(in f32 v) {\r\n"
        "    switch(v) {\r\n"
        "    default:\r\n"
        "        break;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badSwitchRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_BadFloatSwitch, "impl/bad_float_switch.adhd", arena.arena, badSwitchRes));

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
    EXPECT_FALSE(Compiler::compile(s_DupCaseSwitch, "impl/bad_dup_case.adhd", arena.arena, dupCaseRes));

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
    EXPECT_FALSE(Compiler::compile(s_PartialCtorInit, "impl/bad_partial_ctor.adhd", arena.arena, partialCtorRes));

    static constexpr MStringView s_CallNonFuncField =
        "class NonCallableField {\r\n"
        "    public i32 value = 0;\r\n"
        "    public void run() {\r\n"
        "        this.value();\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult nonFuncFieldRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_CallNonFuncField, "impl/bad_call_field.adhd", arena.arena, nonFuncFieldRes));

    static constexpr MStringView s_ConflictingInlineAttrs =
        "[inline, noinline]\r\n"
        "void badAttrs() {}\r\n";
    CompileResult badAttrRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_ConflictingInlineAttrs, "impl/bad_inline_attrs.adhd", arena.arena, badAttrRes));
}

TEST(AdhdCompiler, SupportsPass2PreprocessorSemaAndCodegenEnhancements){
    AdhdTestArena arena;

    static constexpr MStringView s_ValidPass2Source =
        "/* leading block comment */ #define PICK_VAL(cond, a, b) ((cond) ? (a) : (b))\r\n"
        "#if PICK_VAL(1, 10, 20) == 10 && '\\u0041' == 65\r\n"
        "enum<u8> SemiEnum {\r\n"
        "    enum First = 0u\r\n"
        "};\r\n"
        "interface ISemiInterface {\r\n"
        "    i32 get()const;\r\n"
        "};\r\n"
        "class TexAsset {};\r\n"
        "[reflect]\r\n"
        "class OptionalContainerDeps {\r\n"
        "    public Optional<Vector<AssetRef<TexAsset>>> optRefs;\r\n"
        "};\r\n"
        "[inline]\r\n"
        "u32 inlineParallelAndRawVec(in f32x3u unalignedVec, mut Vector<f32x4> simdItems) {\r\n"
        "    for(mut f32x4 item : simdItems) {\r\n"
        "        item = item + f32x4(1.0f, 2.0f, 3.0f, 4.0f);\r\n"
        "    }\r\n"
        "    parallel LocalTask {\r\n"
        "        Vector<i32> taskLocalVec;\r\n"
        "        taskLocalVec.push_back(42);\r\n"
        "    }\r\n"
        "    wait();\r\n"
        "    f32 firstLane = unalignedVec[0];\r\n"
        "    native(in laneVal = firstLane) {\r\n"
        "        const i32 sep = 1'000;\r\n"
        "        const char* rawBrace = R\"(raw } brace)\";\r\n"
        "        (void)sep;\r\n"
        "        (void)rawBrace;\r\n"
        "        (void)laneVal;\r\n"
        "    }\r\n"
        "    return bit_cast<u32>(firstLane);\r\n"
        "}\r\n"
        "#endif\r\n";

    CompileResult validRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_ValidPass2Source, "impl/pass2_valid.adhd", arena.arena, validRes));
    const MStringView hdrView(validRes.generatedHeader.data(), validRes.generatedHeader.size());
    EXPECT_NE(hdrView.find("#include <core/task/cpu/scheduler.h>"), MStringView::npos);
    EXPECT_NE(hdrView.find("unalignedVec.raw[0]"), MStringView::npos);
    EXPECT_NE(hdrView.find("if(this->optRefs.has_value()){ for(const auto& __dep : *this->optRefs){ if(__dep.valid()) callback(__dep.virtualPath); } }"), MStringView::npos);

    static constexpr MStringView s_BadHexLiteral =
        "const u32 kBad = 0x1G;\r\n";
    CompileResult badHexRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_BadHexLiteral, "impl/bad_hex.adhd", arena.arena, badHexRes));

    static constexpr MStringView s_DupMacroParam =
        "#define BAD_MACRO(x, x) (x)\r\n"
        "const i32 kVal = 1;\r\n";
    CompileResult dupMacroRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DupMacroParam, "impl/bad_macro_param.adhd", arena.arena, dupMacroRes));

    static constexpr MStringView s_UnaryCStyleCast =
        "i32 badCast() {\r\n"
        "    return (i32)-1;\r\n"
        "}\r\n";
    CompileResult unaryCastRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_UnaryCStyleCast, "impl/bad_unary_cast.adhd", arena.arena, unaryCastRes));

    static constexpr MStringView s_BitCastSizeMismatch =
        "u64 badBitCast(in f32 x) {\r\n"
        "    return bit_cast<u64>(x);\r\n"
        "}\r\n";
    CompileResult bitCastRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_BitCastSizeMismatch, "impl/bad_bit_cast.adhd", arena.arena, bitCastRes));

    static constexpr MStringView s_DanglingLocalReturn =
        "in i32 badReturnRef() {\r\n"
        "    i32 localVal = 7;\r\n"
        "    return localVal;\r\n"
        "}\r\n";
    CompileResult danglingRetRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DanglingLocalReturn, "impl/bad_dangling_ret.adhd", arena.arena, danglingRetRes));

    static constexpr MStringView s_RvalueMutArg =
        "void mutateInt(mut i32 v) { v = v + 1; }\r\n"
        "void caller(in i32 a, in i32 b) {\r\n"
        "    mutateInt(a + b);\r\n"
        "}\r\n";
    CompileResult rvalueMutRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_RvalueMutArg, "impl/bad_rvalue_mut.adhd", arena.arena, rvalueMutRes));

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
    EXPECT_FALSE(Compiler::compile(s_EarlyReturnCtor, "impl/bad_early_ret_ctor.adhd", arena.arena, earlyRetCtorRes));

    static constexpr MStringView s_ParallelForSet =
        "void badParSet(in Set<i32> items) {\r\n"
        "    parallel_for(in i32 x : items) {\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult parSetRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_ParallelForSet, "impl/bad_par_set.adhd", arena.arena, parSetRes));
}

TEST(AdhdCompiler, ValidatesPass3PreprocessorParserSemaAndCodegenEdgeCases){
    AdhdTestArena arena;

    static constexpr MStringView s_ValidPass3Source =
        "/* multi-line block comment\r\n"
        "   closing before directive */ #define K_SHIFT 2\r\n"
        "const i32 MaxCount = 8;\r\n"
        "enum<u8> ExecMode {\r\n"
        "    enum Slow = 0,\r\n"
        "    enum Fast = 1,\r\n"
        "}\r\n"
        "class ModeHolder {\r\n"
        "    public ExecMode mode = ExecMode::Enum::Fast;\r\n"
        "    public Vector<Func<i32(in i32)>> callbacks;\r\n"
        "    public Array<u32, (1 << K_SHIFT)> table = Array<u32, (1 << K_SHIFT)>();\r\n"
        "    public i32 evaluate(in i32 x, in StringView tag) {\r\n"
        "        ExecMode m0 = this.mode::Enum::Fast;\r\n"
        "        i32 limit = (MaxCount) - 1;\r\n"
        "        i32 dblNeg = - -x;\r\n"
        "        auto fn = lambda[in MaxCount, in limit](in i32 v) -> i32 {\r\n"
        "            return v + MaxCount + limit;\r\n"
        "        };\r\n"
        "        this.callbacks.push_back(fn);\r\n"
        "        switch(x) {\r\n"
        "        case 1: {\r\n"
        "            dblNeg += 1;\r\n"
        "        } break;\r\n"
        "        default:\r\n"
        "            break;\r\n"
        "        }\r\n"
        "        switch(tag) {\r\n"
        "        default:\r\n"
        "            break;\r\n"
        "        case \"alpha\":\r\n"
        "            if(x < 0) {\r\n"
        "                break;\r\n"
        "            }\r\n"
        "            dblNeg += 2;\r\n"
        "            break;\r\n"
        "        }\r\n"
        "        for(i32 k = 0; k < 2; ++k) {\r\n"
        "            switch(tag) {\r\n"
        "            case \"beta\":\r\n"
        "                if(k == 0) {\r\n"
        "                    continue;\r\n"
        "                }\r\n"
        "                if(x == 0) {\r\n"
        "                    break;\r\n"
        "                }\r\n"
        "                dblNeg += 1;\r\n"
        "                break;\r\n"
        "            default:\r\n"
        "                break;\r\n"
        "            }\r\n"
        "        }\r\n"
        "        parallel {\r\n"
        "            String taskNote = \"worker_a\";\r\n"
        "            taskNote.clear();\r\n"
        "        }\r\n"
        "        parallel {\r\n"
        "            String taskNote2 = \"worker_b\";\r\n"
        "            taskNote2.clear();\r\n"
        "        }\r\n"
        "        wait();\r\n"
        "        native {\r\n"
        "            #pragma clang diagnostic push\r\n"
        "            #pragma clang diagnostic pop\r\n"
        "        }\r\n"
        "        return dblNeg + cast<i32>(m0);\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult validRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_ValidPass3Source, "impl/pass3_valid.adhd", arena.arena, validRes));
    const MStringView hdrView(validRes.generatedHeader.data(), validRes.generatedHeader.size());
    const MStringView cppView(validRes.generatedSource.data(), validRes.generatedSource.size());
    EXPECT_NE(hdrView.find("Vector<::InplaceFunction<64u, i32(i32)>> callbacks{s_ClassArena};"), MStringView::npos);
    EXPECT_NE(cppView.find("-(-x)"), MStringView::npos);
    EXPECT_NE(cppView.find("[limit]("), MStringView::npos);
    EXPECT_NE(cppView.find("goto __adhd_switch_break_"), MStringView::npos);
    EXPECT_NE(cppView.find("::NWB::Core::Alloc::ScratchArena __adhd_scratch(::Name(\"parallel\"));"), MStringView::npos);

    static constexpr MStringView s_SimdLoopScopeSource =
        "void processSimdLoops(mut Vector<f32x4> vec) {\r\n"
        "    for(in f32x4 item : vec) {\r\n"
        "        f32x4 tmp = item + item;\r\n"
        "    }\r\n"
        "    for(mut f32x4 item : vec) {\r\n"
        "        item = item + item;\r\n"
        "        if(vec.size() > 4u) {\r\n"
        "            continue;\r\n"
        "        }\r\n"
        "        break;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult simdLoopRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_SimdLoopScopeSource, "impl/simd_loop_scope.adhd", arena.arena, simdLoopRes));
    const MStringView simdLoopCpp(simdLoopRes.generatedSource.data(), simdLoopRes.generatedSource.size());
    EXPECT_NE(simdLoopCpp.find("::StoreFloat(__adhd_simd_item, item);\r\n            continue;"), MStringView::npos);
    EXPECT_NE(simdLoopCpp.find("::StoreFloat(__adhd_simd_item, item);\r\n        break;"), MStringView::npos);

    static constexpr MStringView s_BadSurrogateEscape =
        "const u32 kSurrogate = '\\uD800';\r\n";
    CompileResult badSurrogateRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_BadSurrogateEscape, "impl/bad_surrogate.adhd", arena.arena, badSurrogateRes));

    static constexpr MStringView s_BadEmptyAttrParens =
        "[reflect()]\r\n"
        "class BadAttr {\r\n"
        "    public i32 x = 0;\r\n"
        "}\r\n";
    CompileResult badAttrRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_BadEmptyAttrParens, "impl/bad_attr.adhd", arena.arena, badAttrRes));

    static constexpr MStringView s_BadDupConstMethod =
        "class DupConst {\r\n"
        "    public i32 get()const const { return 0; }\r\n"
        "}\r\n";
    CompileResult badDupConstRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_BadDupConstMethod, "impl/bad_dup_const.adhd", arena.arena, badDupConstRes));

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
    EXPECT_FALSE(Compiler::compile(s_BadDependentBound, "impl/bad_dep_bound.adhd", arena.arena, badDepBoundRes));

    static constexpr MStringView s_BadInInterfaceMutCall =
        "interface IMutator {\r\n"
        "    void mutate();\r\n"
        "}\r\n"
        "void callInInterface(in IMutator m) {\r\n"
        "    m.mutate();\r\n"
        "}\r\n";
    CompileResult badInIfaceRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_BadInInterfaceMutCall, "impl/bad_in_iface.adhd", arena.arena, badInIfaceRes));

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
    EXPECT_FALSE(Compiler::compile(s_BadParallelContainerLambda, "impl/bad_par_lam.adhd", arena.arena, badParLamRes));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

