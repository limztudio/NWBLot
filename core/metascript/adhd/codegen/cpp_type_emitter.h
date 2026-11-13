// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../sema/type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr MStringView s_AdhdGeneratedFileBanner = "// Compiled by ADHD Compiler " NWB_ADHD_COMPILER_VERSION "\r\n";
inline constexpr usize s_AdhdEmitReserveBytes = 4096u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CppTypeEmitter final{
public:
    [[nodiscard]] static MStringView MapBuiltinCppType(MStringView typeName)noexcept;


public:
    explicit CppTypeEmitter(const SymbolTable& symbols)noexcept
        : m_symbols(symbols)
    {}


public:
    void setCurrentNamespace(MStringView namespacePath)const noexcept{ m_currentNamespace = namespacePath; }
    [[nodiscard]] const SymbolTable& symbols()const noexcept{ return m_symbols; }
    [[nodiscard]] MStringView resolveCrossNamespace(MStringView declNamespace)const noexcept{
        if(declNamespace.empty() || declNamespace == m_currentNamespace)
            return {};
        return declNamespace;
    }
    void appendQualifiedName(MString& out, MStringView declNamespace, MStringView symbolName)const;
    void emitConstantTextCpp(MString& out, MStringView text)const;
    void emitTypeArgCpp(MString& out, const TypeArg& arg)const;
    void emitTypeCpp(MString& out, const TypeRef& type)const;
    void emitReturnTypeCpp(MString& out, const TypeRef& type)const;
    void emitTypeWithCustomArenaCpp(MString& out, const TypeRef& type, MStringView arenaExpr)const;
    void emitTypeArgListCpp(MString& out, Span<const TypeArg> args)const;
    void emitParamTypeCpp(MString& out, ParamPassingMode::Enum mode, const TypeRef& type, b8 topLevelConst = true)const;
    void emitParamListCpp(MString& out, Span<const ParamDecl> params, b8 injectCallerArena = false)const;
    void emitTemplateHeaderCpp(MString& out, Span<const TemplateParamDecl> params)const;
    void emitCallableTemplateHeaderCpp(MString& out, const MethodDecl& callable, b8 includeDefaults, MStringView indent = {})const;
    void emitMethodSignature(MString& out, const ClassDecl* ownerClass, const MethodDecl& method, b8 isDeclarationInClass)const;
    void emitMethodSignature(MString& out, const ClassDecl* ownerClass, const MethodDecl& method, b8 isDeclarationInClass, b8 ownerNeedsMotherArena)const;


private:
    const SymbolTable& m_symbols;
    mutable MStringView m_currentNamespace;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

