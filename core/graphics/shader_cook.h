// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u64 s_DefaultSegmentSize = 16ull * 1024ull * 1024ull;
inline constexpr u64 s_DefaultMetadataSize = 512ull * 1024ull;

using CookArena = Core::Alloc::CustomArena;

template<typename T>
using CookAllocator = Core::Alloc::CustomAllocator<T>;

template<typename T>
using CookVector = Vector<T, CookAllocator<T>>;

template<typename T, typename V>
using CookMap = HashMap<T, V, Hasher<T>, EqualTo<T>, CookAllocator<Pair<const T, V>>>;

template<typename T>
using CookHashSet = HashSet<T, Hasher<T>, EqualTo<T>, CookAllocator<T>>;

using DefineCombo = CookMap<AString, AString>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ManifestEntry{
    explicit ManifestEntry(CookArena& arena)
        : defineValues(CookAllocator<Pair<const AString, CookVector<AString>>>(arena))
    {}

    AString name;
    AString compiler = "glslang";
    AString stage;
    AString targetProfile;
    AString entryPoint = "main";
    AString source;
    AString defaultVariant;

    CookMap<AString, CookVector<AString>> defineValues;
};

struct ManifestData{
    explicit ManifestData(CookArena& arena)
        : includeRoots(CookAllocator<AString>(arena))
        , entries(CookAllocator<ManifestEntry>(arena))
    {}

    AString volumeName = "graphics";
    u64 segmentSize = s_DefaultSegmentSize;
    u64 metadataSize = s_DefaultMetadataSize;

    CookVector<AString> includeRoots;
    CookVector<ManifestEntry> entries;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseManifestFile(const Path& manifestPath, CookArena& arena, ManifestData& outManifest, AString& outError);
bool GatherShaderDependencies(
    const Path& sourcePath,
    const CookVector<Path>& includeDirectories,
    CookArena& arena,
    CookVector<Path>& outDependencies,
    AString& outError
);

void ExpandDefineCombinations(
    const CookMap<AString, CookVector<AString>>& defineValues,
    CookArena& arena,
    CookVector<DefineCombo>& outCombinations
);
AString BuildVariantName(const DefineCombo& combo, CookArena& arena);

bool ComputeSourceChecksum(
    const ManifestEntry& entry,
    AStringView variantName,
    const CookVector<Path>& dependencies,
    CookArena& arena,
    u64& outChecksum,
    AString& outError
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

