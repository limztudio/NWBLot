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
        if(includePath == "shared_service.adhd"){
            static constexpr MStringView s_ServiceHeader =
                "interface IStore<T> {\r\n"
                "    void put(in T value);\r\n"
                "}\r\n"
                "[reflect]\r\n"
                "class SharedService {\r\n"
                "    private i32 m_total = 0;\r\n"
                "    public i32 accumulate(in i32 delta);\r\n"
                "}\r\n";
            outContent = MString(s_ServiceHeader.data(), s_ServiceHeader.size(), arena);
            return true;
        }
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

    static constexpr MStringView s_ByValueInterfaceVar =
        "interface IComponent {\r\n"
        "    void step(in f32 dt);\r\n"
        "}\r\n"
        "class Comp : IComponent {\r\n"
        "    public void step(in f32 dt) override {}\r\n"
        "}\r\n"
        "void badByValue() {\r\n"
        "    Comp c = Comp();\r\n"
        "    IComponent val = c;\r\n"
        "}\r\n";
    CompileResult byValIfaceRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_ByValueInterfaceVar, "impl/bad_val_iface.adhd", arena.arena, byValIfaceRes));

    static constexpr MStringView s_MismatchedOverrideConst =
        "interface IComponent {\r\n"
        "    u32 id() const;\r\n"
        "}\r\n"
        "class BadComp : IComponent {\r\n"
        "    public u32 id() override {\r\n"
        "        return 0;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult mismatchOverrideRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_MismatchedOverrideConst, "impl/bad_override_sig.adhd", arena.arena, mismatchOverrideRes));
}

TEST(AdhdCompiler, SupportsCrossFileIncludesTemplatedInterfaceRefsAndStrictEnumAndOperatorChecks){
    AdhdTestArena arena;
    MockIncludeResolver resolver;

    static constexpr MStringView s_SplitImplSource =
        "#include \"shared_service.adhd\"\r\n"
        "class StoreClient {\r\n"
        "    private mut IStore<i32> m_store;\r\n"
        "    public StoreClient(mut IStore<i32> store) {\r\n"
        "        this.m_store = store;\r\n"
        "    }\r\n"
        "    public void push(in i32 v) {\r\n"
        "        this.m_store.put(v);\r\n"
        "    }\r\n"
        "}\r\n"
        "i32 SharedService::accumulate(in i32 delta) {\r\n"
        "    this.m_total += delta;\r\n"
        "    return this.m_total;\r\n"
        "}\r\n";

    CompileResult splitRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_SplitImplSource, "impl/service/shared_service_impl.adhd", arena.arena, splitRes, &resolver));
    const MStringView hdrView(splitRes.generatedHeader.data(), splitRes.generatedHeader.size());
    const MStringView cppView(splitRes.generatedSource.data(), splitRes.generatedSource.size());
    EXPECT_NE(hdrView.find("#include \"shared_service.adhd.h\""), MStringView::npos);
    EXPECT_EQ(hdrView.find("class SharedService final"), MStringView::npos);
    EXPECT_NE(hdrView.find("::NotNull<IStore<i32>*> m_store;"), MStringView::npos);
    EXPECT_NE(cppView.find("StoreClient::StoreClient(IStore<i32>& store)noexcept : m_store(::MakeNotNull(&(store)))"), MStringView::npos);
    EXPECT_NE(cppView.find("this->m_store->put(v);"), MStringView::npos);
    EXPECT_NE(cppView.find("i32 SharedService::accumulate(const i32 delta)noexcept"), MStringView::npos);
    EXPECT_EQ(cppView.find("SharedService::reflectedFieldCount"), MStringView::npos);

    static constexpr MStringView s_MismatchedEnumFlagScope =
        "enum<u8> Mode {\r\n"
        "    enum Idle = 0,\r\n"
        "    flag Active,\r\n"
        "}\r\n"
        "u8 badScope() {\r\n"
        "    return Mode::Flag::Idle;\r\n"
        "}\r\n";
    CompileResult badEnumScopeRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_MismatchedEnumFlagScope, "impl/bad_enum_scope.adhd", arena.arena, badEnumScopeRes));

    static constexpr MStringView s_InvalidOperatorArity =
        "class Mask {\r\n"
        "    public u32 bits = 0;\r\n"
        "    public Mask operator~(in Mask other) const {\r\n"
        "        return Mask();\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badOpArityRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_InvalidOperatorArity, "impl/bad_op_arity.adhd", arena.arena, badOpArityRes));

    static constexpr MStringView s_MismatchedTemplateArgOverride =
        "interface IBatch {\r\n"
        "    void process(in Span<i32> items);\r\n"
        "}\r\n"
        "class BadBatch : IBatch {\r\n"
        "    public void process(in Span<f32> items) override {}\r\n"
        "}\r\n";
    CompileResult badTemplateOverrideRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_MismatchedTemplateArgOverride, "impl/bad_tpl_override.adhd", arena.arena, badTemplateOverrideRes));

    static constexpr MStringView s_ExprLevelTemplateInstantiation =
        "class Pair<T, U> {\r\n"
        "    public T first = 0;\r\n"
        "    public U second = f32x4(0.0f, 0.0f, 0.0f, 1.0f);\r\n"
        "}\r\n"
        "void makeAutoPair() {\r\n"
        "    auto p = Pair<u8, f32x4>();\r\n"
        "}\r\n";
    CompileResult exprInstRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_ExprLevelTemplateInstantiation, "impl/auto_pair.adhd", arena.arena, exprInstRes));
    ASSERT_EQ(exprInstRes.unit.templateInstantiations.size(), 1u);
    EXPECT_EQ(exprInstRes.unit.templateInstantiations[0]->orderedFields[0]->name, "second");

    static constexpr MStringView s_DuplicateFieldSource =
        "class DupClass {\r\n"
        "    public u32 value = 0;\r\n"
        "    private u32 value = 1;\r\n"
        "}\r\n";
    CompileResult dupFieldRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DuplicateFieldSource, "impl/dup_field.adhd", arena.arena, dupFieldRes));

    static constexpr MStringView s_UnknownThisMemberSource =
        "class KnownClass {\r\n"
        "    public u32 count = 0;\r\n"
        "    public u32 readBad() const {\r\n"
        "        return this.missingField;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult unknownThisRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_UnknownThisMemberSource, "impl/unknown_this.adhd", arena.arena, unknownThisRes));

    static constexpr MStringView s_InterfaceMethodByValueReturn =
        "interface IFactory {\r\n"
        "    IFactory cloneSelf();\r\n"
        "}\r\n";
    CompileResult ifaceValRetRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_InterfaceMethodByValueReturn, "impl/bad_iface_ret.adhd", arena.arena, ifaceValRetRes));

    static constexpr MStringView s_FuncAndSimdParamAbiSource =
        "void dispatchSample(in Func<i32(in i32)> fn, in f32x4 tint) {\r\n"
        "}\r\n";
    CompileResult abiRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_FuncAndSimdParamAbiSource, "impl/param_abi.adhd", arena.arena, abiRes));
    EXPECT_NE(MStringView(abiRes.generatedHeader.data(), abiRes.generatedHeader.size()).find("InplaceFunction<0u, i32(i32)>"), MStringView::npos);
    EXPECT_NE(MStringView(abiRes.generatedHeader.data(), abiRes.generatedHeader.size()).find("const Float4& tint"), MStringView::npos);
    EXPECT_EQ(MStringView(abiRes.generatedHeader.data(), abiRes.generatedHeader.size()).find("// Compiled by ADHD Compiler 1.0.0\r\n"), 0u);
    EXPECT_EQ(MStringView(abiRes.generatedSource.data(), abiRes.generatedSource.size()).find("// Compiled by ADHD Compiler 1.0.0\r\n"), 0u);

    static constexpr MStringView s_TemplateArgCountMismatchSource =
        "class Pair<T, U> {\r\n"
        "    public T first = 0;\r\n"
        "    public U second = 0;\r\n"
        "}\r\n"
        "void badPairUse() {\r\n"
        "    Pair<u8> p;\r\n"
        "}\r\n";
    CompileResult badTplArityRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_TemplateArgCountMismatchSource, "impl/bad_tpl_arity.adhd", arena.arena, badTplArityRes));

    static constexpr MStringView s_DuplicateEnumeratorSource =
        "enum DupMode : u8 {\r\n"
        "    enum Alpha,\r\n"
        "    enum Alpha\r\n"
        "}\r\n";
    CompileResult dupEnumRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DuplicateEnumeratorSource, "impl/dup_enum.adhd", arena.arena, dupEnumRes));

    static constexpr MStringView s_DuplicateLocalVarSource =
        "void badScope(in u32 x) {\r\n"
        "    u32 x = 1u;\r\n"
        "}\r\n";
    CompileResult dupLocalRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DuplicateLocalVarSource, "impl/dup_local.adhd", arena.arena, dupLocalRes));

    static constexpr MStringView s_SimdNwbAndParallelInclusiveSource =
        "[reflect]\r\n"
        "class BoundsAsset {\r\n"
        "    public f64x3 extent = f64x3(1.0, 2.0, 3.0);\r\n"
        "    public i32x4 tile = i32x4(0, 0, 1, 1);\r\n"
        "}\r\n"
        "void runInclusive() {\r\n"
        "    parallel_for (u32 i = 0u; i <= 7u; ++i) {\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult simdNwbRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_SimdNwbAndParallelInclusiveSource, "impl/bounds_asset.adhd", arena.arena, simdNwbRes));
    const MStringView boundsHdr(simdNwbRes.generatedHeader.data(), simdNwbRes.generatedHeader.size());
    const MStringView boundsCpp(simdNwbRes.generatedSource.data(), simdNwbRes.generatedSource.size());
    EXPECT_NE(boundsHdr.find("static constexpr usize reflectedFieldCount()noexcept{ return 2u; }"), MStringView::npos);
    EXPECT_NE(boundsHdr.find("Double3(static_cast<f64>"), MStringView::npos);
    EXPECT_NE(boundsHdr.find("Int4(static_cast<i32>"), MStringView::npos);
    EXPECT_NE(boundsCpp.find("static_cast<usize>(7u + 1u)"), MStringView::npos);

    static constexpr MStringView s_BreakOutsideLoopSource =
        "void badBreak() {\r\n"
        "    break;\r\n"
        "}\r\n";
    CompileResult badBreakRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_BreakOutsideLoopSource, "impl/bad_break.adhd", arena.arena, badBreakRes));

    static constexpr MStringView s_ReturnInParallelSource =
        "u32 badParReturn() {\r\n"
        "    parallel_for (u32 i = 0u; i < 4u; ++i) {\r\n"
        "        return i;\r\n"
        "    }\r\n"
        "    return 0u;\r\n"
        "}\r\n";
    CompileResult badParRetRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_ReturnInParallelSource, "impl/bad_par_ret.adhd", arena.arena, badParRetRes));

    static constexpr MStringView s_VoidReturnValueSource =
        "void badVoidRet() {\r\n"
        "    return 42;\r\n"
        "}\r\n";
    CompileResult badVoidRetRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_VoidReturnValueSource, "impl/bad_void_ret.adhd", arena.arena, badVoidRetRes));
}

