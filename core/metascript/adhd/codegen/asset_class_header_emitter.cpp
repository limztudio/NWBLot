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
    const usize scopePos = sub.typedSubsystemBase.rfind(ContractSemantics::s_CppScopeSeparator);
    const MStringView typedSubLeaf = (scopePos == MStringView::npos)
        ? sub.typedSubsystemBase
        : sub.typedSubsystemBase.substr(scopePos + ContractSemantics::s_CppScopeSeparator.size());

    out.append("class ");
    out.append(decl.name.data(), decl.name.size());
    out.append(" final : public ");
    out.append(sub.typedSubsystemBase.data(), sub.typedSubsystemBase.size());
    out.push_back('<');
    out.append(decl.name.data(), decl.name.size());
    out.append(">{\r\n");
    out.append("public:\r\n");
    if(subsystemContract && !subsystemContract->defineMacroName.empty()){
        out.append("    ");
        out.append(subsystemContract->defineMacroName.data(), subsystemContract->defineMacroName.size());
        out.append("(\"");
        out.append(decl.subsystemTypeName.data(), decl.subsystemTypeName.size());
        out.append("\")\r\n\r\n");
    }

    b8 hasVectorField = false;
    for(const FieldDecl* field : decl.fields){
        if(!field)
            continue;
        MStringView assetName;
        b8 isVectorField = false;
        if(assetTypes.tryResolveSubsystemFieldAssetType(*field, assetName, isVectorField) && isVectorField){
            hasVectorField = true;
            break;
        }
    }

    out.append("public:\r\n");
    if(!hasVectorField){
        out.append("    using ");
        out.append(sub.typedSubsystemBase.data(), sub.typedSubsystemBase.size());
        out.push_back('<');
        out.append(decl.name.data(), decl.name.size());
        out.push_back('>');
        out.append(ContractSemantics::s_CppScopeSeparator.data(), ContractSemantics::s_CppScopeSeparator.size());
        out.append(typedSubLeaf.data(), typedSubLeaf.size());
        out.append(";\r\n\r\n");
    }
    else{
        const auto emitVectorFieldInitializers = [&](){
            for(const FieldDecl* field : decl.fields){
                if(!field)
                    continue;
                MStringView assetName;
                b8 isVectorField = false;
                if(assetTypes.tryResolveSubsystemFieldAssetType(*field, assetName, isVectorField) && isVectorField){
                    out.append("        , ");
                    out.append(field->name.data(), field->name.size());
                    out.append("(arena)\r\n");
                }
            }
        };
        out.append("    explicit ");
        out.append(decl.name.data(), decl.name.size());
        out.push_back('(');
        out.append(sub.assetArenaCppType.data(), sub.assetArenaCppType.size());
        out.append("& arena)noexcept\r\n        : ");
        out.append(typedSubLeaf.data(), typedSubLeaf.size());
        out.append("(arena)\r\n");
        emitVectorFieldInitializers();
        out.append("    {}\r\n");
        out.append("    ");
        out.append(decl.name.data(), decl.name.size());
        out.push_back('(');
        out.append(sub.assetArenaCppType.data(), sub.assetArenaCppType.size());
        out.append("& arena, const ");
        out.append(CodegenSymbols::s_NameCppType.data(), CodegenSymbols::s_NameCppType.size());
        out.append("& virtualPath)noexcept\r\n        : ");
        out.append(typedSubLeaf.data(), typedSubLeaf.size());
        out.append("(arena, virtualPath)\r\n");
        emitVectorFieldInitializers();
        out.append("    {}\r\n\r\n");
    }

    out.append("public:\r\n");
    for(const FieldDecl* field : decl.fields){
        if(!field)
            continue;
        MStringView assetName;
        b8 isVectorField = false;
        if(!assetTypes.tryResolveSubsystemFieldAssetType(*field, assetName, isVectorField))
            continue;
        const MStringView fieldTmpl = isVectorField ? sub.assetRefVectorTemplate : sub.assetRefTemplate;
        out.append("    ");
        out.append(fieldTmpl.data(), fieldTmpl.size());
        out.push_back('<');
        out.append(assetName.data(), assetName.size());
        out.append("> ");
        out.append(field->name.data(), field->name.size());
        out.append(";\r\n");
    }

    out.append("\r\npublic:\r\n    static constexpr ");
    out.append(sub.fieldDescTemplate.data(), sub.fieldDescTemplate.size());
    out.push_back('<');
    out.append(decl.name.data(), decl.name.size());
    out.append("> s_Fields[] = {\r\n");
    for(const FieldDecl* field : decl.fields){
        if(!field)
            continue;
        MStringView assetName;
        b8 isVectorField = false;
        if(!assetTypes.tryResolveSubsystemFieldAssetType(*field, assetName, isVectorField))
            continue;
        out.append("        ");
        out.append(sub.makeFieldDescTemplate.data(), sub.makeFieldDescTemplate.size());
        out.push_back('<');
        out.append(decl.name.data(), decl.name.size());
        out.append(", ");
        m_typeEmitter.appendQualifiedName(out, m_symbols.findTypeNamespace(assetName), assetName);
        out.append(", &");
        out.append(decl.name.data(), decl.name.size());
        out.append(ContractSemantics::s_CppScopeSeparator.data(), ContractSemantics::s_CppScopeSeparator.size());
        out.append(field->name.data(), field->name.size());
        out.append(">(\"");
        AppendCamelToSnakeCase(out, field->name);
        out.append("\", ");
        out.append(CodegenSymbols::s_NameCppType.data(), CodegenSymbols::s_NameCppType.size());
        out.append("(\"");
        const ClassDecl* assetCls = m_symbols.findClass(assetName);
        if(assetCls && !assetCls->assetTypeName.empty())
            out.append(assetCls->assetTypeName.data(), assetCls->assetTypeName.size());
        else
            AppendCamelToSnakeCase(out, assetName);
        out.append("\")),\r\n");
    }
    out.append("    };\r\n");
    out.append("};\r\n\r\n");

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

