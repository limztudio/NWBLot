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
    [[nodiscard]] static b8 write(Alloc::PersistentArena& arena, const CrashRequest& request);


private:
    template<typename ArenaT> static void appendManifestPropertyPrefix(CrashStringT<ArenaT>& out, StringView key, b8 isFirst);
    [[nodiscard]] static StringView artifactStrategyName(const CrashRequest& request)noexcept;
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> buildManifest(ArenaT& arena, const CrashRequest& request);
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> buildMetadataText(ArenaT& arena, const CrashRequest& request);
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> buildBreadcrumbText(ArenaT& arena, const CrashRequest& request);
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> buildEmergencyText(ArenaT& arena, const CrashRequest& request);
    [[nodiscard]] static b8 hasCpuContext(const CrashRequest& request)noexcept;
    [[nodiscard]] static b8 hasCallstack(const CrashRequest& request)noexcept;
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> buildCpuContextText(ArenaT& arena, const CrashRequest& request);
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> buildCallstackText(ArenaT& arena, const CrashRequest& request);
    template<typename ArenaT> [[nodiscard]] static CrashStringT<ArenaT> buildArtifactStrategyText(ArenaT& arena, const CrashRequest& request);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