TEST(AdhdCompiler, OptimizesSimdMathWithBlockScopedLoadStoreAndFmaLowering){
    AdhdTestArena arena;

    static constexpr MStringView s_SimdSource =
        "class TransformBody {\r\n"
        "    private f32x4 position = f32x4(0.0f, 0.0f, 0.0f, 1.0f);\r\n"
        "    public f32 integrate(in f32x4 velocity, in f32x4 accel, in f32 dt, in u32 steps) {\r\n"
        "        f32x4 step = velocity * f32x4(dt, dt, dt, 0.0f) + accel * dt;\r\n"
        "        const f32x4 offset(1.0f, 2.0f, 3.0f, 0.0f);\r\n"
        "        step.w = 1.0f;\r\n"
        "        const f32x4 invStep = 1.0f / step;\r\n"
        "        const f32x4 dir = normalize(step.zyxw);\r\n"
        "        const f32x4 dir3 = normalize(step.xyz);\r\n"
        "        for (u32 i = 0u; i < steps; ++i) {\r\n"
        "            if (i == 0u) {\r\n"
        "                this.position += dir * step;\r\n"
        "            } else {\r\n"
        "                this.position += offset * dt;\r\n"
        "            }\r\n"
        "        }\r\n"
        "        return dot(this.position.xyz, dir3.xyz) + this.position.x + invStep[0u];\r\n"
        "    }\r\n"
        "    public f32x4 transformBatch(mut Span<f32x4> points, in f32x4x4 m0, in f32x4x4 m1, in f32x4 bias) const {\r\n"
        "        const f32x4x4 combinedT = transpose(mul(m0, m1));\r\n"
        "        parallel_for (mut auto pt : points) {\r\n"
        "            pt = mul(combinedT, pt) + bias;\r\n"
        "        }\r\n"
        "        return mul(combinedT, bias);\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult simdRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_SimdSource, "impl/physics/transform_body.adhd", arena.arena, simdRes));
    const MStringView cppView(simdRes.generatedSource.data(), simdRes.generatedSource.size());

    EXPECT_NE(cppView.find("const ::SIMDVectorSingle __adhd_simd_velocity = ::LoadFloat(velocity);"), MStringView::npos);
    EXPECT_NE(cppView.find("const ::SIMDVectorSingle __adhd_simd_accel = ::LoadFloat(accel);"), MStringView::npos);
    EXPECT_NE(cppView.find("::SIMDVectorSingle __adhd_simd_this_position = ::LoadFloat(this->position);"), MStringView::npos);
    EXPECT_NE(cppView.find("::SIMDVectorSingle __adhd_simd_step = ::VectorMultiplyAdd(__adhd_simd_velocity, ::VectorSet(dt, dt, dt, 0.0f), ::VectorScale(__adhd_simd_accel, dt));"), MStringView::npos);
    EXPECT_NE(cppView.find("const ::SIMDVectorSingle __adhd_simd_offset = ::VectorSet(1.0f, 2.0f, 3.0f, 0.0f);"), MStringView::npos);
    EXPECT_NE(cppView.find("__adhd_simd_step = ::VectorSetW(__adhd_simd_step, 1.0f);"), MStringView::npos);
    EXPECT_NE(cppView.find("const ::SIMDVectorSingle __adhd_simd_invStep = ::VectorReciprocal(__adhd_simd_step);"), MStringView::npos);
    EXPECT_NE(cppView.find("const ::SIMDVectorSingle __adhd_simd_dir = ::Vector4Normalize(::VectorSwizzle<2u, 1u, 0u, 3u>(__adhd_simd_step));"), MStringView::npos);
    EXPECT_NE(cppView.find("const ::SIMDVectorSingle __adhd_simd_dir3 = ::Vector3Normalize(__adhd_simd_step);"), MStringView::npos);
    EXPECT_NE(cppView.find("__adhd_simd_this_position = ::VectorMultiplyAdd(__adhd_simd_dir, __adhd_simd_step, __adhd_simd_this_position);"), MStringView::npos);
    EXPECT_NE(cppView.find("__adhd_simd_this_position = ::VectorMultiplyAdd(__adhd_simd_offset, ::VectorReplicate(dt), __adhd_simd_this_position);"), MStringView::npos);
    EXPECT_NE(cppView.find("::StoreFloat(__adhd_simd_this_position, this->position);"), MStringView::npos);
    EXPECT_NE(cppView.find("::VectorGetX(::Vector3Dot(__adhd_simd_this_position, __adhd_simd_dir3))"), MStringView::npos);
    EXPECT_NE(cppView.find("::VectorGetByIndex(__adhd_simd_invStep, static_cast<usize>(0u))"), MStringView::npos);

    EXPECT_NE(cppView.find("const ::SIMDMatrix __adhd_simd_combinedT = ::MatrixMultiplyTranspose(__adhd_simd_m0, __adhd_simd_m1);"), MStringView::npos);
    EXPECT_NE(cppView.find("__adhd_simd_pt = ::VectorAdd(::Vector4Transform(__adhd_simd_pt, __adhd_simd_combinedT), __adhd_simd_bias);"), MStringView::npos);
    EXPECT_NE(cppView.find("::StoreFloat(__adhd_simd_pt, pt);"), MStringView::npos);

    usize posLoadCount = 0u;
    usize searchPos = 0u;
    while((searchPos = cppView.find("::LoadFloat(this->position)", searchPos)) != MStringView::npos){
        ++posLoadCount;
        searchPos += 27u;
    }
    EXPECT_EQ(posLoadCount, 1u);

    usize posStoreCount = 0u;
    searchPos = 0u;
    while((searchPos = cppView.find("::StoreFloat(__adhd_simd_this_position, this->position)", searchPos)) != MStringView::npos){
        ++posStoreCount;
        searchPos += 55u;
    }
    EXPECT_EQ(posStoreCount, 1u);

    usize biasLoadCount = 0u;
    searchPos = 0u;
    while((searchPos = cppView.find("::LoadFloat(bias)", searchPos)) != MStringView::npos){
        ++biasLoadCount;
        searchPos += 17u;
    }
    EXPECT_EQ(biasLoadCount, 1u);
}

