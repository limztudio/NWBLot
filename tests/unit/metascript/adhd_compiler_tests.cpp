// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/compiler.h>
#include <core/metascript/parser.h>

#include <global/inplace_function.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AdhdTestArenaTag{};
using AdhdTestArena = NWB::Tests::TestArena<AdhdTestArenaTag>;

using NWB::Core::Metascript::Adhd::Compiler;
using NWB::Core::Metascript::Adhd::CompileResult;
using NWB::Core::Metascript::MString;
using NWB::Core::Metascript::MStringView;
using NWB::Core::Metascript::MetaArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MockIncludeResolver final : public NWB::Core::Metascript::IIncludeResolver{
public:
    u32 resolveCallCount = 0u;


public:
    [[nodiscard]] bool resolveInclude(
        const MStringView includePath,
        const bool,
        MetaArena& arena,
        MString& outResolvedKey,
        MString& outContent
    )override{
        ++resolveCallCount;
        outResolvedKey = MString(includePath.data(), includePath.size(), arena);
        static constexpr MStringView s_HeaderCode = "const u32 kSharedLimit = 16;\r\n";
        outContent = MString(s_HeaderCode.data(), s_HeaderCode.size(), arena);
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AdhdInplaceFunction, StatelessZeroByteAndStatefulTrampolines){
    using StatelessFn = InplaceFunction<0u, i32(i32, i32)>;
    static_assert(sizeof(StatelessFn) == sizeof(void*), "Stateless InplaceFunction<0> must be 1 pointer wide");

    StatelessFn addFn([](const i32 a, const i32 b) -> i32{ return a + b; });
    ASSERT_TRUE(static_cast<bool>(addFn));
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
    ASSERT_TRUE(static_cast<bool>(statefulFn));
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
        "    public bool dirty = false;\r\n"
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
        "    public RenderMask operator|(in RenderMask extra) const {\r\n"
        "        return this.mask | extra;\r\n"
        "    }\r\n"
        "    public f32 saturateWeight(in f32 w) const;\r\n"
        "}\r\n"
        "f32 MaterialDef::saturateWeight(in f32 w) const {\r\n"
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

TEST(AdhdCompiler, ValidatesTemplateInterfaceBoundsAndRebindableInterfaceReferences){
    AdhdTestArena arena;

    static constexpr MStringView s_ValidSource =
        "interface ITask {\r\n"
        "    void execute(in f32 dt);\r\n"
        "}\r\n"
        "class PhysicsTask : ITask {\r\n"
        "    private f32 m_accum = 0.0f;\r\n"
        "    public void execute(in f32 dt) override {\r\n"
        "        this.m_accum += dt;\r\n"
        "    }\r\n"
        "}\r\n"
        "class RenderTask : ITask {\r\n"
        "    private f32 m_time = 0.0f;\r\n"
        "    public void execute(in f32 dt) override {\r\n"
        "        this.m_time += dt;\r\n"
        "    }\r\n"
        "}\r\n"
        "class TaskRunner<T : ITask> {\r\n"
        "    public T task = T();\r\n"
        "}\r\n"
        "void dispatchAll(in f32 dt) {\r\n"
        "    TaskRunner<PhysicsTask> runner = TaskRunner<PhysicsTask>();\r\n"
        "    PhysicsTask a = PhysicsTask();\r\n"
        "    RenderTask b = RenderTask();\r\n"
        "    mut ITask active = a;\r\n"
        "    active.execute(dt);\r\n"
        "    active = b;\r\n"
        "    active.execute(dt);\r\n"
        "}\r\n";

    CompileResult validRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_ValidSource, "impl/task_runner.adhd", arena.arena, validRes));
    const MStringView cppView(validRes.generatedSource.data(), validRes.generatedSource.size());
    EXPECT_NE(cppView.find("::NotNull<ITask*> active = ::MakeNotNull(&(a));"), MStringView::npos);
    EXPECT_NE(cppView.find("active->execute(dt);"), MStringView::npos);
    EXPECT_NE(cppView.find("(active = ::MakeNotNull(&(b)));"), MStringView::npos);

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
        "void badUse() {\r\n"
        "    TaskRunner<PlainData> runner = TaskRunner<PlainData>();\r\n"
        "}\r\n";

    CompileResult invalidBoundRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_InvalidBoundArgSource, "impl/bad_bound.adhd", arena.arena, invalidBoundRes));

    static constexpr MStringView s_DowncastInterfaceSource =
        "interface ITask {\r\n"
        "    void execute(in f32 dt);\r\n"
        "}\r\n"
        "class PhysicsTask : ITask {\r\n"
        "    public void execute(in f32 dt) override {}\r\n"
        "}\r\n"
        "void badDowncast(in ITask task) {\r\n"
        "    PhysicsTask p = cast<PhysicsTask>(task);\r\n"
        "}\r\n";
    CompileResult downcastRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DowncastInterfaceSource, "impl/bad_downcast.adhd", arena.arena, downcastRes));
}

