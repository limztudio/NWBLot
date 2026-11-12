// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_class_header_emitter.h"

#include <core/metascript/adhd/sema/asset_type_analyzer.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AssetClassHeaderEmitter::emitSubsystem(MString& out, const ClassDecl& decl)const{
    const AssetTypeAnalyzer assetTypes(m_symbols);
    out.append("class ");
    out.append(decl.name.data(), decl.name.size());
    out.append(" final : public ::NWB::Impl::TypedSubsystem<");
    out.append(decl.name.data(), decl.name.size());
    out.append(">{\r\n");
    out.append("public:\r\n");
    out.append("    NWB_DEFINE_ASSET_TYPE(\"");
    out.append(decl.subsystemTypeName.data(), decl.subsystemTypeName.size());
    out.append("\")\r\n\r\n");

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
        out.append("    using ::NWB::Impl::TypedSubsystem<");
        out.append(decl.name.data(), decl.name.size());
        out.append(">::TypedSubsystem;\r\n\r\n");
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
        out.append("(::NWB::Core::Assets::AssetArena& arena)noexcept\r\n");
        out.append("        : TypedSubsystem(arena)\r\n");
        emitVectorFieldInitializers();
        out.append("    {}\r\n");
        out.append("    ");
        out.append(decl.name.data(), decl.name.size());
        out.append("(::NWB::Core::Assets::AssetArena& arena, const ::Name& virtualPath)noexcept\r\n");
        out.append("        : TypedSubsystem(arena, virtualPath)\r\n");
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
        out.append(isVectorField ? "    ::NWB::Impl::SubsystemAssetRefVector<" : "    ::NWB::Core::Assets::AssetRef<");
        out.append(assetName.data(), assetName.size());
        out.append("> ");
        out.append(field->name.data(), field->name.size());
        out.append(";\r\n");
    }

    out.append("\r\npublic:\r\n");
    out.append("    static constexpr ::NWB::Impl::SubsystemFieldDescriptor<");
    out.append(decl.name.data(), decl.name.size());
    out.append("> s_Fields[] = {\r\n");
    for(const FieldDecl* field : decl.fields){
        if(!field)
            continue;
        MStringView assetName;
        b8 isVectorField = false;
        if(!assetTypes.tryResolveSubsystemFieldAssetType(*field, assetName, isVectorField))
            continue;
        out.append("        ::NWB::Impl::MakeSubsystemFieldDescriptor<");
        out.append(decl.name.data(), decl.name.size());
        out.append(", ");
        out.append(assetName.data(), assetName.size());
        out.append(", &");
        out.append(decl.name.data(), decl.name.size());
        out.append("::");
        out.append(field->name.data(), field->name.size());
        out.append(">(\"");
        AppendCamelToSnakeCase(out, field->name);
        out.append("\", ::Name(\"");
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

