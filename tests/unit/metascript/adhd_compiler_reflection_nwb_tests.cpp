// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_reflection_nwb_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::AdhdCompilerTestsDetail;

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
        "    public i32 readVal(in usize idx)const;\r\n"
        "    private i32 readVal(in usize idx);\r\n"
        "}\r\n"
        "[reflect]\r\n"
        "i32 DualAccessor::readVal(in usize idx) {\r\n"
        "    this.secret += cast<i32>(idx);\r\n"
        "    return this.secret;\r\n"
        "}\r\n"
        "i32 DualAccessor::readVal(in usize idx)const {\r\n"
        "    f32 scale = 1.0f;\r\n"
        "    u32 mask = 0x10u + 10u;\r\n"
        "    return this.secret + cast<i32>(idx) + cast<i32>(scale) + cast<i32>(mask);\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "/abs/work/dual_accessor.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("namespace NWB{"), MStringView::npos);
    EXPECT_NE(hdrView.find("reflectedMethodCount()noexcept{ return 2u; }"), MStringView::npos);
    EXPECT_NE(cppView.find("f32 scale = 1.0f;"), MStringView::npos);
    EXPECT_NE(cppView.find("u32 mask = (0x10u + 10u);"), MStringView::npos);

    const MString relNs = Compiler::DeriveNamespaceFromPath("impl/2d_scene/node.adhd", arena.arena);
    EXPECT_EQ(MStringView(relNs.data(), relNs.size()), "NWB::Impl::_2dScene");

    static constexpr MStringView s_DuplicateAttrSource =
        "[reflect, reflect]\r\n"
        "void dupAttr() {}\r\n";
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
        "void badNative(in i32 a, mut i32 b) {\r\n"
        "    native(in a, mut a = b) {\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult dupNativeRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DuplicateNativeBindingSource, "impl/sim/dup_native.adhd", arena.arena, dupNativeRes));
}

TEST(AdhdCompiler, SupportsReturnFuncCapturePropagationEnumAliasScopeOverloadedInterfaceAndConstMutationChecks){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "flag<u8> StageMask {\r\n"
        "    Idle = 0,\r\n"
        "    Active = 1 << 0,\r\n"
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
        "    return StageMaskAlias::Active;\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/dual_processor.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());

    EXPECT_NE(hdrView.find("::InplaceFunction<64u, i32(i32)> makeAdder(const i32 base)noexcept;"), MStringView::npos);
    EXPECT_NE(hdrView.find("return NWB_TEXT(\"content/default.nwb\");"), MStringView::npos);
    EXPECT_NE(hdrView.find("return StageMask::Active;"), MStringView::npos);

    static constexpr MStringView s_MutateGlobalConstSource =
        "const u32 kImmutable = 10u;\r\n"
        "void badMutateGlobal() {\r\n"
        "    kImmutable = 20u;\r\n"
        "}\r\n";
    CompileResult mutGlobalRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutateGlobalConstSource, "impl/sim/bad_mut_global.adhd", arena.arena, mutGlobalRes));

    static constexpr MStringView s_MissingReturnSource =
        "i32 missingReturn(in i32 x) {\r\n"
        "    i32 y = x + 1;\r\n"
        "}\r\n";
    CompileResult missingRetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MissingReturnSource, "impl/sim/bad_missing_ret.adhd", arena.arena, missingRetRes));

    static constexpr MStringView s_MutateParallelForIndexSource =
        "void badParIndexMut() {\r\n"
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
        "u32 badScope() {\r\n"
        "    return Dummy::x;\r\n"
        "}\r\n";
    CompileResult badScopeRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_InvalidNonEnumScopeAccessSource, "impl/sim/bad_non_enum_scope.adhd", arena.arena, badScopeRes));
}

