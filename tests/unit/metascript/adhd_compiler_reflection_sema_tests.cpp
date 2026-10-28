// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

TEST(AdhdCompiler, SupportsParameterizedInterfaceBoundsInheritedTemplateInterfacesRefAliasesLambdaArenasAndSimdNwbReflection){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "[asset(\"Mesh\")]\r\n"
        "class MeshAsset {\r\n"
        "    public u32 vertexCount = 0u;\r\n"
        "}\r\n"
        "interface IBaseStream<T> {\r\n"
        "    T sample() const;\r\n"
        "}\r\n"
        "interface IDerivedStream<U> : IBaseStream<U> {\r\n"
        "    void push(in U item);\r\n"
        "}\r\n"
        "type VecStreamIn = in IDerivedStream<f32x4>;\r\n"
        "class VecStreamImpl : IDerivedStream<f32x4> {\r\n"
        "    public f32x4 current = f32x4(1.0f);\r\n"
        "    public f32x4 sample() const override {\r\n"
        "        return this.current;\r\n"
        "    }\r\n"
        "    public void push(in f32x4 item) override {\r\n"
        "        this.current = item;\r\n"
        "    }\r\n"
        "}\r\n"
        "class Holder<T> {\r\n"
        "    public T value;\r\n"
        "    public T get() const {\r\n"
        "        return this.value;\r\n"
        "    }\r\n"
        "}\r\n"
        "class StreamConsumer {\r\n"
        "    public VecStreamIn stream;\r\n"
        "    public StreamConsumer(in VecStreamIn s) {\r\n"
        "        this.stream = s;\r\n"
        "    }\r\n"
        "}\r\n"
        "[reflect]\r\n"
        "class SimdNwbPacket {\r\n"
        "    public Vector<f32x4> points;\r\n"
        "    public Optional<f32x4x4> transform;\r\n"
        "    public Vector<AssetRef<MeshAsset>> meshes;\r\n"
        "}\r\n"
        "f32x4 readBounded<S : IDerivedStream<f32x4>>(in S src, in Holder<f32x4> h, in Optional<f32x4> opt) {\r\n"
        "    VecStreamIn aliasRef = src;\r\n"
        "    auto a = src.sample();\r\n"
        "    auto b = h.get();\r\n"
        "    auto c = opt.value();\r\n"
        "    auto localFn = lambda() -> u32 {\r\n"
        "        Vector<u32> scratchItems;\r\n"
        "        scratchItems.push_back(7u);\r\n"
        "        return 1u;\r\n"
        "    };\r\n"
        "    u32 tag = localFn();\r\n"
        "    return a + b + c + aliasRef.sample();\r\n"
        "}\r\n"
        "f32x4 runBounded(in VecStreamImpl impl, in Holder<f32x4> h, in Optional<f32x4> opt) {\r\n"
        "    return readBounded<VecStreamImpl>(impl, h, opt);\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/param_bounds_ref_alias_nwb.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("static constexpr usize reflectedMethodCount()noexcept{ return 2u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("using VecStreamIn = ::NotNull<const IDerivedStream<Float4>*>;"), MStringView::npos);
    EXPECT_NE(hdrView.find("static constexpr usize assetDependencyFieldCount()noexcept{ return 1u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->points.push_back(Float4("), MStringView::npos);
    EXPECT_NE(hdrView.find("Float44 __m{}; for(usize __i = 0u; __i < 16u; ++__i) __m.raw[__i] = static_cast<f32>(__f->asList()[__i].toDouble()); this->transform = __m;"), MStringView::npos);
    EXPECT_NE(hdrView.find("::SIMDVectorSingle __adhd_simd_a = ::LoadFloat(src.sample());"), MStringView::npos);
    EXPECT_NE(hdrView.find("::SIMDVectorSingle __adhd_simd_b = ::LoadFloat(h.get());"), MStringView::npos);
    EXPECT_NE(hdrView.find("::SIMDVectorSingle __adhd_simd_c = ::LoadFloat(opt.value());"), MStringView::npos);
    EXPECT_NE(hdrView.find("::NWB::Core::Alloc::ScratchArena __adhd_scratch(::Name(\"lambda\"));"), MStringView::npos);
    EXPECT_NE(cppView.find("StreamConsumer::StreamConsumer(const VecStreamIn& s)noexcept : stream(::MakeNotNull(&((*s))))"), MStringView::npos);

    static constexpr MStringView s_WrongParameterizedBoundSource =
        "interface IStream<T> {\r\n"
        "    T read() const;\r\n"
        "}\r\n"
        "class IntStream : IStream<i32> {\r\n"
        "    public i32 read() const override { return 0; }\r\n"
        "}\r\n"
        "f32 consumeFloat<S : IStream<f32>>(in S s) {\r\n"
        "    return s.read();\r\n"
        "}\r\n"
        "f32 badCall(in IntStream is) {\r\n"
        "    return consumeFloat<IntStream>(is);\r\n"
        "}\r\n";
    CompileResult wrongBoundRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_WrongParameterizedBoundSource, "impl/sim/wrong_param_bound.adhd", arena.arena, wrongBoundRes));

    static constexpr MStringView s_DoubleRefAliasSource =
        "interface IFoo {\r\n"
        "    void tick();\r\n"
        "}\r\n"
        "type FooRef = in IFoo;\r\n"
        "void badDoubleRef(in FooRef f) {\r\n"
        "    mut FooRef local = f;\r\n"
        "}\r\n";
    CompileResult doubleRefRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_DoubleRefAliasSource, "impl/sim/double_ref_alias.adhd", arena.arena, doubleRefRes));

    static constexpr MStringView s_EscapingMutLambdaSource =
        "Func<i32()> badReturnMutLambda() {\r\n"
        "    i32 counter = 0;\r\n"
        "    return lambda[mut counter]() -> i32 {\r\n"
        "        counter += 1;\r\n"
        "        return counter;\r\n"
        "    };\r\n"
        "}\r\n";
    CompileResult escapingLambdaRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_EscapingMutLambdaSource, "impl/sim/escaping_mut_lambda.adhd", arena.arena, escapingLambdaRes));
}

