// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        "    private i32 calc()const { return 7; }\r\n"
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
        "    i32 id()const;\r\n"
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
        "    enum kCount,\r\n"
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
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/named_enum_spec.adhd", arena.arena, res, nullptr, {}, defines));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());

    EXPECT_NE(hdrView.find("constexpr usize reflectedEnumeratorCount()noexcept{ return 2u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("inline constexpr ::NamedEnumCase<Enum> s_NamedEnumCases[] = {"), MStringView::npos);
    EXPECT_NE(hdrView.find("{ \"Idle\", Idle },"), MStringView::npos);
    EXPECT_NE(hdrView.find("{ \"Active\", Active },"), MStringView::npos);
    EXPECT_EQ(hdrView.find("{ \"kCount\", kCount },"), MStringView::npos);
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

    static constexpr MStringView s_BadEnumCountSource =
        "enum<u8> BadMode {\r\n"
        "    enum Idle,\r\n"
        "    enum Count,\r\n"
        "}\r\n";
    CompileResult badEnumCountRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_BadEnumCountSource, "impl/sim/bad_enum_count.adhd", arena.arena, badEnumCountRes));
}

TEST(AdhdCompiler, SupportsAliasedOutOfLineMethodsForwardListMapSimdAndCtorIfElse){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "type EntityId = u32;\r\n"
        "\r\n"
        "interface ILookup {\r\n"
        "    EntityId resolve(in u32 raw)const;\r\n"
        "}\r\n"
        "\r\n"
        "class LookupTable : ILookup {\r\n"
        "    private in ILookup fallback;\r\n"
        "    public LookupTable(in b8 usePrimary, in ILookup primary, in ILookup secondary) {\r\n"
        "        if (usePrimary) {\r\n"
        "            this.fallback = primary;\r\n"
        "        } else {\r\n"
        "            this.fallback = secondary;\r\n"
        "        }\r\n"
        "    }\r\n"
        "    public u32 resolve(in EntityId raw)const override;\r\n"
        "}\r\n"
        "\r\n"
        "EntityId LookupTable::resolve(in u32 raw)const {\r\n"
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

    EXPECT_NE(hdrView.find(": fallback(::MakeNotNull(&(primary)))"), MStringView::npos);
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

TEST(AdhdCompiler, SupportsAssetDependencyTraversalInRebindDeducedBoundsAndOolInstantiations){
    AdhdTestArena arena;

    static constexpr MStringView s_Source =
        "interface ISource {\r\n"
        "    i32 read()const;\r\n"
        "    void write(in i32 v);\r\n"
        "}\r\n"
        "\r\n"
        "class SourceImpl : ISource {\r\n"
        "    public i32 value = 0;\r\n"
        "    public i32 read()const override { return this.value; }\r\n"
        "    public void write(in i32 v) override { this.value = v; }\r\n"
        "}\r\n"
        "\r\n"
        "class ReaderHolder {\r\n"
        "    public in ISource roRef;\r\n"
        "    public ReaderHolder(in ISource initRef) {\r\n"
        "        this.roRef = initRef;\r\n"
        "    }\r\n"
        "    public void rebindTo(in ISource nextRef) {\r\n"
        "        this.roRef = nextRef;\r\n"
        "    }\r\n"
        "}\r\n"
        "\r\n"
        "class OuterHolder {\r\n"
        "    public OolPair<LeafInner> wrapped = OolPair<LeafInner>();\r\n"
        "}\r\n"
        "\r\n"
        "class OolPair<T> {\r\n"
        "    public T item = T();\r\n"
        "    public LeafInner helper = LeafInner();\r\n"
        "    public u8 tag = 1;\r\n"
        "}\r\n"
        "\r\n"
        "class LeafInner {\r\n"
        "    public i32 v = 7;\r\n"
        "}\r\n"
        "\r\n"
        "const LeafInner kDefaultLeaf();\r\n"
        "\r\n"
        "class OolRunner {\r\n"
        "    public i32 execute()const;\r\n"
        "}\r\n"
        "\r\n"
        "i32 OolRunner::execute()const {\r\n"
        "    LeafInner localLeaf();\r\n"
        "    OolPair<u64> pair = OolPair<u64>();\r\n"
        "    return cast<i32>(pair.tag) + localLeaf.v + kDefaultLeaf.v;\r\n"
        "}\r\n"
        "\r\n"
        "i32 readFrom<T : ISource>(in T src) {\r\n"
        "    return src.read();\r\n"
        "}\r\n"
        "\r\n"
        "i32 testRebindAndDeducedCall(in SourceImpl a, in SourceImpl b) {\r\n"
        "    in ISource ro = a;\r\n"
        "    ro = b;\r\n"
        "    return readFrom(b) + ro.read();\r\n"
        "}\r\n"
        "\r\n"
        "[reflect]\r\n"
        "class NestedAssetGroup {\r\n"
        "    public AssetRef<SourceImpl> subRef;\r\n"
        "}\r\n"
        "\r\n"
        "[asset(\"prefab\")]\r\n"
        "class PrefabAsset {\r\n"
        "    public AssetRef<SourceImpl> mainRef;\r\n"
        "    public Optional<AssetRef<SourceImpl>> optRef;\r\n"
        "    public Vector<AssetRef<SourceImpl>> refList;\r\n"
        "    public NestedAssetGroup nested = NestedAssetGroup();\r\n"
        "}\r\n";

    CompileResult res(arena.arena);
    ASSERT_TRUE(Compiler::compile(s_Source, "impl/sim/asset_deps_rebind.adhd", arena.arena, res));

    const MStringView hdrView(res.generatedHeader.data(), res.generatedHeader.size());
    const MStringView srcView(res.generatedSource.data(), res.generatedSource.size());

    EXPECT_NE(hdrView.find("static constexpr usize assetDependencyFieldCount()noexcept{ return 4u; }"), MStringView::npos);
    EXPECT_NE(hdrView.find("void forEachAssetDependency([[maybe_unused]] DependencyCallback&& callback)const{"), MStringView::npos);
    EXPECT_NE(hdrView.find("if(this->mainRef.valid()) callback(this->mainRef.virtualPath);"), MStringView::npos);
    EXPECT_NE(hdrView.find("if(this->optRef.has_value() && this->optRef->valid()) callback(this->optRef->virtualPath);"), MStringView::npos);
    EXPECT_NE(hdrView.find("for(const auto& __dep : this->refList){ if(__dep.valid()) callback(__dep.virtualPath); }"), MStringView::npos);
    EXPECT_NE(hdrView.find("this->nested.forEachAssetDependency(callback);"), MStringView::npos);
    EXPECT_NE(hdrView.find("[[nodiscard]] static b8 extractNwbAssetDependencies(const NwbValue& inMap, [[maybe_unused]] DependencyCallback&& callback)noexcept{"), MStringView::npos);
    EXPECT_NE(hdrView.find("__f->isMap() && !NestedAssetGroup::extractNwbAssetDependencies(*__f, callback)) return false;"), MStringView::npos);
    EXPECT_NE(hdrView.find("template<>\r\nclass OolPair<u64> final{"), MStringView::npos);
    const usize leafInnerDefPos = hdrView.find("class LeafInner final{");
    const usize outerHolderDefPos = hdrView.find("class OuterHolder final{");
    const usize defaultLeafConstPos = hdrView.find("inline constexpr LeafInner kDefaultLeaf{};");
    ASSERT_NE(leafInnerDefPos, MStringView::npos);
    ASSERT_NE(outerHolderDefPos, MStringView::npos);
    ASSERT_NE(defaultLeafConstPos, MStringView::npos);
    EXPECT_LT(leafInnerDefPos, outerHolderDefPos);
    EXPECT_LT(leafInnerDefPos, defaultLeafConstPos);
    EXPECT_NE(srcView.find("LeafInner localLeaf{};"), MStringView::npos);
    EXPECT_NE(srcView.find("(ro = ::MakeNotNull(&(b)));"), MStringView::npos);
    EXPECT_NE(hdrView.find("(this->roRef = ::MakeNotNull(&(nextRef)));"), MStringView::npos);

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
    EXPECT_FALSE(Compiler::compile(s_DeducedBoundViolationSource, "impl/sim/bad_deduced.adhd", arena.arena, badDeducedRes));

    static constexpr MStringView s_WrongArgCountSource =
        "i32 addPair(in i32 a, in i32 b) {\r\n"
        "    return a + b;\r\n"
        "}\r\n"
        "void badArity() {\r\n"
        "    addPair(1);\r\n"
        "}\r\n";
    CompileResult badArityRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_WrongArgCountSource, "impl/sim/bad_arity.adhd", arena.arena, badArityRes));

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
    EXPECT_FALSE(Compiler::compile(s_MutateInRefInCtorSource, "impl/sim/bad_ctor_in_mut.adhd", arena.arena, badCtorMutRes));

    static constexpr MStringView s_ProhibitedBoolSource =
        "class BadFlagHolder {\r\n"
        "    public bo" "ol enabled = false;\r\n"
        "}\r\n";
    CompileResult prohibitedBoolRes(arena.arena);
    EXPECT_FALSE(Compiler::compile(s_ProhibitedBoolSource, "impl/sim/prohibited_b8.adhd", arena.arena, prohibitedBoolRes));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

