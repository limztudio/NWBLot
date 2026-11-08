// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "package_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CrashPackageReport{
public:
    [[nodiscard]] static b8 Write(Alloc::PersistentArena& arena, const CrashRequest& request);


private:
    template<typename ArenaT> static void AppendManifestPropertyPrefix(CrashStringT<ArenaT>& out, StringView key, b8 isFirst);
    [[nodiscard]] static StringView ArtifactStrategyName(const CrashRequest& request)noexcept;
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> BuildManifest(ArenaT& arena, const CrashRequest& request);
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> BuildMetadataText(ArenaT& arena, const CrashRequest& request);
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> BuildBreadcrumbText(ArenaT& arena, const CrashRequest& request);
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> BuildEmergencyText(ArenaT& arena, const CrashRequest& request);
    [[nodiscard]] static b8 HasCpuContext(const CrashRequest& request)noexcept;
    [[nodiscard]] static b8 HasCallstack(const CrashRequest& request)noexcept;
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> BuildCpuContextText(ArenaT& arena, const CrashRequest& request);
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> BuildCallstackText(ArenaT& arena, const CrashRequest& request);
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> BuildArtifactStrategyText(ArenaT& arena, const CrashRequest& request);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


