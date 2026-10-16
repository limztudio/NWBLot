// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/alloc/module.h>
#include <core/task/cpu/scheduler.h>

#include <global/compile.h>
#include <global/assert.h>
#include <global/containers.h>
#include <global/filesystem.h>
#include <global/hash_utils.h>
#include <global/limit.h>
#include <global/simdmath.h>
#include <global/simd/type.h>
#include <global/simplemath.h>
#include <global/text_utils.h>
#include <global/type.h>

#include <ufbx.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_FBX_TO_NWB_BEGIN NWB_BEGIN namespace FbxToNwb{
#define NWB_FBX_TO_NWB_END }; NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UtilityDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_UtilityArena("utilities/fbx_to_nwb/utility");
inline constexpr Name s_MeshBuildScratch("utilities/fbx_to_nwb/mesh_build");
inline constexpr Name s_MeshRefreshScratch("utilities/fbx_to_nwb/mesh_refresh");
inline constexpr Name s_AssetWriteScratch("utilities/fbx_to_nwb/asset_write");

inline Core::Alloc::GlobalArena& Arena(){
    static Core::Alloc::GlobalArena s_Arena(s_UtilityArena);
    return s_Arena;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = DefaultAString<Core::Alloc::GlobalArena, UtilityDetail::Arena>;
using AStringStream = DefaultAStringStream<Core::Alloc::GlobalArena, UtilityDetail::Arena>;

template<
    typename K,
    typename V,
    typename Hash = Hasher<K>,
    typename Equal = EqualTo<K>
>
using HashMap = DefaultHashMap<K, V, Core::Alloc::GlobalArena, UtilityDetail::Arena, Hash, Equal>;

template<typename T>
using UtilityVector = DefaultVector<T, Core::Alloc::GlobalArena, UtilityDetail::Arena>;

template<typename T>
using UtilityScratchVector = ::Vector<T, Core::Alloc::ScratchArena>;

template<typename K, typename V, typename Hash = Hasher<K>, typename Equal = EqualTo<K>>
using UtilityScratchMap = ::HashMap<K, V, Core::Alloc::ScratchArena, Hash, Equal>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