TEST(AdhdCompiler, SupportsFixedVectorLayoutStringArenaPathConvertStringAndMatrixReflection){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "[serialize]\r\n"
        "class PackedBatch {\r\n"
        "    public FixedVector<u32, 4> indices = FixedVector<u32, 4>();\r\n"
        "    public CompactString tag = \"default_tag\";\r\n"
        "    public f32x3_i32 packedSphere = f32x3_i32(1.0f, 2.0f, 3.0f, 7);\r\n"
        "    public f32x4x4 worldTransform = f32x4x4();\r\n"
        "}\r\n"
        "[reflect]\r\n"
        "class AssetManifest {\r\n"
        "    public String label = \"manifest\";\r\n"
        "    public Path rootPath = \"content/root\";\r\n"
        "    public void logEntry(in StringView entryName) {\r\n"
        "        String localNote = \"checking\";\r\n"
        "        Path sub = \"meshes/hero.nwb\";\r\n"
        "        this.rootPath = \"content/updated\";\r\n"
        "        const f32x4x4 customMat(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);\r\n"
        "        const f32x4 pt(1.0f, 2.0f, 3.0f, 1.0f);\r\n"
        "        const f32x4 outPt = mul(customMat, pt);\r\n"
        "        info(\"Entry {}: {} ({})\", entryName, localNote, outPt.x);\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/assets/manifest.adhd", arena.arena, res));
    ASSERT_EQ(res.unit.classes.size(), 2u);

    const auto* packedCls = res.unit.classes[0];
    EXPECT_FALSE(packedCls->needsClassArena);
    ASSERT_EQ(packedCls->orderedFields.size(), 4u);
    EXPECT_EQ(packedCls->orderedFields[0]->resolvedSize, 20u);
    EXPECT_EQ(packedCls->orderedFields[1]->resolvedSize, 256u);
    EXPECT_EQ(packedCls->orderedFields[2]->resolvedSize, 16u);
    EXPECT_EQ(packedCls->orderedFields[3]->resolvedSize, 64u);

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("FixedVector<u32, 4> indices = FixedVector<u32, 4>();"), MStringView::npos);
    EXPECT_NE(hdrView.find("Float44 worldTransform = Float44{};"), MStringView::npos);
    EXPECT_NE(hdrView.find("Float3Int(static_cast<f32>(__f->asList()[0].toDouble()), static_cast<f32>(__f->asList()[1].toDouble()), static_cast<f32>(__f->asList()[2].toDouble()), static_cast<i32>(__f->asList()[3].asInteger()))"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->worldTransform.raw[__i] = static_cast<f32>(__f->asList()[__i].toDouble());"), MStringView::npos);
    EXPECT_NE(hdrView.find("AString label{\"manifest\", s_ClassArena};"), MStringView::npos);
    EXPECT_NE(hdrView.find("Path rootPath = NWB_TEXT(\"content/root\");"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->rootPath = ::ConvertString(__f->asString());"), MStringView::npos);

    EXPECT_NE(cppView.find("AString localNote(\"checking\", __adhd_scratch);"), MStringView::npos);
    EXPECT_NE(cppView.find("Path sub = NWB_TEXT(\"meshes/hero.nwb\");"), MStringView::npos);
    EXPECT_NE(cppView.find("(this->rootPath = NWB_TEXT(\"content/updated\"));"), MStringView::npos);
    EXPECT_NE(cppView.find("const ::SIMDMatrix __adhd_simd_customMat = ::MatrixSet("), MStringView::npos);
    EXPECT_NE(cppView.find("::ConvertString(entryName), ::ConvertString(localNote)"), MStringView::npos);
}

TEST(AdhdCompiler, SupportsTypeAliasesSelfDefaultedFieldsBoundedOutOfLineAndListNwbReflection){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "type EntityId = u64;\r\n"
        "type Vec4 = f32x4;\r\n"
        "type NameMap<V> = HashMap<Name, V>;\r\n"
        "type TextureRef = AssetRef<TextureAsset>;\r\n"
        "interface IResettable {\r\n"
        "    void reset();\r\n"
        "}\r\n"
        "class BoundedBuffer<T : IResettable, const usize Capacity> {\r\n"
        "    public T item;\r\n"
        "    public FixedVector<u32, Capacity> slots;\r\n"
        "    public void clearSlots();\r\n"
        "}\r\n"
        "void BoundedBuffer<T : IResettable, const usize Capacity>::clearSlots() {\r\n"
        "    this.item.reset();\r\n"
        "    this.slots.clear();\r\n"
        "}\r\n"
        "[reflect]\r\n"
        "class SceneRecord {\r\n"
        "    public EntityId id = 1;\r\n"
        "    public Vec4 tint = Vec4(1.0f, 0.5f, 0.25f, 1.0f);\r\n"
        "    public TextureRef icon = TextureRef();\r\n"
        "    public Array<u32, 4> lodBias = Array<u32, 4>();\r\n"
        "    public FixedVector<f32, 8> weights;\r\n"
        "    public Optional<Func<void(in EntityId)>> onLoaded;\r\n"
        "    private NameMap<u32> m_lookup;\r\n"
        "    public f32 computeTintSum(in EntityId targetId, in Vec4 scale) const {\r\n"
        "        Optional<EntityId> maybeId;\r\n"
        "        Vec4 weighted = this.tint * scale;\r\n"
        "        return weighted.x + weighted.y;\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/scene/scene_record.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("NameMap<u32> m_lookup{s_ClassArena};"), MStringView::npos);
    EXPECT_NE(hdrView.find("Optional<::InplaceFunction<0u, void(EntityId)>> onLoaded;"), MStringView::npos);
    EXPECT_NE(hdrView.find("static constexpr usize assetDependencyFieldCount()noexcept{ return 1u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->lodBias[__i] = static_cast<u32>(__f->asList()[__i].asInteger());"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->weights.push_back(static_cast<f32>(__f->asList()[__i].toDouble()));"), MStringView::npos);
    EXPECT_NE(hdrView.find("f32 computeTintSum(const EntityId targetId, const Vec4& scale)const noexcept;"), MStringView::npos);

    EXPECT_NE(cppView.find("Optional<EntityId> maybeId{};"), MStringView::npos);
    EXPECT_NE(cppView.find("const ::SIMDVectorSingle __adhd_simd_this_tint = ::LoadFloat(this->tint);"), MStringView::npos);
    EXPECT_NE(cppView.find("::SIMDVectorSingle __adhd_simd_weighted = ::VectorMultiply(__adhd_simd_this_tint, __adhd_simd_scale);"), MStringView::npos);

    static constexpr MStringView s_InvalidNestedSerializeSource =
        "[serialize]\r\n"
        "class BadBinaryPacket {\r\n"
        "    public Array<String, 4> names = Array<String, 4>();\r\n"
        "}\r\n";
    CompileResult badSerRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_InvalidNestedSerializeSource, "impl/scene/bad_packet.adhd", arena.arena, badSerRes));

    static constexpr MStringView s_WaitAndOptionalReflectionSource =
        "[reflect]\r\n"
        "class TaskedAsset {\r\n"
        "    public AssetRef<ModelAsset> mesh;\r\n"
        "    public Array<i32, 6> buckets;\r\n"
        "    public Optional<u32> maxLod;\r\n"
        "    public Optional<Name> tag;\r\n"
        "    public void execute(mut u32 total) {\r\n"
        "        Array<i32, 6> localBuckets;\r\n"
        "        parallel StepA {\r\n"
        "            total += 1u;\r\n"
        "        }\r\n"
        "        wait(StepA);\r\n"
        "        parallel StepB {\r\n"
        "            total += 2u;\r\n"
        "        }\r\n"
        "        wait();\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult waitRes(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_WaitAndOptionalReflectionSource, "impl/scene/tasked_asset.adhd", arena.arena, waitRes));
    const MStringView waitHdr(waitRes.generatedHeader.data(), waitRes.generatedHeader.size());
    const MStringView waitCpp(waitRes.generatedSource.data(), waitRes.generatedSource.size());
    EXPECT_NE(waitHdr.find("::NWB::Core::Assets::AssetRef<ModelAsset> mesh;"), MStringView::npos);
    EXPECT_NE(waitHdr.find("Array<i32, 6> buckets{};"), MStringView::npos);
    EXPECT_NE(waitHdr.find("this->maxLod = static_cast<u32>(__f->asInteger());"), MStringView::npos);
    EXPECT_NE(waitHdr.find("::Name::FromText(__f->asString())"), MStringView::npos);
    EXPECT_NE(waitCpp.find("s_AdhdTaskScheduler.wait(__adhd_task_StepA);"), MStringView::npos);
    EXPECT_NE(waitCpp.find("__adhd_task_scope.wait();"), MStringView::npos);

    static constexpr MStringView s_InvalidWaitTargetSource =
        "void badWait() {\r\n"
        "    wait(NonExistentTask);\r\n"
        "}\r\n";
    CompileResult badWaitRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_InvalidWaitTargetSource, "impl/scene/bad_wait.adhd", arena.arena, badWaitRes));
}

TEST(AdhdCompiler, SupportsTemplatedInterfacesArenaCtorExprsAndStatefulFuncPropagation){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "enum<u8> ChannelState {\r\n"
        "    enum Closed = 0,\r\n"
        "    enum Open = 1,\r\n"
        "}\r\n"
        "interface ISink<T> {\r\n"
        "    void send(in T item);\r\n"
        "    void send(in T item, mut u32 count);\r\n"
        "}\r\n"
        "class IntSink : ISink<i32> {\r\n"
        "    public ChannelState state;\r\n"
        "    public Vector<i32> history = Vector<i32>(4);\r\n"
        "    public Optional<Func<i32(in i32)>> transform;\r\n"
        "    public void send(in i32 item) override {\r\n"
        "        this.history.push_back(item);\r\n"
        "    }\r\n"
        "    public void send(in i32 item, mut u32 count) override {\r\n"
        "        this.history.push_back(item);\r\n"
        "        count += 1u;\r\n"
        "    }\r\n"
        "    public void resetHistory(in i32 bias) {\r\n"
        "        ChannelState localState;\r\n"
        "        auto scratchVec = Vector<i32>(8);\r\n"
        "        this.history = Vector<i32>(16);\r\n"
        "        this.transform = lambda[in bias](in i32 x) -> i32 {\r\n"
        "            return x + bias;\r\n"
        "        };\r\n"
        "        auto boxed = cast<Optional<u32>>(0u);\r\n"
        "        auto stateCopy = ChannelState::Enum::Open;\r\n"
        "        u8 rawState = stateCopy::Enum::Open;\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/net/int_sink.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("class IntSink final : public ISink<i32>"), MStringView::npos);
    EXPECT_NE(hdrView.find("ChannelState::Enum state{};"), MStringView::npos);
    EXPECT_NE(hdrView.find("Vector<i32> history = Vector<i32>(4, s_ClassArena);"), MStringView::npos);
    EXPECT_NE(hdrView.find("Optional<::InplaceFunction<64u, i32(i32)>> transform;"), MStringView::npos);

    EXPECT_NE(cppView.find("ChannelState::Enum localState{};"), MStringView::npos);
    EXPECT_NE(cppView.find("auto scratchVec = Vector<i32>(8, __adhd_scratch);"), MStringView::npos);
    EXPECT_NE(cppView.find("(this->history = Vector<i32>(16, s_ClassArena));"), MStringView::npos);
    EXPECT_NE(cppView.find("static_cast<Optional<u32>>(0u)"), MStringView::npos);
    EXPECT_NE(cppView.find("ChannelState::Enum::Open"), MStringView::npos);

    static constexpr MStringView s_MismatchedTemplatedInterfaceSource =
        "interface ISink<T> {\r\n"
        "    void send(in T item);\r\n"
        "}\r\n"
        "class BadSink : ISink<i32> {\r\n"
        "    public void send(in f32 item) override {}\r\n"
        "}\r\n";
    CompileResult badSinkRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_MismatchedTemplatedInterfaceSource, "impl/net/bad_sink.adhd", arena.arena, badSinkRes));
}

