// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <global/array.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ClassRoleKind{
    enum Enum : u8{
#define NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT(roleEnum, implicitInterfaceAdhdName, cppBaseTypeOrTemplate, defineMacroName, typeNameStaticMethod, typeNameStaticField, typeTextStaticField, requiredPreambleHeader) \
        roleEnum,
#include "../export_contracts.h"
#undef NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT
    };
};

namespace PreambleHeaderGroup{
    enum Enum : u8{
        NameLiteralOnly,
        ScalarLiteralOnly,
        GlobalBase,
        CoreBase,
        HeaderDiagnostic,
        HeaderTaskScope,
        SourceDefault,
    };
};

struct ClassRoleContractInfo{
    ClassRoleKind::Enum role;
    MStringView implicitInterfaceAdhdName;
    MStringView cppBaseTypeOrTemplate;
    MStringView defineMacroName;
    MStringView typeNameStaticMethod;
    MStringView typeNameStaticField;
    MStringView typeTextStaticField;
    MStringView requiredPreambleHeader;
};

struct AutoOverrideMethodInfo{
    ClassRoleKind::Enum role;
    MStringView methodName;
};

struct InterfaceCtorForwardInfo{
    MStringView interfaceAdhdName;
    MStringView cppBaseType;
    MStringView paramAdhdTypeName;
};

struct AssetCodecContractInfo{
    MStringView hookMethodName;
    usize hookParamCount;
    MStringView defineCodecMacro;
    MStringView registrarCppType;
    MStringView factoryCppTemplate;
    MStringView assetNameSuffix;
    MStringView shortCodecSuffix;
    MStringView fullCodecSuffix;
    MStringView registrarHeader;
};

struct SubsystemContractInfo{
    MStringView assetArenaCppType;
    MStringView typedSubsystemBase;
    MStringView assetRefVectorTemplate;
    MStringView assetRefTemplate;
    MStringView fieldDescTemplate;
    MStringView makeFieldDescTemplate;
};

struct NamespacePreambleInfo{
    MStringView namespaceMatch;
    b8 isPrefixMatch;
    MStringView headerText;
};


class ContractSemantics final{
public:
#define NWB_ADHD_EXPORT_FILE_CONVENTION(idName, textValue) \
    inline static constexpr MStringView s_##idName = textValue;
#include "../export_contracts.h"
#undef NWB_ADHD_EXPORT_FILE_CONVENTION

    inline static constexpr auto s_ClassRoleContracts = Array{
    #define NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT(roleEnum, implicitInterfaceAdhdName, cppBaseTypeOrTemplate, defineMacroName, typeNameStaticMethod, typeNameStaticField, typeTextStaticField, requiredPreambleHeader) \
        ClassRoleContractInfo{ ClassRoleKind::roleEnum, implicitInterfaceAdhdName, cppBaseTypeOrTemplate, defineMacroName, typeNameStaticMethod, typeNameStaticField, typeTextStaticField, requiredPreambleHeader },
    #include "../export_contracts.h"
    #undef NWB_ADHD_EXPORT_CLASS_ROLE_CONTRACT
    };

    inline static constexpr auto s_AutoOverrideMethods = Array{
    #define NWB_ADHD_EXPORT_AUTO_OVERRIDE_METHOD(roleEnum, methodName) \
        AutoOverrideMethodInfo{ ClassRoleKind::roleEnum, methodName },
    #include "../export_contracts.h"
    #undef NWB_ADHD_EXPORT_AUTO_OVERRIDE_METHOD
    };

    inline static constexpr auto s_InterfaceCtorForwards = Array{
    #define NWB_ADHD_EXPORT_INTERFACE_CTOR_FORWARD(interfaceAdhdName, cppBaseType, paramAdhdTypeName) \
        InterfaceCtorForwardInfo{ interfaceAdhdName, cppBaseType, paramAdhdTypeName },
    #include "../export_contracts.h"
    #undef NWB_ADHD_EXPORT_INTERFACE_CTOR_FORWARD
    };

private:
    inline static constexpr auto s_AssetCodecContracts = Array{
    #define NWB_ADHD_EXPORT_ASSET_CODEC_CONTRACT(hookMethodName, hookParamCount, defineCodecMacro, registrarCppType, factoryCppTemplate, assetNameSuffix, shortCodecSuffix, fullCodecSuffix, registrarHeader) \
        AssetCodecContractInfo{ hookMethodName, hookParamCount, defineCodecMacro, registrarCppType, factoryCppTemplate, assetNameSuffix, shortCodecSuffix, fullCodecSuffix, registrarHeader },
    #include "../export_contracts.h"
    #undef NWB_ADHD_EXPORT_ASSET_CODEC_CONTRACT
    };

    inline static constexpr auto s_SubsystemContracts = Array{
    #define NWB_ADHD_EXPORT_SUBSYSTEM_CONTRACT(assetArenaCppType, typedSubsystemBase, assetRefVectorTemplate, assetRefTemplate, fieldDescTemplate, makeFieldDescTemplate) \
        SubsystemContractInfo{ assetArenaCppType, typedSubsystemBase, assetRefVectorTemplate, assetRefTemplate, fieldDescTemplate, makeFieldDescTemplate },
    #include "../export_contracts.h"
    #undef NWB_ADHD_EXPORT_SUBSYSTEM_CONTRACT
    };

