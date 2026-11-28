// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpp_header_importer_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CppHeaderImportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendExternAttributes(MString& out, const MStringView requiredHeader, const MStringView cppExportName, const MStringView indent){
    MStringView headerInner;
    if(!requiredHeader.empty()){
        b8 hadQuotes = false;
        const MStringView inner = StripHeaderDelimiters(requiredHeader, nullptr, &hadQuotes);
        if(!hadQuotes && !inner.empty() && inner.find('"') == MStringView::npos)
            headerInner = inner;
    }
    if(headerInner.empty() && cppExportName.empty())
        return;

    out.append(indent.data(), indent.size());
    out.push_back('[');
    b8 hasAttr = false;
    if(!headerInner.empty()){
        out.append(KeywordSemantics::s_ExternAttrCppHeader.data(), KeywordSemantics::s_ExternAttrCppHeader.size());
        out.append("(\"");
        out.append(headerInner.data(), headerInner.size());
        out.append("\")");
        hasAttr = true;
    }
    if(!cppExportName.empty()){
        if(hasAttr)
            out.append(", ");
        out.append(KeywordSemantics::s_ExternAttrCppName.data(), KeywordSemantics::s_ExternAttrCppName.size());
        out.append("(\"");
        out.append(cppExportName.data(), cppExportName.size());
        out.append("\")");
    }
    out.append("]\r\n");
}

void AppendTypeRefExternText(MString& out, const TypeRef& type, const b8 includeRefMode){
    if(includeRefMode && type.refMode != ParamPassingMode::None){
        const MStringView refKeyword = KeywordSemantics::ParamPassingModeKeyword(type.refMode);
        if(!refKeyword.empty()){
            out.append(refKeyword.data(), refKeyword.size());
            out.push_back(' ');
        }
    }

    out.append(type.name.data(), type.name.size());
    if(!type.templateArgs.empty()){
        out.push_back('<');
        for(usize i = 0u; i < type.templateArgs.size(); ++i){
            if(i > 0u)
                out.append(", ");
            const TypeArg& targ = type.templateArgs[i];
            if(targ.type)
                AppendTypeRefExternText(out, *targ.type, true);
            else
                out.append(targ.constantText.data(), targ.constantText.size());
        }
        out.push_back('>');
    }
}