TEST(AdhdCompiler, SupportsMethodAndInterfaceReflectionNativeCharLiteralsAndForwardDecls){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "i32 forwardHelper(in i32 x);\r\n"
        "[reflect, asset(\"index_contract\")]\r\n"
        "interface IIndexable {\r\n"
        "    [reflect]\r\n"
        "    i32 operator[](in usize idx) const;\r\n"
        "    usize count() const;\r\n"
        "}\r\n"
        "class IndexedBucket : IIndexable {\r\n"
        "    [reflect]\r\n"
        "    private i32 m_base = 0;\r\n"
        "    [inline]\r\n"
        "    public IndexedBucket() {}\r\n"
        "    [noinline]\r\n"
        "    public ~IndexedBucket();\r\n"
        "    [inline]\r\n"
        "    public i32 operator[](in usize idx) override const;\r\n"
        "    public usize count() const override {\r\n"
        "        return 4u;\r\n"
        "    }\r\n"
        "    public void inspectNative(mut String text) {\r\n"
        "        native(mut s = text) {\r\n"
        "            if(!s.empty() && s[0] == '{') {\r\n"
        "                s[0] = '}';\r\n"
        "            }\r\n"
        "        }\r\n"
        "    }\r\n"
        "}\r\n"
        "IndexedBucket::~IndexedBucket() {\r\n"
        "}\r\n"
        "i32 IndexedBucket::operator[](in usize idx) const {\r\n"
        "    return forwardHelper(this.m_base + cast<i32>(idx));\r\n"
        "}\r\n"
        "i32 forwardHelper(in i32 x) {\r\n"
        "    return x + 1;\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/data/indexed_bucket.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("s_AssetTypeName = \"index_contract\""), MStringView::npos);
    EXPECT_NE(hdrView.find("reflectedMethodCount()noexcept{ return 2u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("reflectedMethodCount()noexcept{ return 3u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("NWB_FORCE_INLINE IndexedBucket()noexcept"), MStringView::npos);
    EXPECT_NE(hdrView.find("NWB_NO_INLINE ~IndexedBucket()noexcept;"), MStringView::npos);
    EXPECT_NE(hdrView.find("NWB_FORCE_INLINE i32 operator[](const usize idx)const noexcept override"), MStringView::npos);
    EXPECT_NE(hdrView.find("inMap.findField(\"m_base\")"), MStringView::npos);

    EXPECT_NE(cppView.find("IndexedBucket::~IndexedBucket()noexcept"), MStringView::npos);
    EXPECT_NE(cppView.find("AStringView IndexedBucket::reflectedMethodName("), MStringView::npos);
    EXPECT_NE(cppView.find("if(!s.empty() && s[0] == '{')"), MStringView::npos);
    EXPECT_NE(cppView.find("i32 forwardHelper(const i32 x)noexcept"), MStringView::npos);

    static constexpr MStringView s_DuplicateFreeFnSource =
        "i32 dupFn(in i32 x) { return x; }\r\n"
        "i32 dupFn(in i32 x) { return x + 1; }\r\n";
    CompileResult dupFreeRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DuplicateFreeFnSource, "impl/data/dup_fn.adhd", arena.arena, dupFreeRes));

    static constexpr MStringView s_DuplicateClassMethodSource =
        "class DupMethod {\r\n"
        "    public i32 calc(in i32 x) const { return x; }\r\n"
        "    public i32 calc(in i32 y) const { return y; }\r\n"
        "}\r\n";
    CompileResult dupMethodRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DuplicateClassMethodSource, "impl/data/dup_method.adhd", arena.arena, dupMethodRes));
}