TEST(AdhdCompiler, SupportsMacroIfTopoEmitMultiHopLayoutAccessControlAndNotNullPropagation){
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
        "    i32 derivedId() const;\r\n"
        "}\r\n"
        "interface IBaseNode {\r\n"
        "    i32 baseId() const;\r\n"
        "}\r\n"
        "\r\n"
        "class ConcreteNode : IDerivedNode {\r\n"
        "    private i32 secret = 42;\r\n"
        "    public i32 baseId() const override { return this.secret; }\r\n"
        "    public i32 derivedId() const override { return this.secret + 1; }\r\n"
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
        "}\r\n"
        "\r\n"
        "[reflect]\r\n"
        "class ContainerAssetBundle {\r\n"
        "    public Set<Name> tags;\r\n"
        "    public Map<String, i32> weights;\r\n"
        "    public HashMap<Name, AssetRef<LeafInner>> namedDeps;\r\n"
        "    public Deque<f32> timeline;\r\n"
        "}\r\n"
        "\r\n"
        "class NodeHolder {\r\n"
        "    public in IDerivedNode nodeRef;\r\n"
        "    public NodeHolder(in IDerivedNode n) {\r\n"
        "        this.nodeRef = n;\r\n"
        "    }\r\n"
        "}\r\n"
        "\r\n"
        "AliasB evaluateNodes(in NodeHolder holder, in Vector<in IDerivedNode> list) {\r\n"
        "    in auto localRef = holder.nodeRef;\r\n"
        "    AliasB sum = localRef.derivedId() + holder.nodeRef.baseId() + list[0].derivedId();\r\n"
        "    for (in IDerivedNode item : list) {\r\n"
        "        sum += item.baseId();\r\n"
        "    }\r\n"
        "    return sum;\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/topo_layout_access.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

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

    ASSERT_EQ(res.unit.classes.size(), 6u);
    EXPECT_EQ(res.unit.classes[1]->name, "GrandOuter");
    EXPECT_EQ(res.unit.classes[1]->totalAlign, 8u);
    EXPECT_EQ(res.unit.classes[1]->totalSize, 24u);

    EXPECT_NE(hdrView.find("static constexpr usize assetDependencyFieldCount()noexcept{ return 1u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->tags.clear(); for(usize __i = 0u; __i < __f->asList().size(); ++__i) this->tags.insert(::Name(__f->asList()[__i].asString().data()));"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->weights.clear(); for(const auto& [__k, __v] : __f->asMap()) this->weights[AString(__k, s_ClassArena)] = static_cast<i32>(__v.asInteger());"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->namedDeps.clear(); for(const auto& [__k, __v] : __f->asMap()) this->namedDeps[::Name(__k.data())] = {::Name(__v.asString().data())};"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->timeline.push_back(static_cast<f32>(__f->asList()[__i].toDouble()));"), MStringView::npos);

    EXPECT_NE(cppView.find("::NotNull<const IDerivedNode*> localRef = ::MakeNotNull(&((*(holder.nodeRef))));"), MStringView::npos);
    EXPECT_NE(cppView.find("localRef->derivedId()"), MStringView::npos);
    EXPECT_NE(cppView.find("holder.nodeRef->baseId()"), MStringView::npos);
    EXPECT_NE(cppView.find("list[0]->derivedId()"), MStringView::npos);
    EXPECT_NE(cppView.find("for(const ::NotNull<const IDerivedNode*>& item : list)"), MStringView::npos);
    EXPECT_NE(cppView.find("item->baseId()"), MStringView::npos);

    static constexpr MStringView s_PrivateFieldSource =
        "class SecretBox {\r\n"
        "    private i32 hidden = 7;\r\n"
        "}\r\n"
        "i32 leak(in SecretBox b) {\r\n"
        "    return b.hidden;\r\n"
        "}\r\n";
    CompileResult privFieldRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_PrivateFieldSource, "impl/sim/priv_field.adhd", arena.arena, privFieldRes));

    static constexpr MStringView s_PrivateMethodSource =
        "class SecretBox {\r\n"
        "    private i32 calc() const { return 7; }\r\n"
        "}\r\n"
        "i32 leak(in SecretBox b) {\r\n"
        "    return b.calc();\r\n"
        "}\r\n";
    CompileResult privMethodRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_PrivateMethodSource, "impl/sim/priv_method.adhd", arena.arena, privMethodRes));

    static constexpr MStringView s_PrivateCtorSource =
        "class SecretBox {\r\n"
        "    public i32 v = 0;\r\n"
        "    private SecretBox(in i32 x) { this.v = x; }\r\n"
        "}\r\n"
        "void make() {\r\n"
        "    SecretBox b(5);\r\n"
        "}\r\n";
    CompileResult privCtorRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_PrivateCtorSource, "impl/sim/priv_ctor.adhd", arena.arena, privCtorRes));

    static constexpr MStringView s_AutoInterfaceByValueSource =
        "interface INode {\r\n"
        "    i32 id() const;\r\n"
        "}\r\n"
        "void badAuto(in INode n) {\r\n"
        "    auto copy = n;\r\n"
        "}\r\n";
    CompileResult autoIfaceRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_AutoInterfaceByValueSource, "impl/sim/auto_iface.adhd", arena.arena, autoIfaceRes));

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
    EXPECT_FALSE(Compiler::compile(s_MutRebindConstSource, "impl/sim/mut_rebind_const.adhd", arena.arena, mutRebindRes));
}

