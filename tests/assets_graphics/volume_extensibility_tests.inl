// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ProjectProbeAsset final : public NWB::Core::Assets::TypedAsset<ProjectProbeAsset>{
public:
    NWB_DEFINE_ASSET_TYPE("project_probe")

public:
    explicit ProjectProbeAsset(NWB::Core::Assets::AssetArena&){
    }
    explicit ProjectProbeAsset(const Name virtualPath)
        : NWB::Core::Assets::TypedAsset<ProjectProbeAsset>(virtualPath)
    {
    }
    ProjectProbeAsset(NWB::Core::Assets::AssetArena&, const Name virtualPath)
        : NWB::Core::Assets::TypedAsset<ProjectProbeAsset>(virtualPath)
    {
    }

public:
    bool loadBinary(const NWB::Core::Assets::AssetBytes& binary){
        usize cursor = 0u;
        if(!ReadPOD(binary, cursor, m_marker))
            return false;
        return cursor == binary.size();
    }

public:
    void setMarker(const u32 marker){ m_marker = marker; }
    [[nodiscard]] u32 marker()const{ return m_marker; }

private:
    u32 m_marker = 0u;
};

class ProjectProbeAssetCodec final : public NWB::Core::Assets::AssetCodec<ProjectProbeAsset>{
public:
    ProjectProbeAssetCodec() = default;

public:
    virtual bool serialize(const NWB::Core::Assets::IAsset& asset, NWB::Core::Assets::AssetBytes& outBinary)const override{
        if(asset.assetType() != ProjectProbeAsset::AssetTypeName())
            return false;

        AppendPOD(outBinary, static_cast<const ProjectProbeAsset&>(asset).marker());
        return true;
    }
};

struct ProjectProbeCookEntry{
    Name virtualPath = NAME_NONE;
    u32 marker = 0u;

    explicit ProjectProbeCookEntry(NWB::Core::Assets::CookArena&){
    }
};

static constexpr u32 s_ProjectProbeDocumentMarker = 0x44504345u; // DPCE
static constexpr u32 s_ProjectProbeValueMarker = 0x42504345u; // BPCE

static bool ParseProjectProbeDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const NWB::Core::Metascript::Document&,
    ProjectProbeCookEntry& outEntry,
    NWB::Core::Assets::CookEntryParseContext& context
){
    outEntry = ProjectProbeCookEntry(context.cookArena);
    outEntry.marker = s_ProjectProbeDocumentMarker;
    return NWB::Core::Assets::BuildMetadataDerivedAssetVirtualPath(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        outEntry.virtualPath,
        context.scratchArena
    );
}

static bool ParseProjectProbeValue(
    const Name virtualPath,
    const Path&,
    const NWB::Core::Metascript::Value&,
    ProjectProbeCookEntry& outEntry,
    NWB::Core::Assets::CookEntryParseContext& context
){
    outEntry = ProjectProbeCookEntry(context.cookArena);
    outEntry.virtualPath = virtualPath;
    outEntry.marker = s_ProjectProbeValueMarker;
    return outEntry.virtualPath != NAME_NONE;
}

static bool BuildProjectProbeAsset(ProjectProbeCookEntry& entry, ProjectProbeAsset& outAsset){
    outAsset = ProjectProbeAsset(entry.virtualPath);
    outAsset.setMarker(entry.marker);
    return true;
}

static bool RegisterProjectProbeCookEntry(NWB::Core::Assets::CookEntryRegistry& registry){
    return registry.registerType<ProjectProbeCookEntry, ProjectProbeAsset, ProjectProbeAssetCodec>(
        ProjectProbeAsset::AssetTypeName(),
        NWB_TEXT("project probe asset"),
        &ParseProjectProbeDocument,
        &ParseProjectProbeValue,
        &BuildProjectProbeAsset
    );
}

static NWB::Core::Assets::CookEntryAutoRegistrar s_ProjectProbeCookEntryRegistrar(
    &RegisterProjectProbeCookEntry
);

static bool LoadProjectProbeAsset(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    const Name assetName,
    const u32 expectedMarker
){
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!LoadCookedAsset<ProjectProbeAssetCodec>(
        context,
        testArena,
        outputDirectory,
        assetName,
        loadedAsset,
        0u
    ))
        return false;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedAsset->assetType() == ProjectProbeAsset::AssetTypeName());
    const ProjectProbeAsset* probe = static_cast<const ProjectProbeAsset*>(loadedAsset.get());
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, probe->marker() == expectedMarker);
    return probe->marker() == expectedMarker;
}

static void TestProjectCookEntryAutoRegistration(TestContext& context){
    NWB::Core::Assets::CookArena arena(s_ProjectCookEntryArena);
    NWB::Core::Assets::CookEntryRegistry registry(arena);

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !registry.has(ProjectProbeAsset::AssetTypeName()));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::Assets::RegisterAutoCollectedCookEntryTypes(registry));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, registry.has(ProjectProbeAsset::AssetTypeName()));
}

static void TestProjectCookEntryDocumentCook(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    const bool cooked = CookSingleGraphicsMeta(
        "project_probe asset;\n\n"
        "asset.label = \"document\";\n",
        "project_cook_entry_document",
        "custom",
        "probe.nwb",
        testArena,
        root,
        outputDirectory
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked)
        return;

    LoadProjectProbeAsset(
        context,
        testArena,
        outputDirectory,
        Name("project/custom/probe"),
        s_ProjectProbeDocumentMarker
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestProjectCookEntryAssetBunchCook(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    const bool cooked = CookSingleGraphicsMeta(
        "project_probe probe;\n"
        "probe.label = \"bunch\";\n\n"
        "asset_bunch bunch = [\n"
        "    probe,\n"
        "];\n",
        "project_cook_entry_bunch",
        "custom",
        "bundle.nwb",
        testArena,
        root,
        outputDirectory
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked)
        return;

    LoadProjectProbeAsset(
        context,
        testArena,
        outputDirectory,
        Name("project/custom/bundle/probe"),
        s_ProjectProbeValueMarker
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