TEST(AdhdCompiler, EnforcesPrivateReflectionFilterSimdMutCallReloadsAndSafeLastUseMove){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "type IntVec = Vector<i32>;\r\n"
        "interface IRunnable {\r\n"
        "    void run();\r\n"
        "}\r\n"
        "[reflect]\r\n"
        "class JobNode : IRunnable {\r\n"
        "    public i32 visibleId = 1;\r\n"
        "    private i32 hiddenState = 2;\r\n"
        "    [reflect]\r\n"
        "    private i32 reflectedSecret = 3;\r\n"
        "    private void internalHelper() {}\r\n"
        "    [reflect]\r\n"
        "    private void reflectedHelper() {}\r\n"
        "    public void run() override {}\r\n"
        "}\r\n"
        "type JobNodeAlias = JobNode;\r\n"
        "class RunnerBox<T : IRunnable> {\r\n"
        "    public T item;\r\n"
        "}\r\n"
        "bool tweakSimd(mut f32x4 v) {\r\n"
        "    v = v + f32x4(1.0f, 1.0f, 1.0f, 1.0f);\r\n"
        "    return true;\r\n"
        "}\r\n"
        "void consumeVec(in IntVec v) {}\r\n"
        "void mutateVec(mut IntVec v) {\r\n"
        "    v.push_back(1);\r\n"
        "}\r\n"
        "f32 runPipeline(mut f32x4 pos, in f32x4 offset) {\r\n"
        "    RunnerBox<JobNodeAlias> box = RunnerBox<JobNodeAlias>();\r\n"
        "    pos = pos + offset;\r\n"
        "    const bool stepped = tweakSimd(pos);\r\n"
        "    native(mut p = pos) {\r\n"
        "        p.x += 2.0f;\r\n"
        "    }\r\n"
        "    pos = pos + offset;\r\n"
        "    IntVec movedVec;\r\n"
        "    consumeVec(movedVec);\r\n"
        "    IntVec mutOnlyVec;\r\n"
        "    mutateVec(mutOnlyVec);\r\n"
        "    if (stepped) {\r\n"
        "        return pos.x;\r\n"
        "    }\r\n"
        "    return 0.0f;\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/job/job_node.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("reflectedFieldCount()noexcept{ return 2u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("reflectedMethodCount()noexcept{ return 2u; }"), MStringView::npos);
    EXPECT_EQ(hdrView.find("inMap.findField(\"hiddenState\")"), MStringView::npos);
    EXPECT_NE(hdrView.find("inMap.findField(\"reflectedSecret\")"), MStringView::npos);

    EXPECT_EQ(cppView.find("return \"hiddenState\";"), MStringView::npos);
    EXPECT_NE(cppView.find("return \"reflectedSecret\";"), MStringView::npos);
    EXPECT_EQ(cppView.find("return \"internalHelper\";"), MStringView::npos);
    EXPECT_NE(cppView.find("return \"reflectedHelper\";"), MStringView::npos);
    EXPECT_NE(cppView.find("const bool stepped = tweakSimd(pos);\r\n    __adhd_simd_pos = ::LoadFloat(pos);"), MStringView::npos);
    EXPECT_NE(cppView.find("__adhd_simd_pos = ::LoadFloat(pos);\r\n    }"), MStringView::npos);
    EXPECT_NE(cppView.find("consumeVec(::Move(movedVec));"), MStringView::npos);
    EXPECT_NE(cppView.find("mutateVec(mutOnlyVec);"), MStringView::npos);

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
    EXPECT_FALSE(Compiler::compile(s_UnsafeParallelNativeMutContainer, "impl/job/bad_par_native.adhd", arena.arena, badParNativeRes));
}

TEST(AdhdCompiler, SupportsTemplateSpecializationArenaReflectionAndScalarMathLowering){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "enum<u8> ModeFlag {\r\n"
        "    flag Fast\r\n"
        "}\r\n"
        "interface IStore<T> {\r\n"
        "    void put(in T item);\r\n"
        "}\r\n"
        "class MemoryStore<T> : IStore<T> {\r\n"
        "    private i32 hiddenTag = 0;\r\n"
        "    [reflect]\r\n"
        "    private i32 reflectedTag = 1;\r\n"
        "    public T payload;\r\n"
        "    public f32x4 pos = f32x4(0.0f, 0.0f, 0.0f, 1.0f);\r\n"
        "    public f64x2 dpair = f64x2(1.0, 2.0);\r\n"
        "    public void put(in T item) override {\r\n"
        "        this.payload = item;\r\n"
        "    }\r\n"
        "}\r\n"
        "class RefHolder {\r\n"
        "    public in f32x4 targetPos;\r\n"
        "    public mut String targetName;\r\n"
        "    public in ModeFlag targetMode;\r\n"
        "    public RefHolder(in f32x4 p, mut String n, in ModeFlag m) {\r\n"
        "        this.targetPos = p;\r\n"
        "        this.targetName = n;\r\n"
        "        this.targetMode = m;\r\n"
        "    }\r\n"
        "}\r\n"
        "type VecStore = MemoryStore<Vector<i32>>;\r\n"
        "void inspectGeneric<T>(in MemoryStore<T> store) {\r\n"
        "}\r\n"
        "f32 computeScalarMath(in f32 a, in f32 b, in f32 t) {\r\n"
        "    VecStore store = VecStore();\r\n"
        "    const f32 blended = lerp(a, b, t);\r\n"
        "    const f32 snapped = round(blended);\r\n"
        "    return rsqrt(snapped + 1.0f);\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/store/memory_store.adhd", arena.arena, res));
    ASSERT_EQ(res.unit.templateInstantiations.size(), 1u);

    const auto* inst = res.unit.templateInstantiations[0];
    ASSERT_EQ(inst->orderedFields.size(), 5u);
    EXPECT_EQ(inst->orderedFields[0]->name, "dpair");
    EXPECT_EQ(inst->orderedFields[0]->resolvedAlign, 32u);
    EXPECT_EQ(inst->orderedFields[0]->resolvedSize, 32u);
    EXPECT_EQ(inst->orderedFields[1]->name, "pos");
    EXPECT_EQ(inst->orderedFields[1]->resolvedAlign, 16u);

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("template<>\r\nclass MemoryStore<Vector<i32>> final : public IStore<Vector<i32>>{"), MStringView::npos);
    EXPECT_EQ(hdrView.find("class MemoryStore<T> final : public IStore<T>{\r\nprivate:\r\n    using T ="), MStringView::npos);
    EXPECT_NE(hdrView.find("Vector<i32> payload{s_ClassArena};"), MStringView::npos);
    EXPECT_NE(hdrView.find("reflectedFieldCount()noexcept{ return 4u; }"), MStringView::npos);
    EXPECT_EQ(hdrView.find("return \"hiddenTag\";"), MStringView::npos);
    EXPECT_NE(hdrView.find("return \"reflectedTag\";"), MStringView::npos);
    EXPECT_NE(hdrView.find("::NotNull<const Float4*> targetPos;"), MStringView::npos);
    EXPECT_NE(hdrView.find("::NotNull<AString*> targetName;"), MStringView::npos);
    EXPECT_NE(hdrView.find("::NotNull<const ModeFlag::Enum*> targetMode;"), MStringView::npos);

    EXPECT_NE(cppView.find("const f32 blended = ((a) + ((b) - (a)) * (t));"), MStringView::npos);
    EXPECT_NE(cppView.find("const f32 snapped = std::round(blended);"), MStringView::npos);
    EXPECT_NE(cppView.find("return (static_cast<decltype((snapped + 1.0f))>(1) / ::Sqrt((snapped + 1.0f)));"), MStringView::npos);
}

