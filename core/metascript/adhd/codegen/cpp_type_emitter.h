// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../sema/type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr MStringView s_AdhdGeneratedFileBanner = "// Compiled by ADHD Compiler " NWB_ADHD_COMPILER_VERSION "\r\n";
inline constexpr usize s_AdhdEmitReserveBytes = 4096u;

void AppendCamelToSnakeCase(MString& out, MStringView text);
void AppendAdhdTextCrlf(MString& out, MStringView text);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CppTypeEmitter final{
public:
    [[nodiscard]] static MStringView MapBuiltinCppType(MStringView typeName)noexcept;


public:
    explicit CppTypeEmitter(const SymbolTable& symbols)noexcept
        : m_symbols(symbols)
    {}


public:
    void emitTypeCpp(MString& out, const TypeRef& type)const;
    void emitTypeArgListCpp(MString& out, Span<const TypeArg> args)const;
    void emitParamTypeCpp(MString& out, ParamPassingMode::Enum mode, const TypeRef& type, b8 topLevelConst = true)const;
    void emitParamListCpp(MString& out, Span<const ParamDecl> params)const;
    void emitTemplateHeaderCpp(MString& out, Span<const TemplateParamDecl> params)const;
    void emitMethodSignature(MString& out, const ClassDecl* ownerClass, const MethodDecl& method, b8 isDeclarationInClass)const;


private:
    const SymbolTable& m_symbols;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

