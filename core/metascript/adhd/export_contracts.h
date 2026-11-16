// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_ADHD_EXPORT_FILE_CONVENTION
#define NWB_ADHD_EXPORT_FILE_CONVENTION(idName, textValue)
#define NWB_ADHD_EXPORT_FILE_CONVENTION_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT
#define NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT(roleEnum, implicitInterfaceAdhdName, cppBaseTypeOrTemplate, defineMacroName, typeNameStaticMethod, typeNameStaticField, typeTextStaticField, requiredPreambleHeader)
#define NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_AUTO_OVERRIDE_METHOD
#define NWB_ADHD_EXPORT_AUTO_OVERRIDE_METHOD(roleEnum, methodName)
#define NWB_ADHD_EXPORT_AUTO_OVERRIDE_METHOD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_INTERFACE_CTOR_FORWARD
#define NWB_ADHD_EXPORT_INTERFACE_CTOR_FORWARD(interfaceAdhdName, cppBaseType, paramAdhdTypeName)
#define NWB_ADHD_EXPORT_INTERFACE_CTOR_FORWARD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_ASSET_CODEC_CONTRACT
#define NWB_ADHD_EXPORT_ASSET_CODEC_CONTRACT(hookMethodName, hookParamCount, defineCodecMacro, registrarCppType, factoryCppTemplate, assetNameSuffix, shortCodecSuffix, fullCodecSuffix, registrarHeader)
#define NWB_ADHD_EXPORT_ASSET_CODEC_CONTRACT_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SUBSYSTEM_CONTRACT
#define NWB_ADHD_EXPORT_SUBSYSTEM_CONTRACT(assetArenaCppType, typedSubsystemBase, assetRefVectorTemplate, assetRefTemplate, fieldDescTemplate, makeFieldDescTemplate)
#define NWB_ADHD_EXPORT_SUBSYSTEM_CONTRACT_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PREAMBLE_HEADER
#define NWB_ADHD_EXPORT_PREAMBLE_HEADER(groupEnum, headerText)
#define NWB_ADHD_EXPORT_PREAMBLE_HEADER_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_NAMESPACE_PREAMBLE
#define NWB_ADHD_EXPORT_NAMESPACE_PREAMBLE(namespaceMatch, isPrefixMatch, headerText)
#define NWB_ADHD_EXPORT_NAMESPACE_PREAMBLE_AUTO_UNDEF
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "export_global_contracts.h"
#include "export_core_contracts.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_ADHD_EXPORT_NAMESPACE_PREAMBLE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_NAMESPACE_PREAMBLE_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_NAMESPACE_PREAMBLE
#endif

#ifdef NWB_ADHD_EXPORT_PREAMBLE_HEADER_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PREAMBLE_HEADER_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PREAMBLE_HEADER
#endif

#ifdef NWB_ADHD_EXPORT_SUBSYSTEM_CONTRACT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SUBSYSTEM_CONTRACT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SUBSYSTEM_CONTRACT
#endif

#ifdef NWB_ADHD_EXPORT_ASSET_CODEC_CONTRACT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ASSET_CODEC_CONTRACT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_ASSET_CODEC_CONTRACT
#endif

#ifdef NWB_ADHD_EXPORT_INTERFACE_CTOR_FORWARD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_INTERFACE_CTOR_FORWARD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_INTERFACE_CTOR_FORWARD
#endif

#ifdef NWB_ADHD_EXPORT_AUTO_OVERRIDE_METHOD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_AUTO_OVERRIDE_METHOD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_AUTO_OVERRIDE_METHOD
#endif

#ifdef NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT
#endif

#ifdef NWB_ADHD_EXPORT_FILE_CONVENTION_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FILE_CONVENTION_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_FILE_CONVENTION
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

