// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_cook_role_emitter.h"
#include "cpp_type_emitter.h"

#include <core/metascript/adhd/ast/contract_semantics.h>
#include <core/metascript/adhd/sema/asset_type_analyzer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AssetCookRoleEmitter::Emit(MString& out, const TranslationUnit& unit, const SymbolTable& symbols){
    for(const AssetRegistration& registration : unit.assetRegistrations){
        const TypeRef* registrar = symbols.unwrapTypeAlias(registration.registrar);
        const b8 cookOnly = registrar && (registrar->name == ContractSemantics::s_FamilyCookRegistrationType || registrar->name == ContractSemantics::s_CompositeRootRegistrationType || registrar->name == ContractSemantics::s_NonStandaloneAssetRegistrationType);
        if(cookOnly)
            out.append("#if defined(NWB_COOK)\r\n");
        out.append("static ");
        CppTypeEmitter(symbols).emitTypeCpp(out, *registration.registrar);
        out.append(" s_").append(registration.className).append("AssetRegistrar;\r\n");
        if(cookOnly)
            out.append("#endif\r\n");
    }
    for(const MethodDecl* fn : unit.freeFunctions){
        if(!fn || fn->isImported)
            continue;
        if(!fn->pairedSidecar.empty()){
            const auto* builtin = symbols.findGlobalConstant(fn->pairedSidecar) ? nullptr : FindBuiltinGlobalConstant(fn->pairedSidecar);
            out.append("#if defined(NWB_COOK)\r\nstatic ::NWB::Core::Assets::AssetPairedSidecarRuleAutoRegistrar s_").append(fn->name);
            out.append("Sidecar({ .sidecarExtension = ").append(builtin ? builtin->cppExportName : fn->pairedSidecar).append(" });\r\n#endif\r\n\r\n");
        }
        if(fn->assetCookRole == AssetCookRole::None)
            continue;
        if(fn->assetCookRole == AssetCookRole::DocumentParser || fn->assetCookRole == AssetCookRole::VolumePrepare || fn->assetCookRole == AssetCookRole::Setup){
            out.append("#if defined(NWB_COOK)\r\n");
            if(fn->assetCookRole == AssetCookRole::DocumentParser)
                out.append("static ::NWB::Core::Assets::AssetMetadataParserAutoRegistrar s_").append(fn->name).append("Registrar(&").append(fn->name).append(", nullptr);");
            else if(fn->assetCookRole == AssetCookRole::VolumePrepare)
                out.append("static ::NWB::Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareAutoRegistrar s_").append(fn->name).append("Registrar(&").append(fn->name).append(");");
            else
                out.append("[[maybe_unused]] static const bool s_").append(fn->name).append("Registered = (").append(fn->name).append("(), true);");
            out.append("\r\n#endif\r\n\r\n");
            continue;
        }
        if(fn->assetCookRole == AssetCookRole::GatherMerge){
            out.append("static ::NWB::Core::Assets::AssetGatherMergeAutoRegistrar s_").append(fn->name);
            out.append("Registrar(&::NWB::Core::Assets::InvokeAssetGatherMerge<&").append(fn->name).append(">);\r\n\r\n");
            continue;
        }
        const MStringView firstType = symbols.resolveBaseTypeName(fn->params.front().type->name);
        out.append("#if defined(NWB_COOK)\r\n");
        if(fn->assetCookRole == AssetCookRole::Writer){
            const MStringView suffix = ContractSemantics::ResolveAssetCodecSuffix(firstType);
            out.append("b8 ").append(firstType).append(suffix);
            out.append("::serialize(const ::NWB::Core::Assets::IAsset& asset, ::NWB::Core::Assets::AssetBytes& outBinary)const{\r\n");
            out.append("    if(!checkSerializeAssetType(asset, FOUNDATION_TEXT(\"").append(firstType).append(suffix).append("::serialize\")))\r\n");
            out.append("        return false;\r\n");
            out.append("    return ").append(fn->name).append("(*::MakeNotNull(::checked_cast<const ").append(firstType).append("*>(&asset)), outBinary);\r\n}\r\n");
        }
        else if(fn->assetCookRole == AssetCookRole::Entry){
            const MStringView asset = fn->returnType->templateArgs.front().constantOrTypeName();
            const MethodDecl* reader = AssetTypeAnalyzer(symbols).findCookEntryReader(fn->cookValueReader, fn->cookAcceptsValue);
            out.append("static ::NWB::Core::Assets::CookEntryAutoRegistrar s_").append(fn->name).append("Registrar([](::NWB::Core::Assets::CookEntryRegistry& registry){\r\n");
            const MStringView entry = fn->params.front().type->name;
            out.append("    return ::NWB::Core::Assets::RegisterAuthoredCookEntry<").append(entry).append(", ").append(asset).append(", ");
            if(fn->cookCodec.empty())
                out.append(asset).append(ContractSemantics::ResolveAssetCodecSuffix(asset));
            else
                out.append(fn->cookCodec);
            out.append(", ").append(fn->cookDiagnosticPrefix).append(">(\r\n");
            out.append("        registry, FOUNDATION_TEXT(\"").append(fn->cookKindLabel).append("\"), ").append(fn->cookAcceptsValue ? "true" : "false").append(", ").append(fn->cookLogsFailure ? "true" : "false").append(",\r\n");
            if(reader->params[0].type->name == "Path"){
                out.append("        []([[maybe_unused]] ::NWB::Core::Assets::CookEntryParseContext& context, const auto& root, const AStringView virtualRoot, const auto& path, const ::NWB::Core::Metascript::Document& document){ return ");
                out.append(reader->name).append("(").append(reader->injectsCallerArena ? "context.cookArena, " : "").append("root, virtualRoot, path, document");
            }
            else{
                out.append("        []([[maybe_unused]] ::NWB::Core::Assets::CookEntryParseContext& context, const Name pathName, const auto& path, const ::NWB::Core::Metascript::Value& value){ return ");
                out.append(reader->name).append("(").append(reader->injectsCallerArena ? "context.cookArena, " : "").append("pathName, path, value");
                if(reader->params.size() == 4u)
                    out.append(", context.cpuScheduler");
            }
            out.append("); },\r\n");
            out.append("        []([[maybe_unused]] ::NWB::Core::Assets::AssetArena& arena, ").append(entry).append("& entry){ return ");
            out.append(fn->name).append("(").append(fn->injectsCallerArena ? "arena, " : "").append("entry); }\r\n    );\r\n});\r\n");
        }
        else{
            const MStringView registrar = firstType == "CookEntryRegistry" ? "CookEntryAutoRegistrar" : "CookDependencyAutoRegistrar";
            out.append("static ::NWB::Core::Assets::").append(registrar).append(" s_").append(fn->name).append("Registrar(&").append(fn->name).append(");\r\n");
        }
        out.append("#endif\r\n\r\n");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