TEST(AdhdCompiler, SupportsGlobalConstAndContainerIndexInferenceLambdaSyncAndOrderedReflection){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "enum<u8> QualityLevel {\r\n"
        "    enum Low = 0,\r\n"
        "    enum High = 1,\r\n"
        "}\r\n"
        "type Vec4Alias = f32x4;\r\n"
        "const f32x4 kGravity = f32x4(0.0f, -9.8f, 0.0f, 0.0f);\r\n"
        "[reflect]\r\n"
        "class ConfigPacket {\r\n"
        "    public u8 flagByte = 1;\r\n"
        "    public f32x4 origin = f32x4(0.0f, 0.0f, 0.0f, 1.0f);\r\n"
        "    public Array<QualityLevel, 2> tiers = Array<QualityLevel, 2>();\r\n"
        "    public FixedVector<bool, 4> toggles;\r\n"
        "    public Optional<QualityLevel> overrideQuality;\r\n"
        "    public Optional<AssetRef<TextureAsset>> fallbackTex;\r\n"
        "}\r\n"
        "f32 stepSimdWithLambdaAndContainers(in Vector<f32x4> positions, in Vector<String> labels, in f32x4 impulse, in f32 dt) {\r\n"
        "    auto readImpulseX = lambda[in impulse]() -> f32 {\r\n"
        "        return impulse.x;\r\n"
        "    };\r\n"
        "    const f64x3x3 rot3(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);\r\n"
        "    const f64x3 dpt = mul(rot3, f64x3(1.0, 2.0, 3.0));\r\n"
        "    const Vec4Alias unit = Vec4Alias(1.0f, 0.0f, 0.0f, 0.0f);\r\n"
        "    const f32x4 firstStepped = positions[0u] + (kGravity + impulse) * dt + unit;\r\n"
        "    for (in auto lbl : labels) {\r\n"
        "        info(\"label: {}\", lbl);\r\n"
        "    }\r\n"
        "    return firstStepped.y + readImpulseX() + cast<f32>(dpt.x);\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/config_packet.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("this->tiers[__i] = static_cast<QualityLevel::Enum>(__f->asList()[__i].asInteger());"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->toggles.push_back((__f->asList()[__i].asInteger() != 0));"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->overrideQuality = static_cast<QualityLevel::Enum>(__f->asInteger());"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->fallbackTex = {*__name};"), MStringView::npos);

    EXPECT_NE(cppView.find("if(index == 0u) return \"origin\";"), MStringView::npos);
    EXPECT_NE(cppView.find("const ::SIMDMatrixDouble __adhd_simd_rot3 = ::MatrixDoubleSet(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);"), MStringView::npos);
    EXPECT_NE(cppView.find("const ::SIMDVectorSingle __adhd_simd_unit = ::VectorSet(1.0f, 0.0f, 0.0f, 0.0f);"), MStringView::npos);
    EXPECT_NE(cppView.find("::LoadFloat(positions[0u])"), MStringView::npos);
    EXPECT_NE(cppView.find("::LoadFloat(kGravity)"), MStringView::npos);
    EXPECT_NE(cppView.find("::ConvertString(lbl)"), MStringView::npos);

    static constexpr MStringView s_UninitRefLocalSource =
        "void badUninitRef() {\r\n"
        "    mut String s;\r\n"
        "}\r\n";
    CompileResult badRefRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_UninitRefLocalSource, "impl/sim/bad_ref.adhd", arena.arena, badRefRes));

    static constexpr MStringView s_UnknownLambdaCaptureSource =
        "void badCapture() {\r\n"
        "    auto fn = lambda[in missingVar]() -> void {\r\n"
        "    };\r\n"
        "}\r\n";
    CompileResult badCapRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_UnknownLambdaCaptureSource, "impl/sim/bad_cap.adhd", arena.arena, badCapRes));
}

TEST(AdhdCompiler, SupportsTopLevelConstCtorStackedSwitchCasesLambdaThisAndMutSimdCaptureReload){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "const f32x4 kDefaultUp(0.0f, 1.0f, 0.0f, 0.0f);\r\n"
        "const Path kDefaultRoot = \"content/assets\";\r\n"
        "class WorkerNode {\r\n"
        "    public i32 scale = 2;\r\n"
        "    public i32 runWithThisLambda(in i32 inputVal) {\r\n"
        "        auto readScaled = lambda[in this](in i32 v) -> i32 {\r\n"
        "            return v * this.scale;\r\n"
        "        };\r\n"
        "        auto bumpScale = lambda[mut this](in i32 delta) -> void {\r\n"
        "            this.scale += delta;\r\n"
        "        };\r\n"
        "        bumpScale(1);\r\n"
        "        return readScaled(inputVal);\r\n"
        "    }\r\n"
        "}\r\n"
        "f32 evaluateModesAndMutSimdLambda(in Vector<StringView> modes, in f32x4 stepVec) {\r\n"
        "    f32x4 acc = kDefaultUp;\r\n"
        "    auto addStep = lambda[mut acc, in stepVec]() -> void {\r\n"
        "        acc += stepVec;\r\n"
        "    };\r\n"
        "    for (in auto mode : modes) {\r\n"
        "        switch (mode) {\r\n"
        "        case \"skip\":\r\n"
        "            continue;\r\n"
        "        case \"fast\":\r\n"
        "        case \"turbo\":\r\n"
        "            addStep();\r\n"
        "            break;\r\n"
        "        default:\r\n"
        "            break;\r\n"
        "        }\r\n"
        "    }\r\n"
        "    return acc.y;\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/worker_node.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("inline constexpr Float4 kDefaultUp(0.0f, 1.0f, 0.0f, 0.0f);"), MStringView::npos);
    EXPECT_NE(hdrView.find("inline constexpr Path kDefaultRoot = NWB_TEXT(\"content/assets\");"), MStringView::npos);

    EXPECT_NE(cppView.find("[this](const i32 v)noexcept -> i32"), MStringView::npos);
    EXPECT_NE(cppView.find("if(mode == \"skip\"){\r\n            continue;\r\n        }"), MStringView::npos);
    EXPECT_NE(cppView.find("else if(mode == \"fast\" || mode == \"turbo\"){\r\n            addStep();\r\n            __adhd_simd_acc = ::LoadFloat(acc);\r\n        }"), MStringView::npos);

    static constexpr MStringView s_UncapturedThisInLambdaSource =
        "class BadNode {\r\n"
        "    public i32 x = 1;\r\n"
        "    public i32 test() const {\r\n"
        "        auto fn = lambda() -> i32 {\r\n"
        "            return this.x;\r\n"
        "        };\r\n"
        "        return fn();\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badUncapturedThisRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_UncapturedThisInLambdaSource, "impl/sim/bad_this_cap.adhd", arena.arena, badUncapturedThisRes));

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
    EXPECT_FALSE(Compiler::compile(s_MutateInThisLambdaSource, "impl/sim/bad_in_this.adhd", arena.arena, badMutInThisRes));

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
    EXPECT_FALSE(Compiler::compile(s_DuplicateDefaultSwitchSource, "impl/sim/bad_dup_default.adhd", arena.arena, badDupDefaultRes));

    static constexpr MStringView s_BodylessOutOfLineSource =
        "class Box {\r\n"
        "    public void run();\r\n"
        "}\r\n"
        "void Box::run();\r\n";
    CompileResult badOolRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_BodylessOutOfLineSource, "impl/sim/bad_ool.adhd", arena.arena, badOolRes));
}

