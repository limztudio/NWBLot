// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

TEST(AdhdCompiler, SupportsIntegerAndMatrixSimdLoweringAndStorageSyncForNonSimdUses){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "void tweakScalar(mut f32 lane) {\r\n"
        "    lane += 1.0f;\r\n"
        "}\r\n"
        "\r\n"
        "i32x4 computeIntSimd(in i32x4 a, in i32x4 b) {\r\n"
        "    i32x4 acc = -a;\r\n"
        "    acc = acc - b;\r\n"
        "    acc = (acc * b) / b;\r\n"
        "    acc %= b;\r\n"
        "    acc <<= 1;\r\n"
        "    acc >>= 1;\r\n"
        "    return clamp(abs(acc), a, b);\r\n"
        "}\r\n"
        "\r\n"
        "f32x4x4 computeMatSimd(in f32x4x4 m0, in f32x4x4 m1, in f32 scale) {\r\n"
        "    f32x4x4 out = -m0 + m1;\r\n"
        "    out -= m1;\r\n"
        "    out = (out * m1) * scale;\r\n"
        "    out /= scale;\r\n"
        "    return out;\r\n"
        "}\r\n"
        "\r\n"
        "bool syncAndMutateLane(mut f32x4 v, in f32x4 other) {\r\n"
        "    v += other;\r\n"
        "    tweakScalar(v.x);\r\n"
        "    native(mut lx = v.x) {\r\n"
        "        lx += 2.0f;\r\n"
        "    }\r\n"
        "    u32x4 bits = bit_cast<u32x4>(v);\r\n"
        "    return (v == other) && (bits.x != 0u);\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/int_mat_simd.adhd", arena.arena, res));

    const MStringView cppView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(cppView.find("::VectorAddInt(::VectorXorInt(__adhd_simd_a, ::VectorTrueInt()), ::VectorReplicateInt(1u))"), MStringView::npos);
    EXPECT_NE(cppView.find("::VectorSetInt(static_cast<u32>(static_cast<i32>(::VectorGetIntX(__lhs)) * static_cast<i32>(::VectorGetIntX(__rhs)))"), MStringView::npos);
    EXPECT_NE(cppView.find("::MatrixMultiply(__adhd_simd_out, __adhd_simd_m1)"), MStringView::npos);
    EXPECT_NE(cppView.find("::StoreFloat(__adhd_simd_v, v);\r\n    tweakScalar(v.x);\r\n    __adhd_simd_v = ::LoadFloat(v);"), MStringView::npos);
    EXPECT_NE(cppView.find("auto& lx = (v.x);"), MStringView::npos);
    EXPECT_NE(cppView.find("__adhd_simd_v = ::LoadFloat(v);\r\n    }"), MStringView::npos);
    EXPECT_NE(cppView.find("::LoadInt(::BitCast<UInt4>(v));"), MStringView::npos);

    static constexpr MStringView s_MixedSwizzleSource =
        "void badSwizzle(in f32x3_i32 v) {\r\n"
        "    f32x3_i32 s = v.xyzw;\r\n"
        "}\r\n";
    CompileResult badSwizzleRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_MixedSwizzleSource, "impl/sim/bad_mixed_swizzle.adhd", arena.arena, badSwizzleRes));

    static constexpr MStringView s_WrongBuiltinAritySource =
        "f32 badDot(in f32x4 a) {\r\n"
        "    return dot(a);\r\n"
        "}\r\n";
    CompileResult badBuiltinArityRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_WrongBuiltinAritySource, "impl/sim/bad_builtin_arity.adhd", arena.arena, badBuiltinArityRes));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