TEST(AdhdCompiler, SupportsReferenceReturnTypesConstantTemplateArgsAndOptionalSimdReflection){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "const usize kBatchCap(0x08u);\r\n"
        "interface IWorker {\r\n"
        "    u32 run();\r\n"
        "}\r\n"
        "interface IWorkerProvider {\r\n"
        "    in IWorker currentWorker()const;\r\n"
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
        "    public in IWorker currentWorker()const override {\r\n"
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
        "    b8 cmp = a < b >> 1u;\r\n"
        "    Func<mut IWorker(mut WorkerHost)> getter = lambda(mut WorkerHost h) -> mut IWorker {\r\n"
        "        return h.mutableWorker();\r\n"
        "    };\r\n"
        "    if (cmp) {\r\n"
        "        return getter(host).run();\r\n"
        "    }\r\n"
        "    return host.mutableWorker().run();\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/ref_return_and_const_args.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("virtual ::NotNull<const IWorker*> currentWorker()const noexcept = 0;"), MStringView::npos);
    EXPECT_NE(hdrView.find("virtual ::NotNull<IWorker*> mutableWorker()noexcept = 0;"), MStringView::npos);
    EXPECT_NE(hdrView.find("inline ::NotNull<const IWorker*> selectWorker(const WorkerHost& host)noexcept{"), MStringView::npos);
    EXPECT_NE(hdrView.find("FixedVector<T, Cap> items;"), MStringView::npos);
    EXPECT_NE(hdrView.find("return ::MakeNotNull(&(this->worker));"), MStringView::npos);
    EXPECT_NE(hdrView.find("return ::MakeNotNull(&((*host.currentWorker())));"), MStringView::npos);
    EXPECT_NE(cppView.find("return getter(host)->run();"), MStringView::npos);
    EXPECT_NE(cppView.find("return host.mutableWorker()->run();"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->boundsTint = Float4(static_cast<f32>(__f->asList()[0].toDouble())"), MStringView::npos);

    static constexpr MStringView s_ReturnInParamAsMutRefSource =
        "mut i32 badMutReturn(in i32 x) {\r\n"
        "    return x;\r\n"
        "}\r\n";
    CompileResult badMutRetRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ReturnInParamAsMutRefSource, "impl/sim/bad_mut_ret.adhd", arena.arena, badMutRetRes));

    static constexpr MStringView s_MutateInReturnValueSource =
        "in i32 getConstRef(in i32 x) {\r\n"
        "    return x;\r\n"
        "}\r\n"
        "void badMutateConstRef(in i32 a) {\r\n"
        "    getConstRef(a) = 5;\r\n"
        "}\r\n";
    CompileResult badMutConstCallRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_MutateInReturnValueSource, "impl/sim/bad_mut_const_call.adhd", arena.arena, badMutConstCallRes));

    static constexpr MStringView s_InvalidVectorAritySource =
        "void badVecArity() {\r\n"
        "    Vector<i32, i32> v;\r\n"
        "}\r\n";
    CompileResult badVecArityRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_InvalidVectorAritySource, "impl/sim/bad_vec_arity.adhd", arena.arena, badVecArityRes));
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
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/parallel_templates_and_nwb.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("inline static ::NWB::Core::Alloc::GlobalArena s_ClassArena{::Name(\"AssetManifest\")};"), MStringView::npos);
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
    EXPECT_FALSE(Compiler::Compile(s_VoidVariableSource, "impl/sim/bad_void_var.adhd", arena.arena, badVoidVarRes));

    static constexpr MStringView s_ByValueInterfaceTemplateArgSource =
        "interface IJob {\r\n"
        "    void tick();\r\n"
        "}\r\n"
        "void badIfaceVec() {\r\n"
        "    Vector<IJob> jobs;\r\n"
        "}\r\n";
    CompileResult badIfaceVecRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_ByValueInterfaceTemplateArgSource, "impl/sim/bad_iface_vec.adhd", arena.arena, badIfaceVecRes));

    static constexpr MStringView s_WaitInsideParallelSource =
        "void badParallelWait() {\r\n"
        "    parallel TaskA {\r\n"
        "        wait();\r\n"
        "    }\r\n"
        "}\r\n";
    CompileResult badParWaitRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_WaitInsideParallelSource, "impl/sim/bad_par_wait.adhd", arena.arena, badParWaitRes));

    static constexpr MStringView s_DuplicateWaitTargetSource =
        "void badDupWait() {\r\n"
        "    parallel TaskA {\r\n"
        "    }\r\n"
        "    wait(TaskA, TaskA);\r\n"
        "}\r\n";
    CompileResult badDupWaitRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DuplicateWaitTargetSource, "impl/sim/bad_dup_wait.adhd", arena.arena, badDupWaitRes));
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
        "u32 dispatchByTag(in b8 fastPath) {\r\n"
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
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/templated_alias_switch_simd_sync.adhd", arena.arena, res));

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
    EXPECT_FALSE(Compiler::Compile(s_CyclicTypeAliasSource, "impl/sim/cyclic_alias.adhd", arena.arena, cyclicAliasRes));

    static constexpr MStringView s_RecursiveClassValueMemberSource =
        "class BadNode {\r\n"
        "    public Optional<BadNode> next;\r\n"
        "}\r\n";
    CompileResult recClassRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_RecursiveClassValueMemberSource, "impl/sim/rec_class.adhd", arena.arena, recClassRes));
}

