// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        "    public b8 operator==(in TagKey rhs)const {\r\n"
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
        "    u32 id()const;\r\n"
        "}\r\n"
        "class Converter {\r\n"
        "    private Vector<i32> m_buffer(8);\r\n"
        "    public Out convert<Out>(in u32 x)const {\r\n"
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
        "    u32 id()const;\r\n"
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
    EXPECT_NE(hdrView.find("StoreClient(IStore<i32>& store)noexcept : m_store(::MakeNotNull(&(store)))"), MStringView::npos);
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
        "    public Mask operator~(in Mask other)const {\r\n"
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
        "    public u32 readBad()const {\r\n"
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

TEST(AdhdCompiler, SupportsCrossFileSplitFunctionsTemplateClassOutOfLineMethodsAndTransitiveIncludes){
    AdhdTestArena arena;

    class TransitiveSplitResolver final : public NWB::Core::Metascript::IIncludeResolver{
    public:
        u32 popCount = 0u;

        [[nodiscard]] b8 resolveInclude(
            const MStringView includePath,
            const b8,
            MetaArena& rArena,
            MString& outResolvedKey,
            MString& outContent
        )override{
            if(includePath == "split_decl.adhd"){
                outResolvedKey = MString("/abs/split_decl.adhd", rArena);
                static constexpr MStringView s_DeclCode =
                    "#include \"sub/leaf.adhd\"\r\n"
                    "i32 computeCrossFile(in i32 x);\r\n"
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

        void popIncludeScope()noexcept override{
            ++popCount;
        }
    };

    static constexpr MStringView s_ImplSource =
        "#include \"split_decl.adhd\"\r\n"
        "i32 computeCrossFile(in i32 x) {\r\n"
        "    return x + s_LeafBias;\r\n"
        "}\r\n"
        "T CrossBox<T>::fetch()const {\r\n"
        "    return this.m_item;\r\n"
        "}\r\n";

    TransitiveSplitResolver resolver;
    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_ImplSource, "impl/split_impl.adhd", arena.arena, res, &resolver));

    EXPECT_EQ(resolver.popCount, 2u);
    ASSERT_EQ(res.unit.includedAdhdFiles.size(), 1u);
    EXPECT_EQ(MStringView(res.unit.includedAdhdFiles[0].data(), res.unit.includedAdhdFiles[0].size()), MStringView("split_decl.adhd"));
    ASSERT_EQ(res.unit.resolvedIncludedFiles.size(), 2u);
    EXPECT_EQ(MStringView(res.unit.resolvedIncludedFiles[0].data(), res.unit.resolvedIncludedFiles[0].size()), MStringView("/abs/split_decl.adhd"));
    EXPECT_EQ(MStringView(res.unit.resolvedIncludedFiles[1].data(), res.unit.resolvedIncludedFiles[1].size()), MStringView("/abs/sub/leaf.adhd"));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("#include \"split_decl.adhd.h\""), MStringView::npos);
    EXPECT_EQ(hdrView.find("#include \"sub/leaf.adhd.h\""), MStringView::npos);
    EXPECT_NE(hdrView.find("template<typename T>\r\ninline T CrossBox<T>::fetch()const noexcept"), MStringView::npos);
    EXPECT_NE(cppView.find("i32 computeCrossFile(const i32 x)noexcept"), MStringView::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

