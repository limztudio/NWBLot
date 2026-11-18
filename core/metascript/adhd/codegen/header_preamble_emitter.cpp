// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_preamble_emitter.h"

#include "codegen_symbols.h"
#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void HeaderPreambleEmitter::Emit(MString& out, const TranslationUnit& unit, const MStringView namespacePath){
    auto appendInclude = [&](const MStringView hdr){
        if(hdr.empty())
            return;
        out.append(CodegenSymbols::s_IncludePrefix.data(), CodegenSymbols::s_IncludePrefix.size());
        out.append(hdr.data(), hdr.size());
        out.append("\r\n");
    };
    auto emitPreambleGroup = [&](const PreambleHeaderGroup::Enum group){
        ContractSemantics::ForEachPreambleHeader(group, [&](const MStringView hdr){
            appendInclude(hdr);
        });
    };

    out.append(s_AdhdGeneratedFileBanner.data(), s_AdhdGeneratedFileBanner.size());
    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");
    out.append("#pragma once\r\n\r\n\r\n");
    if(!unit.namespaceWrapper.empty())
        appendInclude(ContractSemantics::s_EngineNamespaceHeader);
    if(IsNameLiteralOnlyUnit(unit)){
        emitPreambleGroup(PreambleHeaderGroup::NameLiteralOnly);
    }
    else if(IsScalarLiteralOnlyUnit(unit)){
        emitPreambleGroup(PreambleHeaderGroup::ScalarLiteralOnly);
    }
    else{
        emitPreambleGroup(PreambleHeaderGroup::GlobalBase);
        for(const MStringView hdr : unit.requiredGlobalHeaders){
            if(hdr.starts_with(ContractSemantics::s_GlobalIncludePrefix))
                appendInclude(hdr);
        }
        emitPreambleGroup(PreambleHeaderGroup::CoreBase);
        for(const MStringView hdr : unit.requiredGlobalHeaders){
            if(!hdr.starts_with(ContractSemantics::s_GlobalIncludePrefix))
                appendInclude(hdr);
        }
        if(const MStringView nsHdr = ContractSemantics::ResolveNamespacePreambleHeader(namespacePath); !nsHdr.empty())
            appendInclude(nsHdr);
        for(const MStringView extraHdr : unit.extraPreambleHeaders)
            appendInclude(extraHdr);
    }

    const auto stmtsUseDiagnostic = [](const auto& self, const Span<const Stmt* const> stmts)noexcept -> b8{
        for(const Stmt* s : stmts){
            if(!s)
                continue;
            if(s->kind == StmtKind::Diagnostic)
                return true;
            if(self(self, s->statements) || self(self, s->elseStatements))
                return true;
            for(const SwitchCase& sc : s->switchCases){
                if(self(self, sc.statements))
                    return true;
            }
        }
        return false;
    };

    b8 hasAssetDecl = false;
    b8 hasSystemDecl = false;
    b8 hasSubsystemDecl = false;
    b8 hasComponentDecl = false;
    b8 hasHeaderTaskScope = false;
    b8 hasHeaderDiagnostic = false;
    for(const InterfaceDecl* ifaceDecl : unit.interfaces){
        if(ifaceDecl && !ifaceDecl->isImported && !ifaceDecl->isExtern && !ifaceDecl->assetTypeName.empty())
            hasAssetDecl = true;
    }
    for(const ClassDecl* classDecl : unit.classes){
        if(!classDecl || classDecl->isExtern)
            continue;
        for(const MethodDecl* method : classDecl->methods){
            if(!method || method->isImported || method->isExtern)
                continue;
            if(ShouldEmitCallableInHeader(classDecl, *method)){
                if(method->usesTaskScope)
                    hasHeaderTaskScope = true;
                if(!hasHeaderDiagnostic && stmtsUseDiagnostic(stmtsUseDiagnostic, method->body))
                    hasHeaderDiagnostic = true;
            }
        }
        if(classDecl->isImported)
            continue;
        if(!classDecl->assetTypeName.empty())
            hasAssetDecl = true;
        if(!classDecl->systemTypeName.empty())
            hasSystemDecl = true;
        if(!classDecl->subsystemTypeName.empty())
            hasSubsystemDecl = true;
        if(!classDecl->componentTypeName.empty())
            hasComponentDecl = true;
    }
    auto emitRoleHeader = [&](const ClassRoleKind::Enum role){
        if(const ClassRoleContractInfo* info = ContractSemantics::FindClassRoleContract(role); info && !info->requiredPreambleHeader.empty())
            appendInclude(info->requiredPreambleHeader);
    };
    if(hasAssetDecl)
        emitRoleHeader(ClassRoleKind::Asset);
    if(hasSystemDecl)
        emitRoleHeader(ClassRoleKind::System);
    if(hasSubsystemDecl)
        emitRoleHeader(ClassRoleKind::Subsystem);
    if(hasComponentDecl)
        emitRoleHeader(ClassRoleKind::Component);
    for(const MethodDecl* fn : unit.freeFunctions){
        if(fn && !fn->isImported && !fn->isExtern && ShouldEmitCallableInHeader(nullptr, *fn)){
            if(fn->usesTaskScope)
                hasHeaderTaskScope = true;
            if(!hasHeaderDiagnostic && stmtsUseDiagnostic(stmtsUseDiagnostic, fn->body))
                hasHeaderDiagnostic = true;
        }
    }
    if(hasHeaderDiagnostic)
        emitPreambleGroup(PreambleHeaderGroup::HeaderDiagnostic);
    if(hasHeaderTaskScope)
        emitPreambleGroup(PreambleHeaderGroup::HeaderTaskScope);

    for(const MString& inc : unit.includedAdhdFiles){
        const MStringView incView = StripAdhdExtension(MStringView(inc.data(), inc.size()));
        out.append(CodegenSymbols::s_IncludePrefix.data(), CodegenSymbols::s_IncludePrefix.size());
        out.push_back('"');
        out.append(incView.data(), incView.size());
        out.append(ContractSemantics::s_GeneratedHeaderSuffix.data(), ContractSemantics::s_GeneratedHeaderSuffix.size());
        out.append("\"\r\n");
    }

    if(!unit.namespaceWrapper.empty()){
        const MStringView prefix = unit.namespaceWrapper;
        const MStringView root = ContractSemantics::s_DefaultRootNamespace;
        const b8 nestedInEngine = namespacePath.starts_with(root) && namespacePath.substr(root.size()).starts_with("::");
        const b8 engineNamespace = namespacePath == root || nestedInEngine;
        const MStringView relative = nestedInEngine ? namespacePath.substr(root.size() + 2u) : (engineNamespace ? MStringView{} : namespacePath);
        out.append("\r\n#define ");
        out.append(prefix.data(), prefix.size());
        out.append("_BEGIN");
        if(engineNamespace)
            out.append(" NWB_BEGIN");
        if(!relative.empty()){
            out.append(" namespace ");
            out.append(relative.data(), relative.size());
            out.push_back('{');
        }
        out.append("\r\n#define ");
        out.append(prefix.data(), prefix.size());
        out.append("_END");
        if(!relative.empty())
            out.append(" };");
        if(engineNamespace)
            out.append(" NWB_END");
        out.append("\r\n");
    }

    if(!unit.topLevelNativeBlocks.empty()){
        out.append("\r\n");
        for(const MStringView& rawBlock : unit.topLevelNativeBlocks){
            AppendTextDiscardCrAndExpandLf(out, rawBlock);
            out.append("\r\n");
        }
    }

    out.append("\r\n\r\n////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");

    if(!namespacePath.empty()){
        out.append("namespace ");
        out.append(namespacePath.data(), namespacePath.size());
        out.append("{\r\n\r\n\r\n");
    }

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