TEST(AdhdCompiler, SupportsParameterizedInterfaceBoundsInheritedTemplateInterfacesRefAliasesLambdaArenasAndSimdNwbReflection){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "[asset(\"Mesh\")]\r\n"
        "class MeshAsset {\r\n"
        "    public u32 vertexCount = 0u;\r\n"
        "}\r\n"
        "interface IBaseStream<T> {\r\n"
        "    T sample()const;\r\n"
        "}\r\n"
        "interface IDerivedStream<U> : IBaseStream<U> {\r\n"
        "    void push(in U item);\r\n"
        "}\r\n"
        "type VecStreamIn = in IDerivedStream<f32x4>;\r\n"
        "class VecStreamImpl : IDerivedStream<f32x4> {\r\n"
        "    public f32x4 current = f32x4(1.0f);\r\n"
        "    public f32x4 sample()const override {\r\n"
        "        return this.current;\r\n"
        "    }\r\n"
        "    public void push(in f32x4 item) override {\r\n"
        "        this.current = item;\r\n"
        "    }\r\n"
        "}\r\n"
        "class Holder<T> {\r\n"
        "    public T value;\r\n"
        "    public T get()const {\r\n"
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
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/param_bounds_ref_alias_nwb.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());

    EXPECT_NE(hdrView.find("static constexpr usize reflectedMethodCount()noexcept{ return 2u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("using VecStreamIn = ::NotNull<const IDerivedStream<Float4>*>;"), MStringView::npos);
    EXPECT_NE(hdrView.find("static constexpr usize assetDependencyFieldCount()noexcept{ return 1u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->points.push_back(Float4("), MStringView::npos);
    EXPECT_NE(hdrView.find("Float44 __m{}; for(usize __i = 0u; __i < 16u; ++__i) __m.raw[__i] = static_cast<f32>(__f->asList()[__i].toDouble()); this->transform = __m;"), MStringView::npos);
    EXPECT_NE(hdrView.find("::SIMDVectorSingle __adhd_simd_a = ::LoadFloat(src.sample());"), MStringView::npos);
    EXPECT_NE(hdrView.find("::SIMDVectorSingle __adhd_simd_b = ::LoadFloat(h.get());"), MStringView::npos);
    EXPECT_NE(hdrView.find("::SIMDVectorSingle __adhd_simd_c = ::LoadFloat(opt.value());"), MStringView::npos);
    EXPECT_NE(hdrView.find("::NWB::Core::Alloc::ScratchArena __adhd_scratch(::Name(\"lambda\"));"), MStringView::npos);
    EXPECT_NE(hdrView.find("StreamConsumer(const VecStreamIn& s)noexcept : stream(::MakeNotNull(&((*s))))"), MStringView::npos);

    static constexpr MStringView s_WrongParameterizedBoundSource =
        "interface IStream<T> {\r\n"
        "    T read()const;\r\n"
        "}\r\n"
        "class IntStream : IStream<i32> {\r\n"
        "    public i32 read()const override { return 0; }\r\n"
        "}\r\n"
        "f32 consumeFloat<S : IStream<f32>>(in S s) {\r\n"
        "    return s.read();\r\n"
        "}\r\n"
        "f32 badCall(in IntStream is) {\r\n"
        "    return consumeFloat<IntStream>(is);\r\n"
        "}\r\n";
    CompileResult wrongBoundRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_WrongParameterizedBoundSource, "impl/sim/wrong_param_bound.adhd", arena.arena, wrongBoundRes));

    static constexpr MStringView s_DoubleRefAliasSource =
        "interface IFoo {\r\n"
        "    void tick();\r\n"
        "}\r\n"
        "type FooRef = in IFoo;\r\n"
        "void badDoubleRef(in FooRef f) {\r\n"
        "    mut FooRef local = f;\r\n"
        "}\r\n";
    CompileResult doubleRefRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_DoubleRefAliasSource, "impl/sim/double_ref_alias.adhd", arena.arena, doubleRefRes));

    static constexpr MStringView s_EscapingMutLambdaSource =
        "Func<i32()> badReturnMutLambda() {\r\n"
        "    i32 counter = 0;\r\n"
        "    return lambda[mut counter]() -> i32 {\r\n"
        "        counter += 1;\r\n"
        "        return counter;\r\n"
        "    };\r\n"
        "}\r\n";
    CompileResult escapingLambdaRes(arena.arena);
    EXPECT_FALSE(Compiler::Compile(s_EscapingMutLambdaSource, "impl/sim/escaping_mut_lambda.adhd", arena.arena, escapingLambdaRes));
}

