// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_EXPORT_GENERIC_TYPE("AssetRef",           "::NWB::Core::Assets::AssetRef", SingleType,   None,        8u,  8u, false, true,  false, false, true,  false, false, false, false, false, None,        false, true,  true,  false, false, "")
NWB_ADHD_EXPORT_GENERIC_TYPE("ComponentPool", "::NWB::Core::ECS::ComponentPool", SingleType, None, 0u, 8u, false, false, false, false, false, false, false, true, true, false, None, false, false, false, false, false, "<core/ecs/component.h>")
NWB_ADHD_EXPORT_GENERIC_TYPE("TypedAsset", "::NWB::Core::Assets::TypedAsset", SingleType, None, 0u, 8u, false, false, false, false, false, false, false, false, false, false, None, false, false, false, false, false, "<core/assets/module.h>")
NWB_ADHD_EXPORT_GENERIC_TYPE("AssetCodec", "::NWB::Core::Assets::AssetCodec", SingleType, None, 8u, 8u, false, true, false, false, false, false, false, false, false, false, None, false, false, false, false, false, "<core/assets/module.h>")

NWB_ADHD_EXPORT_NON_COPYABLE_BASE_INTERFACE("Core::Assets::IAsset", "Core::Assets::IAsset")

NWB_ADHD_EXPORT_ARENA_TYPE("AssetArena")
NWB_ADHD_EXPORT_ARENA_TYPE("CookArena")
NWB_ADHD_EXPORT_ARENA_TYPE("GlobalArena")
NWB_ADHD_EXPORT_ARENA_TYPE("PersistentArena")
NWB_ADHD_EXPORT_ARENA_TYPE("ScratchArena")

NWB_ADHD_EXPORT_POINTER_LIKE_GENERIC_TYPE("AssetRef",   true, false)
NWB_ADHD_EXPORT_POINTER_LIKE_GENERIC_TYPE("TypedAsset", true, false)
NWB_ADHD_EXPORT_POINTER_LIKE_GENERIC_TYPE("AssetCodec", true, false)

NWB_ADHD_EXPORT_BINARY_HASH_TYPE("AssetRef", ".virtualPath")

NWB_ADHD_EXPORT_NWB_VALUE_TYPE("EntityID",      ".isInteger()", ".asInteger()", "::NWB::Core::ECS::EntityID(static_cast<u32>(", "))",                       "__f->isInteger()) this->",                                                             " = ::NWB::Core::ECS::EntityID(static_cast<u32>(__f->asInteger()));\r\n", "__f->isInteger()) this->",                                                             " = ::NWB::Core::ECS::EntityID(static_cast<u32>(__f->asInteger()));\r\n", false)
NWB_ADHD_EXPORT_NWB_VALUE_TYPE("AssetRef",      ".isString()",  ".asString()",  "{::Name::FromText(",                           ").value_or(::Name{})}",    "__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->",   ".virtualPath = *__name; }\r\n",                                          "__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->",   " = {*__name}; }\r\n",                               true)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

