// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cpp_header_importer.h"

#include <clang-c/Index.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CppHeaderImportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline MStringView InternMString(MetaArena& arena, const MString& text){
    return InternArenaString(arena, MStringView(text.data(), text.size()));
}

[[nodiscard]] MString NormalizeSlashes(MetaArena& arena, MStringView raw);
[[nodiscard]] MStringView TrimOuterQuotesAndAngles(MStringView text)noexcept;
[[nodiscard]] b8 StartsWithCaseInsensitivePrefix(MStringView fullPath, MStringView prefix)noexcept;
[[nodiscard]] b8 IsInternalIdentifier(MStringView name)noexcept;
[[nodiscard]] b8 IsValidAdhdTypeIdentifier(MStringView name)noexcept;
[[nodiscard]] Expr* AllocateSignedIntegerLiteral(MetaArena& arena, long long val);
[[nodiscard]] MString ConsumeClangString(MetaArena& arena, CXString cxStr);
void RewriteTemplateTypeRefName(TypeRef* ref, MStringView oldName, MStringView newName)noexcept;
[[nodiscard]] b8 FormatNormalizedAdhdDeclName(
    MetaArena& arena,
    CXCursor declCursor,
    MStringView& outName,
    MStringView& outNamespace,
    MStringView& outCppExportName,
    b8& outIsDirectEnum,
    b8& outIsFlag
);

void AppendExternAttributes(MString& out, MStringView requiredHeader, MStringView cppExportName, MStringView indent);
void AppendTypeRefExternText(MString& out, const TypeRef& type, b8 includeRefMode);
void AppendMethodExternSignature(MString& out, const MethodDecl& m, MStringView indent, b8 includeHeader);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ImporterContext final{
public:
    ImporterContext(MetaArena& arena, TranslationUnit& unit)noexcept
        : m_arena(arena)
        , m_unit(unit)
        , m_includeRoots(arena)
        , m_headerCache(arena)
        , m_classByExportName(arena)
        , m_classByName(arena)
        , m_enumByExportName(arena)
        , m_enumByName(arena)
        , m_aliasByName(arena)
        , m_constantByName(arena)
        , m_seenFreeFunctions(arena)
        , m_seenConstants(arena)
    {}


public:
    void run(CXTranslationUnit tu, Span<const MString> normalizedIncludeRoots);


private:
    struct ClassTraits{
        b8 isCopyConstructible = true;
        b8 isMoveConstructible = true;
        b8 isCopyAssignable = true;
        b8 isMoveAssignable = true;
        b8 hasExplicitDestructor = false;
        b8 hasUserDeclaredCopyCtor = false;
        b8 hasUserDeclaredCopyAssign = false;
        b8 hasUserDeclaredMoveCtor = false;
        b8 hasUserDeclaredMoveAssign = false;
        MVector<CXCursor> publicBaseRecords;

        explicit ClassTraits(MetaArena& arena)noexcept : publicBaseRecords(arena){}
    };


private:
    [[nodiscard]] MStringView resolveRequiredHeader(CXCursor cursor);
    [[nodiscard]] MStringView mapOperatorToken(MStringView spelling)noexcept;
    [[nodiscard]] b8 hasDefaultInitializerToken(CXCursor parmCursor)const;
    [[nodiscard]] TypeRef* mapGenericContainerType(CXType type, CXType canonical, MStringView rawSpelling, MStringView sugaredLeaf, ParamPassingMode::Enum refMode, u32 depth, b8& outWasGenericContainer);
    [[nodiscard]] TypeRef* mapType(CXType type, b8 isParamContext, u32 depth = 0u);
    [[nodiscard]] b8 evaluateIntegralConstant(CXCursor cursor, MStringView& outText);
    [[nodiscard]] MethodDecl* buildMethodDecl(CXCursor cursor, MStringView ownerClassName, b8 isTemplate, const ClassDecl* ownerClass = nullptr);
    void visitChildrenList(CXCursor parent, MVector<CXCursor>& outChildren)const;
    void inspectBaseAndSpecialMembers(CXCursor classCursor, ClassTraits& traits);
    void importClassMethodsFromCursor(CXCursor classCursor, ClassDecl& targetClass, b8 isBasePass);
    void extractEnum(CXCursor enumCursor);
    void extractTypeAlias(CXCursor aliasCursor);
    void extractClass(CXCursor classCursor);
    void extractGlobalConstant(CXCursor varCursor);
    void extractFreeFunction(CXCursor fnCursor, b8 isTemplate);
    void visitScopeCursor(CXCursor parentCursor);

    [[nodiscard]] ClassDecl* findExistingClassForMerge(MStringView adhdName, MStringView cppExportName)const noexcept;
    [[nodiscard]] EnumDecl* findExistingEnumForMerge(MStringView adhdName, MStringView cppExportName)const noexcept;
    [[nodiscard]] static b8 methodSignaturesEqual(const MethodDecl& a, const MethodDecl& b)noexcept;
    [[nodiscard]] b8 hasClassFieldNamed(const ClassDecl& target, MStringView fieldName)noexcept;
    [[nodiscard]] MString buildFreeFunctionSigKey(const MethodDecl& decl)const;
    void tryAddClassField(ClassDecl& targetClass, CXCursor child, CXType recordType, b8 isStatic);
    static void addUniqueMethod(ClassDecl& target, MethodDecl* method);


private:
    MetaArena& m_arena;
    TranslationUnit& m_unit;
    MVector<MString> m_includeRoots;
    MStringMap<MStringView> m_headerCache;
    MStringMap<ClassDecl*> m_classByExportName;
    MStringMap<ClassDecl*> m_classByName;
    MStringMap<EnumDecl*> m_enumByExportName;
    MStringMap<EnumDecl*> m_enumByName;
    MStringMap<TypeAliasDecl*> m_aliasByName;
    MStringMap<Stmt*> m_constantByName;
    MStringMap<u8> m_seenFreeFunctions;
    MStringMap<u8> m_seenConstants;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