TEST(AdhdCompiler, SupportsCustomClassSwitchEnumVarScopeAndConstSafety){
    AdhdTestArena arena;

    static constexpr MStringView s_ValidSource =
        "enum<u8> Mode {\r\n"
        "    enum Idle = 0,\r\n"
        "    flag Active = 1 << 0,\r\n"
        "}\r\n"
        "class TagKey {\r\n"
        "    public u32 id = 0;\r\n"
        "    public bool operator==(in TagKey rhs) const {\r\n"
        "        return this.id == rhs.id;\r\n"
        "    }\r\n"
        "}\r\n"
        "u32 matchKey(in TagKey key, in TagKey target) {\r\n"
        "    Mode m = Mode::Enum::Idle;\r\n"
        "    u8 bits = m::Flag::Active;\r\n"
        "    switch(key) {\r\n"
        "    case target:\r\n"
        "        return cast<u32>(bits);\r\n"
        "    default:\r\n"
        "        return 0;\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult validRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_ValidSource, "impl/tag_switch.adhd", arena.arena, validRes));
    const MStringView cppView(validRes.generatedSource.data(), validRes.generatedSource.size());
    EXPECT_NE(cppView.find("Mode::Flag::Active"), MStringView::npos);
    EXPECT_NE(cppView.find("if(key == target){"), MStringView::npos);

    static constexpr MStringView s_ConstViolationSource =
        "class Counter {\r\n"
        "    private u32 count = 0;\r\n"
        "    public void bump() {\r\n"
        "        this.count += 1;\r\n"
        "    }\r\n"
        "}\r\n"
        "void badConstCall(in Counter c) {\r\n"
        "    c.bump();\r\n"
        "}\r\n";
    CompileResult constRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_ConstViolationSource, "impl/bad_const.adhd", arena.arena, constRes));
}

TEST(AdhdCompiler, EnforcesTemplateBoundMethodCallsAndEmitsInstantiationReflection){
    AdhdTestArena arena;

    static constexpr MStringView s_ValidSource =
        "interface IComponent {\r\n"
        "    void step(in f32 dt);\r\n"
        "    u32 id() const;\r\n"
        "}\r\n"
        "class Converter {\r\n"
        "    private Vector<i32> m_buffer(8);\r\n"
        "    public Out convert<Out>(in u32 x) const {\r\n"
        "        return cast<Out>(x);\r\n"
        "    }\r\n"
        "}\r\n"
        "u32 runBounded<T : IComponent>(mut T comp, in Converter conv, in f32 dt) {\r\n"
        "    comp.step(dt);\r\n"
        "    return conv.convert<u32>(comp.id());\r\n"
        "}\r\n";

    CompileResult validRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_ValidSource, "impl/bounded_call.adhd", arena.arena, validRes));
    const MStringView hdrView(validRes.generatedHeader.data(), validRes.generatedHeader.size());
    EXPECT_NE(hdrView.find("m_buffer{8, s_ClassArena};"), MStringView::npos);
    EXPECT_NE(hdrView.find("conv.convert<u32>(comp.id())"), MStringView::npos);

    static constexpr MStringView s_UnboundedMethodCall =
        "void badUnbounded<T>(mut T comp) {\r\n"
        "    comp.step(1.0f);\r\n"
        "}\r\n";
    CompileResult unboundedRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_UnboundedMethodCall, "impl/bad_unbounded.adhd", arena.arena, unboundedRes));

    static constexpr MStringView s_CStyleArraySource =
        "void badArray() {\r\n"
        "    i32 values[4] = 0;\r\n"
        "}\r\n";
    CompileResult cArrayRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_CStyleArraySource, "impl/bad_array.adhd", arena.arena, cArrayRes));

    static constexpr MStringView s_CppIncludeOutsideNative =
        "#include <vector>\r\n"
        "void nop() {}\r\n";
    CompileResult cppIncRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_CppIncludeOutsideNative, "impl/bad_inc.adhd", arena.arena, cppIncRes));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

