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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