TEST(AdhdCompiler, SupportsSelfDefaultedNameBinarySerializeNameHashAndContainerEnumNwb){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "enum<u8> BoundaryMode {\r\n"
        "    OpenSurface = 0,\r\n"
        "    ClosedNested = 1,\r\n"
        "}\r\n"
        "[serialize]\r\n"
        "class SubRecord {\r\n"
        "    public Name tag;\r\n"
        "    public u32 version = 1u;\r\n"
        "    [transient]\r\n"
        "    private u64 runtimeCache = 0u;\r\n"
        "}\r\n"
        "[serialize]\r\n"
        "class OuterPacket {\r\n"
        "    public SubRecord sub = SubRecord();\r\n"
        "    public AssetRef<SubRecord> asset;\r\n"
        "    [reflect]\r\n"
        "    private f32 m_nearPlane = 0.1f;\r\n"
        "}\r\n"
        "[reflect]\r\n"
        "class EnumContainerAsset {\r\n"
        "    public Vector<BoundaryMode> modes;\r\n"
        "    public Set<BoundaryMode> modeSet;\r\n"
        "    public Map<Name, BoundaryMode> modeBySlot;\r\n"
        "    public f32x3x4 localTransform(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);\r\n"
        "}\r\n"
        "Name makeDefaultName() {\r\n"
        "    Name localName;\r\n"
        "    return localName;\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/serialize_and_container_enum.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("{ \"OpenSurface\", OpenSurface },"), MStringView::npos);
    EXPECT_NE(hdrView.find("{ \"ClosedNested\", ClosedNested },"), MStringView::npos);
    EXPECT_EQ(hdrView.find("open_surface"), MStringView::npos);
    EXPECT_EQ(hdrView.find("closed_nested"), MStringView::npos);

    EXPECT_NE(hdrView.find("static constexpr usize serializedBinarySize()noexcept{ return 0u + sizeof(::NameHash) + sizeof(version); }"), MStringView::npos);
    EXPECT_NE(hdrView.find("static constexpr usize serializedBinarySize()noexcept{ return 0u + decltype(sub)::serializedBinarySize() + sizeof(::NameHash) + sizeof(m_nearPlane); }"), MStringView::npos);
    EXPECT_NE(hdrView.find("if(const auto* __f = inMap.findField(\"m_nearPlane\"); __f && "), MStringView::npos);
    EXPECT_NE(hdrView.find("if(const auto __e = BoundaryMode::parseNamedEnum(__f->asList()[__i].asString())) this->modes.push_back(*__e);"), MStringView::npos);
    EXPECT_NE(hdrView.find("if(const auto __e = BoundaryMode::parseNamedEnum(__f->asList()[__i].asString())) this->modeSet.insert(*__e);"), MStringView::npos);
    EXPECT_NE(hdrView.find("if(const auto __e = BoundaryMode::parseNamedEnum(__v.asString())) this->modeBySlot[::Name(__k.data())] = *__e;"), MStringView::npos);
    EXPECT_NE(hdrView.find("Float34 localTransform{ .raw = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f } };"), MStringView::npos);

    EXPECT_NE(cppView.find("&this->tag.identityHash(), sizeof(::NameHash)"), MStringView::npos);
    EXPECT_NE(cppView.find("this->tag = ::Name(__hash);"), MStringView::npos);
    EXPECT_NE(cppView.find("&this->asset.virtualPath.identityHash(), sizeof(::NameHash)"), MStringView::npos);
    EXPECT_NE(cppView.find("this->asset.virtualPath = ::Name(__hash);"), MStringView::npos);
    EXPECT_NE(cppView.find("this->sub.serializeBinary(outBuffer + offset, bufferCapacity - offset)"), MStringView::npos);
    EXPECT_NE(cppView.find("this->sub.deserializeBinary(inBuffer + offset, bufferSize - offset)"), MStringView::npos);
    EXPECT_NE(cppView.find("Name localName{};"), MStringView::npos);
}