void AppendMethodExternSignature(MString& out, const MethodDecl& m, const MStringView indent, const b8 isFreeFunction){
    AppendExternAttributes(out, isFreeFunction ? m.requiredHeader : MStringView(), m.cppExportName, indent);
    out.append(indent.data(), indent.size());
    if(m.isConstructor){
        out.append(m.name.data(), m.name.size());
    }
    else{
        if(m.isStatic){
            out.append(KeywordSemantics::s_Static.data(), KeywordSemantics::s_Static.size());
            out.push_back(' ');
        }
        if(isFreeFunction){
            out.append(KeywordSemantics::s_Extern.data(), KeywordSemantics::s_Extern.size());
            out.push_back(' ');
        }
        if(m.returnType)
            AppendTypeRefExternText(out, *m.returnType, true);
        else{
            const MStringView voidName = BuiltinTypeSemantics::VoidTypeName();
            out.append(voidName.data(), voidName.size());
        }
        out.push_back(' ');
        if(m.isOperator){
            out.append(KeywordSemantics::s_Operator.data(), KeywordSemantics::s_Operator.size());
            out.append(m.name.data(), m.name.size());
        }
        else{
            out.append(m.name.data(), m.name.size());
        }
        if(!m.templateParams.empty()){
            out.push_back('<');
            for(usize i = 0u; i < m.templateParams.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                out.append(m.templateParams[i].name.data(), m.templateParams[i].name.size());
            }
            out.push_back('>');
        }
    }

    out.push_back('(');
    for(usize i = 0u; i < m.params.size(); ++i){
        if(i > 0u)
            out.append(", ");
        const ParamDecl& p = m.params[i];
        if(!p.type)
            continue;
        if(p.type->refMode == ParamPassingMode::None && p.mode != ParamPassingMode::None){
            const MStringView modeKeyword = KeywordSemantics::ParamPassingModeKeyword(p.mode);
            if(!modeKeyword.empty()){
                out.append(modeKeyword.data(), modeKeyword.size());
                out.push_back(' ');
            }
        }
        AppendTypeRefExternText(out, *p.type, true);
        out.push_back(' ');
        out.append(p.name.data(), p.name.size());
        if(m.hasDefaultArgs && i >= m.minArgCount)
            out.append(" = {}");
    }
    out.push_back(')');
    if(m.isConst)
        out.append(KeywordSemantics::s_Const.data(), KeywordSemantics::s_Const.size());
    out.append(";\r\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MString CppHeaderImporter::FormatExternAdhd(const TranslationUnit& unit, MetaArena& arena){
    MString out(arena);
    const usize estimatedSize = 4096u
        + unit.enums.size() * 256u
        + unit.typeAliases.size() * 96u
        + unit.globalConstants.size() * 96u
        + unit.classes.size() * 512u
        + unit.freeFunctions.size() * 128u;
    out.reserve(estimatedSize);
    out.append(CppHeaderImportDetail::CppImporterSemantics::s_EmitExternsBanner.data(), CppHeaderImportDetail::CppImporterSemantics::s_EmitExternsBanner.size());

    for(const EnumDecl* e : unit.enums){
        if(!e || !e->isExtern)
            continue;
        CppHeaderImportDetail::AppendExternAttributes(out, e->requiredHeader, e->cppExportName, "");
        const MStringView enumKeyword = KeywordSemantics::EnumDeclKeyword(e->isFlag);
        out.append(KeywordSemantics::s_Extern.data(), KeywordSemantics::s_Extern.size());
        out.push_back(' ');
        out.append(enumKeyword.data(), enumKeyword.size());
        out.push_back(' ');
        out.append(e->name.data(), e->name.size());
        if(!e->sizeTypeName.empty()){
            out.append(" : ");
            out.append(e->sizeTypeName.data(), e->sizeTypeName.size());
        }
        out.append("{\r\n");
        for(usize i = 0u; i < e->entries.size(); ++i){
            out.append("    ");
            out.append(e->entries[i].name.data(), e->entries[i].name.size());
            if(const Expr* val = e->entries[i].valueExpr){
                out.append(" = ");
                if(val->kind == ExprKind::Unary && val->op == TokenType::Minus && val->rhs){
                    out.push_back('-');
                    out.append(val->rhs->text.data(), val->rhs->text.size());
                }
                else{
                    out.append(val->text.data(), val->text.size());
                }
            }
            if(i + 1u < e->entries.size())
                out.push_back(',');
            out.append("\r\n");
        }
        out.append("};\r\n\r\n");
    }

    for(const TypeAliasDecl* a : unit.typeAliases){
        if(!a || !a->isExtern || !a->aliasedType || a->name.empty() || !a->templateParams.empty())
            continue;
        CppHeaderImportDetail::AppendExternAttributes(out, a->requiredHeader, a->cppExportName, "");
        out.append(KeywordSemantics::s_Extern.data(), KeywordSemantics::s_Extern.size());
        out.push_back(' ');
        out.append(KeywordSemantics::s_Type.data(), KeywordSemantics::s_Type.size());
        out.push_back(' ');
        out.append(a->name.data(), a->name.size());
        out.append(" = ");
        CppHeaderImportDetail::AppendTypeRefExternText(out, *a->aliasedType, false);
        out.append(";\r\n\r\n");
    }

    for(const Stmt* g : unit.globalConstants){
        if(!g || !g->isExtern || !g->varType || g->name.empty())
            continue;
        CppHeaderImportDetail::AppendExternAttributes(out, g->requiredHeader, g->cppExportName, "");
        out.append(KeywordSemantics::s_Extern.data(), KeywordSemantics::s_Extern.size());
        out.push_back(' ');
        out.append(KeywordSemantics::s_Const.data(), KeywordSemantics::s_Const.size());
        out.push_back(' ');
        CppHeaderImportDetail::AppendTypeRefExternText(out, *g->varType, false);
        out.push_back(' ');
        out.append(g->name.data(), g->name.size());
        out.append(";\r\n\r\n");
    }

    for(const ClassDecl* c : unit.classes){
        if(!c || !c->isExtern)
            continue;
        CppHeaderImportDetail::AppendExternAttributes(out, c->requiredHeader, c->cppExportName, "");
        out.append(KeywordSemantics::s_Extern.data(), KeywordSemantics::s_Extern.size());
        out.push_back(' ');
        out.append(KeywordSemantics::s_Class.data(), KeywordSemantics::s_Class.size());
        out.push_back(' ');
        out.append(c->name.data(), c->name.size());
        b8 emittedBase = false;
        const MStringView noCopyName = CppHeaderImportDetail::CppImporterSemantics::s_NoCopyBaseClass;
        const MStringView moveOnlyName = CppHeaderImportDetail::CppImporterSemantics::s_MoveOnlyBaseClass;
        if(!c->isCopyConstructible){
            const MStringView ownershipBaseName = (c->isMoveConstructible && c->isMoveAssignable) ? moveOnlyName : noCopyName;
            out.append(" : ");
            out.append(ownershipBaseName.data(), ownershipBaseName.size());
            emittedBase = true;
        }
        for(const TypeRef* iface : c->implementedInterfaces){
            if(!iface || iface->name.empty())
                continue;
            if(!c->isCopyConstructible && (iface->name == noCopyName || iface->name == moveOnlyName))
                continue;
            out.append(emittedBase ? ", " : " : ");
            CppHeaderImportDetail::AppendTypeRefExternText(out, *iface, false);
            emittedBase = true;
        }
        out.append("{\r\n");
        for(const FieldDecl* f : c->fields){
            if(!f || f->access != AccessSpecifier::Public || !f->type)
                continue;
            out.append("    ");
            if(f->isStatic){
                out.append(KeywordSemantics::s_Static.data(), KeywordSemantics::s_Static.size());
                out.push_back(' ');
            }
            CppHeaderImportDetail::AppendTypeRefExternText(out, *f->type, true);
            out.push_back(' ');
            out.append(f->name.data(), f->name.size());
            out.append(";\r\n");
        }
        for(const MethodDecl* m : c->methods){
            if(m && !(m->isOperator && !m->templateParams.empty()))
                CppHeaderImportDetail::AppendMethodExternSignature(out, *m, "    ", false);
        }
        out.append("};\r\n\r\n");
    }

    for(const MethodDecl* f : unit.freeFunctions){
        if(f && f->isExtern)
            CppHeaderImportDetail::AppendMethodExternSignature(out, *f, "", true);
    }

    return out;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