TEST(AdhdCompiler, SupportsBlockCommentsNumericLiteralMacroSafetyConstOverloadLinkingAndCycleValidation){
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
        "class DualAccessor {\r\n"
        "    private i32 secret = 5;\r\n"
        "    public i32 readVal(in usize idx) const;\r\n"
        "    private i32 readVal(in usize idx);\r\n"
        "}\r\n"
        "[reflect]\r\n"
        "i32 DualAccessor::readVal(in usize idx) {\r\n"
        "    this.secret += cast<i32>(idx);\r\n"
        "    return this.secret;\r\n"
        "}\r\n"
        "i32 DualAccessor::readVal(in usize idx) const {\r\n"
        "    f32 scale = 1.0f;\r\n"
        "    u32 mask = 0x10u + 10u;\r\n"
        "    return this.secret + cast<i32>(idx) + cast<i32>(scale) + cast<i32>(mask);\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "/abs/work/dual_accessor.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("namespace NWB{"), MStringView::npos);
    EXPECT_NE(hdrView.find("reflectedMethodCount()noexcept{ return 2u; }"), MStringView::npos);
    EXPECT_NE(cppView.find("f32 scale = 1.0f;"), MStringView::npos);
    EXPECT_NE(cppView.find("u32 mask = (0x10u + 10u);"), MStringView::npos);

    const MString relNs = Compiler::deriveNamespaceFromPath("impl/2d_scene/node.adhd", arena.arena);
    EXPECT_EQ(MStringView(relNs.data(), relNs.size()), "NWB::Impl::_2dScene");

    static constexpr MStringView s_DuplicateAttrSource =
        "[inline, inline]\r\n"
        "void dupAttr() {}\r\n";
    CompileResult dupAttrRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DuplicateAttrSource, "impl/sim/dup_attr.adhd", arena.arena, dupAttrRes));

    static constexpr MStringView s_CyclicInterfaceSource =
        "interface IAlpha : IBeta {\r\n"
        "    void step();\r\n"
        "}\r\n"
        "interface IBeta : IAlpha {\r\n"
        "    void tick();\r\n"
        "}\r\n";
    CompileResult cyclicIfaceRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_CyclicInterfaceSource, "impl/sim/cyclic_iface.adhd", arena.arena, cyclicIfaceRes));

    static constexpr MStringView s_DuplicateTemplateParamSource =
        "class BadPair<T, T> {\r\n"
        "    public T value;\r\n"
        "}\r\n";
    CompileResult dupTParamRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DuplicateTemplateParamSource, "impl/sim/dup_tparam.adhd", arena.arena, dupTParamRes));

    static constexpr MStringView s_DuplicateNativeBindingSource =
        "void badNative(in i32 a, mut i32 b) {\r\n"
        "    native(in a, mut a = b) {\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult dupNativeRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DuplicateNativeBindingSource, "impl/sim/dup_native.adhd", arena.arena, dupNativeRes));
}

TEST(AdhdCompiler, SupportsReturnFuncCapturePropagationEnumAliasScopeOverloadedInterfaceAndConstMutationChecks){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "const u32 kGlobalLimit = 100u;\r\n"
        "enum<u8> StageMask {\r\n"
        "    enum Idle = 0,\r\n"
        "    flag Active = 1 << 0,\r\n"
        "}\r\n"
        "type StageMaskAlias = StageMask;\r\n"
        "interface IProcessor {\r\n"
        "    void process(in i32 v);\r\n"
        "    void process(in f32 v);\r\n"
        "}\r\n"
        "class DualProcessor : IProcessor {\r\n"
        "    public f32 lastFloat = 0.0f;\r\n"
        "    public i32 lastInt = 0;\r\n"
        "    public void process(in f32 v) override {\r\n"
        "        this.lastFloat = v;\r\n"
        "    }\r\n"
        "    public void process(in i32 v) override {\r\n"
        "        this.lastInt = v;\r\n"
        "    }\r\n"
        "}\r\n"
        "Func<i32(in i32)> makeAdder(in i32 base) {\r\n"
        "    return lambda[in base](in i32 x) -> i32 {\r\n"
        "        return x + base;\r\n"
        "    };\r\n"
        "}\r\n"
        "Path defaultConfigPath() {\r\n"
        "    return \"content/default.nwb\";\r\n"
        "}\r\n"
        "u8 readActiveStageMask() {\r\n"
        "    return StageMaskAlias::Flag::Active;\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/dual_processor.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("::InplaceFunction<64u, i32(i32)> makeAdder(const i32 base)noexcept;"), MStringView::npos);
    EXPECT_NE(cppView.find("return NWB_TEXT(\"content/default.nwb\");"), MStringView::npos);
    EXPECT_NE(cppView.find("return StageMask::Flag::Active;"), MStringView::npos);

    static constexpr MStringView s_MutateGlobalConstSource =
        "const u32 kImmutable = 10u;\r\n"
        "void badMutateGlobal() {\r\n"
        "    kImmutable = 20u;\r\n"
        "}\r\n";
    CompileResult mutGlobalRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_MutateGlobalConstSource, "impl/sim/bad_mut_global.adhd", arena.arena, mutGlobalRes));

    static constexpr MStringView s_MissingReturnSource =
        "i32 missingReturn(in i32 x) {\r\n"
        "    i32 y = x + 1;\r\n"
        "}\r\n";
    CompileResult missingRetRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_MissingReturnSource, "impl/sim/bad_missing_ret.adhd", arena.arena, missingRetRes));

    static constexpr MStringView s_MutateParallelForIndexSource =
        "void badParIndexMut() {\r\n"
        "    parallel_for (u32 i = 0u; i < 8u; ++i) {\r\n"
        "        i += 1u;\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult parIdxMutRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_MutateParallelForIndexSource, "impl/sim/bad_par_idx_mut.adhd", arena.arena, parIdxMutRes));

    static constexpr MStringView s_InvalidNonEnumScopeAccessSource =
        "class Dummy {\r\n"
        "    public u32 x = 0u;\r\n"
        "}\r\n"
        "u32 badScope() {\r\n"
        "    return Dummy::x;\r\n"
        "}\r\n";
    CompileResult badScopeRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_InvalidNonEnumScopeAccessSource, "impl/sim/bad_non_enum_scope.adhd", arena.arena, badScopeRes));
}

TEST(AdhdCompiler, SupportsReferenceReturnTypesConstantTemplateArgsAndOptionalSimdReflection){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "const usize kBatchCap(0x08u);\r\n"
        "interface IWorker {\r\n"
        "    u32 run();\r\n"
        "}\r\n"
        "interface IWorkerProvider {\r\n"
        "    in IWorker currentWorker() const;\r\n"
        "    mut IWorker mutableWorker();\r\n"
        "}\r\n"
        "class SimpleWorker : IWorker {\r\n"
        "    public u32 count = 1u;\r\n"
        "    public u32 run() override {\r\n"
        "        this.count += 1u;\r\n"
        "        return this.count;\r\n"
        "    }\r\n"
        "}\r\n"
        "[reflect]\r\n"
        "class BatchHolder<T, const usize Cap> {\r\n"
        "    public FixedVector<T, Cap> items;\r\n"
        "    public Optional<f32x4> boundsTint;\r\n"
        "    public u32 count = 0u;\r\n"
        "}\r\n"
        "class WorkerHost : IWorkerProvider {\r\n"
        "    public SimpleWorker worker;\r\n"
        "    public BatchHolder<u32, kBatchCap> batch;\r\n"
        "    public in IWorker currentWorker() const override {\r\n"
        "        return this.worker;\r\n"
        "    }\r\n"
        "    public mut IWorker mutableWorker() override {\r\n"
        "        return this.worker;\r\n"
        "    }\r\n"
        "}\r\n"
        "in IWorker selectWorker(in WorkerHost host) {\r\n"
        "    return host.currentWorker();\r\n"
        "}\r\n"
        "u32 invokeProvider(mut WorkerHost host, in u32 a, in u32 b) {\r\n"
        "    bool cmp = a < b >> 1u;\r\n"
        "    Func<mut IWorker(mut WorkerHost)> getter = lambda(mut WorkerHost h) -> mut IWorker {\r\n"
        "        return h.mutableWorker();\r\n"
        "    };\r\n"
        "    if (cmp) {\r\n"
        "        return getter(host).run();\r\n"
        "    }\r\n"
        "    return host.mutableWorker().run();\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/ref_return_and_const_args.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("virtual ::NotNull<const IWorker*> currentWorker()const noexcept = 0;"), MStringView::npos);
    EXPECT_NE(hdrView.find("virtual ::NotNull<IWorker*> mutableWorker()noexcept = 0;"), MStringView::npos);
    EXPECT_NE(hdrView.find("::NotNull<const IWorker*> selectWorker(const WorkerHost& host)noexcept;"), MStringView::npos);
    EXPECT_NE(hdrView.find("FixedVector<T, Cap> items;"), MStringView::npos);
    EXPECT_NE(cppView.find("return ::MakeNotNull(&(this->worker));"), MStringView::npos);
    EXPECT_NE(cppView.find("return ::MakeNotNull(&((*host.currentWorker())));"), MStringView::npos);
    EXPECT_NE(cppView.find("return getter(host)->run();"), MStringView::npos);
    EXPECT_NE(cppView.find("return host.mutableWorker()->run();"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->boundsTint = Float4(static_cast<f32>(__f->asList()[0].toDouble())"), MStringView::npos);

    static constexpr MStringView s_ReturnInParamAsMutRefSource =
        "mut i32 badMutReturn(in i32 x) {\r\n"
        "    return x;\r\n"
        "}\r\n";
    CompileResult badMutRetRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_ReturnInParamAsMutRefSource, "impl/sim/bad_mut_ret.adhd", arena.arena, badMutRetRes));

    static constexpr MStringView s_MutateInReturnValueSource =
        "in i32 getConstRef(in i32 x) {\r\n"
        "    return x;\r\n"
        "}\r\n"
        "void badMutateConstRef(in i32 a) {\r\n"
        "    getConstRef(a) = 5;\r\n"
        "}\r\n";
    CompileResult badMutConstCallRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_MutateInReturnValueSource, "impl/sim/bad_mut_const_call.adhd", arena.arena, badMutConstCallRes));

    static constexpr MStringView s_InvalidVectorAritySource =
        "void badVecArity() {\r\n"
        "    Vector<i32, i32> v;\r\n"
        "}\r\n";
    CompileResult badVecArityRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_InvalidVectorAritySource, "impl/sim/bad_vec_arity.adhd", arena.arena, badVecArityRes));
}

