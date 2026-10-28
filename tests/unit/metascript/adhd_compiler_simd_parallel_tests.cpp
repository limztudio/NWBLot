// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    EXPECT_NE(cppView.find("const f32 snapped = ::Round(blended);"), MStringView::npos);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

