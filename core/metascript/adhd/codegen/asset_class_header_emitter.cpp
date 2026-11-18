// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_class_header_emitter.h"
#include "codegen_symbols.h"

#include <core/metascript/adhd/sema/asset_type_analyzer.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AssetClassHeaderEmitter::emitSubsystem(MString& out, const ClassDecl& decl)const{
    const AssetTypeAnalyzer assetTypes(m_symbols);
    const SubsystemContractInfo& sub = ContractSemantics::s_Subsystem;
    const ClassRoleContractInfo* subsystemContract = ContractSemantics::FindClassRoleContract(ClassRoleKind::Subsystem);
    out.append("class ").append(decl.name).append(" final : public ").append(sub.typedAssetBase).append("<");
    out.append(decl.name).append(", ").append(sub.familyType).append(">{\r\npublic:\r\n");
    out.append("    ").append(subsystemContract->defineMacroName).append("(\"").append(decl.subsystemTypeName).append("\")\r\n");
    out.append("    using Parents = ::Vector<").append(sub.assetRefTemplate).append("<").append(sub.familyType).append(">, ::ArenaBase>;\r\n");
    out.append("    explicit ").append(decl.name).append("(").append(sub.assetArenaCppType).append("& arena, const ::Name& path = ::s_NameNone)noexcept\r\n");
    out.append("        : ").append(sub.typedAssetBase).append("<").append(decl.name).append(", ").append(sub.familyType).append(">(path)\r\n");
    out.append("        , subsystems(arena)\r\n");

    const auto visitFields = [&](const auto& visit){
        usize index = 0u;
        for(const FieldDecl* field : decl.fields){
            MStringView asset;
            b8 repeated = false;
            if(field && assetTypes.tryResolveSubsystemFieldAssetType(*field, asset, repeated))
                visit(*field, asset, repeated, index++);
        }
        return index;
    };
    const usize count = visitFields([&](const FieldDecl& field, MStringView, const b8 repeated, usize){
        if(repeated)
            out.append("        , ").append(field.name).append("(arena)\r\n");
    });
    out.append("    {}\r\n\r\n    Parents subsystems;\r\n");
    const auto emitRef = [&](const MStringView asset){
        out.append(sub.assetRefTemplate).append("<");
        m_typeEmitter.appendQualifiedName(out, m_symbols.findTypeNamespace(asset), asset);
        out.append(">");
    };
    visitFields([&](const FieldDecl& field, const MStringView asset, const b8 repeated, usize){
        out.append("    ");
        if(repeated)
            out.append(sub.assetVectorTemplate).append("<");
        emitRef(asset);
        if(repeated)
            out.append(">");
        out.append(" ").append(field.name).append(";\r\n");
    });
    out.append("\r\n    const Parents& parentRefs()const noexcept override{ return subsystems; }\r\n");
    out.append("    Parents& editParentRefs()noexcept override{ return subsystems; }\r\n");
    out.append("    b8 loadBinary(const Span<const u8> binary)noexcept override{ return ").append(sub.loadFunction).append("(binary, *this); }\r\n");
    out.append("    b8 validatePayload()const noexcept override{ return ").append(sub.validateFunction).append("(*this); }\r\n");
    out.append("    usize fieldCount()const noexcept override{ return ");
    AppendUnsignedDecimal(out, count);
    out.append("u; }\r\n");
    const auto emitAccess = [&](const MStringView signature, const MStringView fallback, const auto& emitCase){
        out.append("    ").append(signature).append("noexcept override{\r\n        switch(index){\r\n");
        visitFields([&](const FieldDecl& field, const MStringView asset, const b8 repeated, const usize index){
            out.append("        case ");
            AppendUnsignedDecimal(out, index);
            out.append("u: ");
            emitCase(field, asset, repeated);
            out.append("\r\n");
        });
        out.append("        default: FOUNDATION_ASSERT(false); ").append(fallback).append("\r\n        }\r\n    }\r\n");
    };
    out.append("    ");
    out.append(sub.descriptorType).append(" fieldDescriptor(const usize index)const noexcept override{\r\n        switch(index){\r\n");
    visitFields([&](const FieldDecl& field, const MStringView asset, const b8 repeated, const usize index){
        out.append("        case ");
        AppendUnsignedDecimal(out, index);
        out.append("u: return ").append(sub.descriptorType).append("(\"");
        AppendCamelToSnakeCase(out, field.name);
        out.append("\", ::Name(\"");
        const ClassDecl* target = m_symbols.findClass(asset);
        if(target && !target->assetTypeName.empty())
            out.append(target->assetTypeName);
        else
            AppendCamelToSnakeCase(out, asset);
        out.append("\"), ").append(repeated ? "true" : "false").append(");\r\n");
    });
    out.append("        default: FOUNDATION_ASSERT(false); return {};\r\n        }\r\n    }\r\n");
    emitAccess("usize fieldValueCount(const usize index)const ", "return 0u;", [&](const FieldDecl& field, MStringView, const b8 repeated){
        out.append("return ");
        if(repeated)
            out.append(field.name).append(".size();");
        else
            out.append("1u;");
    });
    emitAccess("::Name fieldValue(const usize index, [[maybe_unused]] const usize element)const ", "return ::s_NameNone;", [&](const FieldDecl& field, MStringView, const b8 repeated){
        if(repeated)
            out.append("FOUNDATION_ASSERT(element < ").append(field.name).append(".size()); return ").append(field.name).append("[element].name();");
        else
            out.append("FOUNDATION_ASSERT(element == 0u); return ").append(field.name).append(".name();");
    });
    emitAccess("void clearFieldValues(const usize index)", "return;", [&](const FieldDecl& field, MStringView, const b8 repeated){
        out.append(field.name).append(repeated ? ".clear(); return;" : " = {}; return;");
    });
    emitAccess("void appendFieldValue(const usize index, const ::Name value)", "return;", [&](const FieldDecl& field, const MStringView asset, const b8 repeated){
        out.append(field.name).append(repeated ? ".push_back(" : " = ");
        emitRef(asset);
        out.append(repeated ? "(value)); return;" : "(value); return;");
    });
    out.append("};\r\n\r\n");

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