TEST(AdhdCompiler, SupportsParallelCallTemplatesExtendedNwbReflectionAndStrictVoidInterfaceWaitChecks){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "[reflect]\r\n"
        "class SubConfig {\r\n"
        "    public u32 level = 1u;\r\n"
        "}\r\n"
        "[reflect]\r\n"
        "class AssetManifest {\r\n"
        "    public Path rootPath = \"assets/root\";\r\n"
        "    public Optional<String> label;\r\n"
        "    public Optional<SubConfig> subConfig;\r\n"
        "    public FixedVector<SubConfig, 4> stages;\r\n"
        "    public Vector<String> tags;\r\n"
        "}\r\n"
        "void runStage<T>(in T item) {\r\n"
        "}\r\n"
        "Path resolveAssetPath(in String relPath) {\r\n"
        "    Path out = relPath;\r\n"
        "    return out;\r\n"
        "}\r\n"
        "void dispatchWork(in u32 seed) {\r\n"
        "    parallel TaskA {\r\n"
        "    }\r\n"
        "    parallel runStage<u32>(seed);\r\n"
        "    wait(TaskA);\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/parallel_templates_and_nwb.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("inline static ::NWB::Core::Alloc::GlobalArena s_ClassArena{::Name(\"AssetManifest\")};"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->rootPath = ::ConvertString(__f->asString());"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->label = AString(__f->asString(), s_ClassArena);"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->subConfig.emplace(); if(!this->subConfig->deserializeNwb(*__f))"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->stages.emplace_back(); if(!this->stages.back().deserializeNwb(__f->asList()[__i])) return false;"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->tags.push_back(AString(__f->asList()[__i].asString(), s_ClassArena));"), MStringView::npos);
    EXPECT_NE(cppView.find("Path out = ::ConvertString(relPath);"), MStringView::npos);
    EXPECT_NE(cppView.find("runStage<u32>(__adhd_arg_0);"), MStringView::npos);

    static constexpr MStringView s_VoidVariableSource =
        "void badVoidVar() {\r\n"
        "    void x = 0;\r\n"
        "}\r\n";
    CompileResult badVoidVarRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_VoidVariableSource, "impl/sim/bad_void_var.adhd", arena.arena, badVoidVarRes));

    static constexpr MStringView s_ByValueInterfaceTemplateArgSource =
        "interface IJob {\r\n"
        "    void tick();\r\n"
        "}\r\n"
        "void badIfaceVec() {\r\n"
        "    Vector<IJob> jobs;\r\n"
        "}\r\n";
    CompileResult badIfaceVecRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_ByValueInterfaceTemplateArgSource, "impl/sim/bad_iface_vec.adhd", arena.arena, badIfaceVecRes));

    static constexpr MStringView s_WaitInsideParallelSource =
        "void badParallelWait() {\r\n"
        "    parallel TaskA {\r\n"
        "        wait();\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badParWaitRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_WaitInsideParallelSource, "impl/sim/bad_par_wait.adhd", arena.arena, badParWaitRes));

    static constexpr MStringView s_DuplicateWaitTargetSource =
        "void badDupWait() {\r\n"
        "    parallel TaskA {\r\n"
        "    }\r\n"
        "    wait(TaskA, TaskA);\r\n"
        "}\r\n";
    CompileResult badDupWaitRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DuplicateWaitTargetSource, "impl/sim/bad_dup_wait.adhd", arena.arena, badDupWaitRes));
}

TEST(AdhdCompiler, SupportsTemplatedTypeAliasUnwrappingSwitchSubjectCachingThisMethodSimdSyncAndCycleChecks){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "type VecOf<T> = Vector<T>;\r\n"
        "type OptOf<T> = Optional<T>;\r\n"
        "[reflect]\r\n"
        "class SimBody {\r\n"
        "    public f32x4 pos = f32x4(0.0f);\r\n"
        "    public OptOf<u32> badge;\r\n"
        "    public void helper();\r\n"
        "    public void step(in VecOf<f32x4> impulses);\r\n"
        "}\r\n"
        "void SimBody::helper() {\r\n"
        "    this.pos = this.pos + f32x4(1.0f);\r\n"
        "}\r\n"
        "void SimBody::step(in VecOf<f32x4> impulses) {\r\n"
        "    this.pos = this.pos + impulses[0];\r\n"
        "    this.helper();\r\n"
        "}\r\n"
        "String fetchTag() {\r\n"
        "    return \"alpha\";\r\n"
        "}\r\n"
        "u32 dispatchByTag(in bool fastPath) {\r\n"
        "    u32 result = 0u;\r\n"
        "    switch (fetchTag()) {\r\n"
        "    case \"alpha\":\r\n"
        "        if (fastPath) {\r\n"
        "            result = 1u;\r\n"
        "            break;\r\n"
        "        } else {\r\n"
        "            result = 2u;\r\n"
        "            break;\r\n"
        "        }\r\n"
        "    default:\r\n"
        "        result = 3u;\r\n"
        "        break;\r\n"
        "    }\r\n"
        "    return result;\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/templated_alias_switch_simd_sync.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("this->badge = static_cast<u32>(__f->asInteger());"), MStringView::npos);
    EXPECT_NE(cppView.find("::StoreFloat(__adhd_simd_this_pos, this->pos);"), MStringView::npos);
    EXPECT_NE(cppView.find("this->helper();"), MStringView::npos);
    EXPECT_NE(cppView.find("__adhd_simd_this_pos = ::LoadFloat(this->pos);"), MStringView::npos);
    EXPECT_NE(cppView.find("const auto& __adhd_switch_subject = (fetchTag());"), MStringView::npos);
    EXPECT_EQ(cppView.find("break;"), MStringView::npos);

    static constexpr MStringView s_CyclicTypeAliasSource =
        "type AliasA = AliasB;\r\n"
        "type AliasB = AliasA;\r\n";
    CompileResult cyclicAliasRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_CyclicTypeAliasSource, "impl/sim/cyclic_alias.adhd", arena.arena, cyclicAliasRes));

    static constexpr MStringView s_RecursiveClassValueMemberSource =
        "class BadNode {\r\n"
        "    public Optional<BadNode> next;\r\n"
        "}\r\n";
    CompileResult recClassRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_RecursiveClassValueMemberSource, "impl/sim/rec_class.adhd", arena.arena, recClassRes));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