    inline static constexpr auto s_NamespacePreambles = Array{
    #define NWB_ADHD_EXPORT_NAMESPACE_PREAMBLE(namespaceMatch, isPrefixMatch, headerText) \
        NamespacePreambleInfo{ namespaceMatch, isPrefixMatch, headerText },
    #include "../export_contracts.h"
    #undef NWB_ADHD_EXPORT_NAMESPACE_PREAMBLE
    };


public:
    inline static constexpr AssetCodecContractInfo s_AssetCodec = s_AssetCodecContracts[0];
    inline static constexpr SubsystemContractInfo s_Subsystem = s_SubsystemContracts[0];


public:
    [[nodiscard]] static constexpr MStringView EnumInnerName(const b8 isFlag)noexcept{
        return isFlag ? s_FlagInnerName : s_EnumInnerName;
    }

    [[nodiscard]] static constexpr b8 HasWrappedNwbEnumSuffix(const MStringView cppExportName)noexcept{
        return cppExportName.ends_with(s_EnumScopeSuffix) || cppExportName.ends_with(s_FlagScopeSuffix);
    }

    [[nodiscard]] static constexpr MStringView StripWrappedNwbEnumSuffix(MStringView scopeName)noexcept{
        if(scopeName.size() > s_EnumScopeSuffix.size() && scopeName.ends_with(s_EnumScopeSuffix))
            scopeName.remove_suffix(s_EnumScopeSuffix.size());
        else if(scopeName.size() > s_FlagScopeSuffix.size() && scopeName.ends_with(s_FlagScopeSuffix))
            scopeName.remove_suffix(s_FlagScopeSuffix.size());
        return scopeName;
    }

    [[nodiscard]] static constexpr MStringView ResolveAssetCodecSuffix(const MStringView className)noexcept{
        return className.ends_with(s_AssetCodec.assetNameSuffix) ? s_AssetCodec.shortCodecSuffix : s_AssetCodec.fullCodecSuffix;
    }

    [[nodiscard]] static constexpr const ClassRoleContractInfo* FindClassRoleContract(const ClassRoleKind::Enum role)noexcept{
        for(const ClassRoleContractInfo& info : s_ClassRoleContracts){
            if(info.role == role)
                return &info;
        }
        return nullptr;
    }

    [[nodiscard]] static constexpr b8 IsAutoOverrideMethod(const ClassRoleKind::Enum role, const MStringView methodName)noexcept{
        for(const AutoOverrideMethodInfo& info : s_AutoOverrideMethods){
            if(info.role == role && info.methodName == methodName)
                return true;
        }
        return false;
    }

    [[nodiscard]] static constexpr b8 IsClassRoleTypeNameMethod(const ClassRoleKind::Enum role, const MStringView methodName)noexcept{
        const ClassRoleContractInfo* info = FindClassRoleContract(role);
        return info && !info->typeNameStaticMethod.empty() && info->typeNameStaticMethod == methodName;
    }

    [[nodiscard]] static constexpr b8 IsClassRoleTypeNameStaticField(const ClassRoleKind::Enum role, const MStringView fieldName)noexcept{
        const ClassRoleContractInfo* info = FindClassRoleContract(role);
        return info && !info->typeNameStaticField.empty() && info->typeNameStaticField == fieldName;
    }

    [[nodiscard]] static constexpr b8 IsClassRoleTypeTextStaticField(const ClassRoleKind::Enum role, const MStringView fieldName)noexcept{
        const ClassRoleContractInfo* info = FindClassRoleContract(role);
        return info && !info->typeTextStaticField.empty() && info->typeTextStaticField == fieldName;
    }

    [[nodiscard]] static constexpr const InterfaceCtorForwardInfo* FindInterfaceCtorForward(const MStringView interfaceAdhdName)noexcept{
        for(const InterfaceCtorForwardInfo& info : s_InterfaceCtorForwards){
            if(info.interfaceAdhdName == interfaceAdhdName)
                return &info;
        }
        return nullptr;
    }

    [[nodiscard]] static constexpr MStringView ResolveNamespacePreambleHeader(const MStringView namespacePath)noexcept{
        if(namespacePath.empty())
            return {};
        for(const NamespacePreambleInfo& entry : s_NamespacePreambles){
            if(!entry.isPrefixMatch && namespacePath == entry.namespaceMatch)
                return entry.headerText;
        }
        for(const NamespacePreambleInfo& entry : s_NamespacePreambles){
            if(entry.isPrefixMatch && namespacePath.size() >= entry.namespaceMatch.size() && namespacePath.substr(0u, entry.namespaceMatch.size()) == entry.namespaceMatch)
                return entry.headerText;
        }
        return {};
    }

    template<typename EmitFn>
    static void ForEachPreambleHeader(const PreambleHeaderGroup::Enum group, EmitFn&& emit){
    #define NWB_ADHD_EXPORT_PREAMBLE_HEADER(groupEnum, headerText) \
        if(PreambleHeaderGroup::groupEnum == group || (group == PreambleHeaderGroup::SourceDefault && (PreambleHeaderGroup::groupEnum == PreambleHeaderGroup::HeaderDiagnostic || PreambleHeaderGroup::groupEnum == PreambleHeaderGroup::HeaderTaskScope))) \
            emit(MStringView(headerText));
    #include "../export_contracts.h"
    #undef NWB_ADHD_EXPORT_PREAMBLE_HEADER
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