TEST(AdhdCompiler, SupportsBuiltInEntityIdUnalignedVectorsSizedFuncAndExternalForwardDecls){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "type ExternalCallback = Func<64u, void(mut ExternalContext)>;\r\n"
        "[reflect]\r\n"
        "class NativeFreeComponent {\r\n"
        "    public AssetRef<ExternalAsset> asset;\r\n"
        "    public EntityID owner = s_InvalidEntityId;\r\n"
        "    public u32x4u packedValue;\r\n"
        "    public f32x2u uv(0.25f, 0.75f);\r\n"
        "    [transient]\r\n"
        "    public ExternalCallback callback;\r\n"
        "    public f32 sumUv()const {\r\n"
        "        return this.uv.x + this.uv.y;\r\n"
        "    }\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/native_free.adhd", arena.arena, res, nullptr, "NWB::Impl"));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    EXPECT_NE(hdrView.find("#include <core/ecs/entity_id.h>"), MStringView::npos);
    EXPECT_NE(hdrView.find("#include <impl/global.h>"), MStringView::npos);
    EXPECT_NE(hdrView.find("class ExternalContext;\r\n"), MStringView::npos);
    EXPECT_NE(hdrView.find("class ExternalAsset;\r\n"), MStringView::npos);
    EXPECT_NE(hdrView.find("using ExternalCallback = ::InplaceFunction<64u, void(ExternalContext&)>;"), MStringView::npos);
    EXPECT_NE(hdrView.find("::NWB::Core::ECS::EntityID owner = ::NWB::Core::ECS::s_InvalidEntityId;"), MStringView::npos);
    EXPECT_NE(hdrView.find("UInt4U packedValue{};"), MStringView::npos);
    EXPECT_NE(hdrView.find("Float2U uv{0.25f, 0.75f};"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->owner = ::NWB::Core::ECS::EntityID(static_cast<u32>(__f->asInteger()));"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->packedValue = UInt4U(static_cast<u32>(__f->asList()[0].asInteger())"), MStringView::npos);

    static constexpr MStringView s_SubsystemSource =
        "[asset(\"custom_tagged_asset\")]\r\n"
        "class CustomTaggedAsset {\r\n"
        "    public u32 version = 1u;\r\n"
        "}\r\n"
        "[subsystem(\"custom_project_subsystem\")]\r\n"
        "class CustomProjectSubsystem {\r\n"
        "    public AssetRef<CustomProjectAsset> primaryAsset;\r\n"
        "    public Vector<AssetRef<CustomTaggedAsset>> taggedAssets;\r\n"
        "}\r\n";

    CompileResult subRes(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_SubsystemSource, "project/custom/subsystem.adhd", arena.arena, subRes, nullptr, "Project::Custom"));

    const MStringView subHdrView(subRes.generatedHeader.data(), subRes.generatedHeader.size());
    EXPECT_NE(subHdrView.find("#include <impl/assets_subsystem/asset.h>\r\n"), MStringView::npos);
    EXPECT_NE(subHdrView.find("class CustomProjectAsset;\r\n"), MStringView::npos);
    EXPECT_NE(subHdrView.find("::NWB::Impl::MakeSubsystemFieldDescriptor<CustomProjectSubsystem, CustomProjectAsset, &CustomProjectSubsystem::primaryAsset>(\"primary_asset\", ::Name(\"custom_project_asset\")),"), MStringView::npos);
    EXPECT_NE(subHdrView.find("::NWB::Impl::MakeSubsystemFieldDescriptor<CustomProjectSubsystem, CustomTaggedAsset, &CustomProjectSubsystem::taggedAssets>(\"tagged_assets\", ::Name(\"custom_tagged_asset\")),"), MStringView::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

