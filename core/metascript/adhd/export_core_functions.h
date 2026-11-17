// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_EXPORT_MEMBER_METHOD("name",                    false, false, false, false, FixedType,              "Name")
NWB_ADHD_EXPORT_MEMBER_METHOD("has",                      false, false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("remove",                   true,  false, false, false, FixedType,              "b8")
NWB_ADHD_EXPORT_MEMBER_METHOD("add",                      true,  false, false, false, FirstTypeArg,           "")
NWB_ADHD_EXPORT_MEMBER_METHOD("tryGet",                   false, false, false, false, FixedType,              "b8")

NWB_ADHD_EXPORT_RECEIVER_METHOD("AssetRef",      "name",         "Name",          false)
NWB_ADHD_EXPORT_RECEIVER_METHOD_MUT("AssetRef", "reset", "void", "", "", false, true)
NWB_ADHD_EXPORT_RECEIVER_METHOD_MUT("AssetRef", "valid", "b8", "", "", true, false)

NWB_ADHD_EXPORT_BUILTIN_FIELD("AssetRef", "virtualPath", "Name",  "",    "")

NWB_ADHD_EXPORT_SPECIAL_METHOD_SEMANTICS("addSystem",    EcsEmplaceSystem)
NWB_ADHD_EXPORT_SPECIAL_METHOD_SEMANTICS("addComponent", EcsEmplaceComponent)
NWB_ADHD_EXPORT_SPECIAL_METHOD_SEMANTICS("loadBinary",   AssetLoadBinary)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

