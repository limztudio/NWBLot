// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT(Asset,     "IAsset",  "::NWB::Core::Assets::TypedAsset", "NWB_DEFINE_ASSET_TYPE",     "AssetTypeName",     "s_AssetTypeName",     "s_AssetTypeText",     "<core/assets/module.h>")
NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT(System,    "ISystem", "::NWB::Core::ECS::ISystem",       "NWB_DEFINE_SYSTEM_TYPE",    "SystemTypeName",    "s_SystemTypeName",    "s_SystemTypeText",    "<core/ecs/system.h>")
NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT(Component, "",        "",                                "NWB_DEFINE_COMPONENT_TYPE", "ComponentTypeName", "s_ComponentTypeName", "s_ComponentTypeText", "<core/ecs/component.h>")
NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT(Subsystem, "IAsset",  "::NWB::Impl::TypedSubsystem",     "NWB_DEFINE_ASSET_TYPE",     "",                  "",                    "",                    "<impl/assets_subsystem/asset.adhd.h>")

NWB_ADHD_EXPORT_AUTO_OVERRIDE_METHOD(System, "prepare")
NWB_ADHD_EXPORT_AUTO_OVERRIDE_METHOD(System, "update")
NWB_ADHD_EXPORT_AUTO_OVERRIDE_METHOD(System, "taskOptions")

NWB_ADHD_EXPORT_INTERFACE_CTOR_FORWARD("IRenderPass", "::NWB::Core::IRenderPass", "GraphicsRuntime")

NWB_ADHD_EXPORT_ASSET_CODEC_CONTRACT("loadBinary", 1u, "NWB_DEFINE_ASSET_CODEC", "::NWB::Core::Assets::AssetCodecAutoRegistrar", "::NWB::Core::Assets::CreateAssetCodec", "Asset", "Codec", "AssetCodec", "<core/assets/auto_registration.h>")

NWB_ADHD_EXPORT_SUBSYSTEM_CONTRACT("::NWB::Core::Assets::AssetArena", "::NWB::Impl::TypedSubsystem", "::NWB::Impl::SubsystemAssetRefVector", "::NWB::Core::Assets::AssetRef", "::NWB::Impl::SubsystemFieldDescriptor", "::NWB::Impl::MakeSubsystemFieldDescriptor")

NWB_ADHD_EXPORT_PREAMBLE_HEADER(CoreBase, "<core/alloc/global.h>")
NWB_ADHD_EXPORT_PREAMBLE_HEADER(CoreBase, "<core/alloc/general.h>")
NWB_ADHD_EXPORT_PREAMBLE_HEADER(CoreBase, "<core/alloc/scratch.h>")
NWB_ADHD_EXPORT_PREAMBLE_HEADER(CoreBase, "<core/alloc/adhd_arena.h>")
NWB_ADHD_EXPORT_PREAMBLE_HEADER(CoreBase, "<core/assets/ref.h>")
NWB_ADHD_EXPORT_PREAMBLE_HEADER(CoreBase, "<core/ecs/entity_id.h>")

NWB_ADHD_EXPORT_PREAMBLE_HEADER(HeaderDiagnostic, "<core/common/log.h>")
NWB_ADHD_EXPORT_PREAMBLE_HEADER(HeaderTaskScope,  "<core/task/cpu/scheduler.h>")

NWB_ADHD_EXPORT_NAMESPACE_PREAMBLE("NWB::Impl::Scene", false, "<impl/ecs_scene/global.adhd.h>")
NWB_ADHD_EXPORT_NAMESPACE_PREAMBLE("NWB::Impl",        true,  "<impl/global.adhd.h>")

NWB_ADHD_EXPORT_FILE_CONVENTION(CoreExternPreludePath, "core/metascript/adhd/export_core_prelude.adhd")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