TEST(AdhdCompiler, SupportsNamedEnumCaseReflectionDeferredTemplateSpecAndMacroDefines){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "enum<u8> Mode {\r\n"
        "    enum Idle,\r\n"
        "    enum Active,\r\n"
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
        "[reflect]\r\n"
        "class Payload {\r\n"
        "    public i32 count = DEFAULT_COUNT;\r\n"
        "    public Mode mode = Mode.Idle;\r\n"
        "    public Optional<Mode> optMode;\r\n"
        "}\r\n"
        "\r\n"
        "[asset(\"bundle\")]\r\n"
        "class BundleAsset {\r\n"
        "    public ParallelHashSet<AssetRef<Payload>> texSet;\r\n"
        "    public List<i32> steps;\r\n"
        "    public Map<String, Payload> payloads;\r\n"
        "}\r\n";

    NWB::Core::Metascript::MVector<MString> defines(arena.arena);
    defines.push_back(MString("DEFAULT_COUNT=42", 16u, arena.arena));
    defines.push_back(MString("FEATURE_ENABLED", 15u, arena.arena));

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/named_enum_spec.adhd", arena.arena, res, nullptr, {}, &defines));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());

    EXPECT_NE(hdrView.find("inline constexpr ::NamedEnumCase<Enum> s_NamedEnumCases[] = {"), MStringView::npos);
    EXPECT_NE(hdrView.find("{ \"Idle\", Idle },"), MStringView::npos);
    EXPECT_NE(hdrView.find("{ \"Active\", Active },"), MStringView::npos);
    EXPECT_NE(hdrView.find("constexpr Span<const ::NamedEnumCase<Enum>> namedEnumCases()noexcept{ return s_NamedEnumCases; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("inline Expected<Enum> parseNamedEnum(const AStringView text)noexcept{ return ::ParseNamedEnumText<Enum>(text, namedEnumCases()); }"), MStringView::npos);
    EXPECT_NE(hdrView.find("constexpr AStringView toNamedEnumText([[maybe_unused]] const Enum value)noexcept"), MStringView::npos);

    EXPECT_NE(hdrView.find("__f->isString()){ if(const auto __e = Mode::parseNamedEnum(__f->asString())) this->mode = *__e; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("__f->isString()){ if(const auto __e = Mode::parseNamedEnum(__f->asString())) this->optMode = *__e; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("i32 count = 42;"), MStringView::npos);

    EXPECT_NE(hdrView.find("static constexpr usize assetDependencyFieldCount()noexcept{ return 1u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->texSet.insert({::Name(__f->asList()[__i].asString().data())});"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->steps.push_back(static_cast<i32>(__f->asList()[__i].asInteger()));"), MStringView::npos);
    EXPECT_NE(hdrView.find("for(const auto& [__k, __v] : __f->asMap()){ if(!__v.isMap() || !this->payloads[AString(__k, s_ClassArena)].deserializeNwb(__v)) return false; }"), MStringView::npos);

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
    EXPECT_FALSE(Compiler::compile(s_TemplateRecSource, "impl/sim/template_rec.adhd", arena.arena, recRes));

    static constexpr MStringView s_TemplateSerializeBadSource =
        "[serialize]\r\n"
        "class Packet<T> {\r\n"
        "    public T data;\r\n"
        "}\r\n"
        "void usePacket(in Packet<Vector<u32>> p) {}\r\n";
    CompileResult serRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_TemplateSerializeBadSource, "impl/sim/template_ser_bad.adhd", arena.arena, serRes));
}

TEST(AdhdCompiler, SupportsAliasedOutOfLineMethodsForwardListMapSimdAndCtorIfElse){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "type EntityId = u32;\r\n"
        "\r\n"
        "interface ILookup {\r\n"
        "    EntityId resolve(in u32 raw) const;\r\n"
        "}\r\n"
        "\r\n"
        "class LookupTable : ILookup {\r\n"
        "    private in ILookup fallback;\r\n"
        "    public LookupTable(in bool usePrimary, in ILookup primary, in ILookup secondary) {\r\n"
        "        if (usePrimary) {\r\n"
        "            this.fallback = primary;\r\n"
        "        } else {\r\n"
        "            this.fallback = secondary;\r\n"
        "        }\r\n"
        "    }\r\n"
        "    public u32 resolve(in EntityId raw) const override;\r\n"
        "}\r\n"
        "\r\n"
        "EntityId LookupTable::resolve(in u32 raw) const {\r\n"
        "    return raw + 1u;\r\n"
        "}\r\n"
        "\r\n"
        "[reflect]\r\n"
        "class GraphNode {\r\n"
        "    public ForwardList<i32> chain;\r\n"
        "    public Map<Name, f32x4> tintBySlot;\r\n"
        "}\r\n"
        "\r\n"
        "u32 consumeIds(in Vector<u32> ids) {\r\n"
        "    return cast<u32>(ids.size());\r\n"
        "}\r\n"
        "\r\n"
        "u32 testVarDeclMove() {\r\n"
        "    Vector<u32> local;\r\n"
        "    local.push_back(7u);\r\n"
        "    u32 count = consumeIds(local);\r\n"
        "    return count;\r\n"
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
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/alias_ool.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView srcView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(srcView.find(": fallback(::MakeNotNull(&(primary)))"), MStringView::npos);
    EXPECT_NE(srcView.find("u32 count = consumeIds(::Move(local));"), MStringView::npos);
    EXPECT_NE(srcView.find("return consumeIds(captured);"), MStringView::npos);
    EXPECT_EQ(srcView.find("return consumeIds(::Move(captured));"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->chain.clear(); for(usize __i = __f->asList().size(); __i > 0u; --__i) this->chain.push_front(static_cast<i32>(__f->asList()[__i - 1u].asInteger()));"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->tintBySlot[::Name(__k.data())] = Float4(static_cast<f32>(__v.asList()[0].toDouble())"), MStringView::npos);

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
    EXPECT_FALSE(Compiler::compile(s_BadOolTemplateParamsSource, "impl/sim/bad_ool_tparams.adhd", arena.arena, badOolRes));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

