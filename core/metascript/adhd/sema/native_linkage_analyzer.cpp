// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "native_linkage_analyzer.h"

#include <core/metascript/lexer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_native_linkage_analyzer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr b8 IsCppHeaderSafeInlineKeyword(const MStringView text)noexcept{
    return text == "inline"
        || text == "constexpr"
        || text == "consteval"
        || text == "template"
        || text == "concept";
}

[[nodiscard]] constexpr b8 IsCppTypeSpecifierKeyword(const MStringView text)noexcept{
    return text == "class"
        || text == "struct"
        || text == "interface"
        || text == "union"
        || text == "enum";
}

[[nodiscard]] constexpr b8 IsCppNonVariableDeclarationKeyword(const MStringView text)noexcept{
    return text == "using"
        || text == "typedef"
        || text == "static_assert"
        || text == "operator";
}

[[nodiscard]] constexpr b8 IsCppNonParameterParenKeyword(const MStringView text)noexcept{
    return text == "alignas"
        || text == "decltype"
        || text == "noexcept"
        || text == "sizeof"
        || text == "static_assert"
        || text == "__attribute__"
        || text == "requires"
        || text == "asm";
}

struct NamespaceDeclarationState{
    u32 tokenCount = 0u;
    u32 angleDepth = 0u;
    Token firstToken{};
    Token secondToken{};
    Token previousToken{};
    b8 isNamespace = false;
    b8 hasTypeKeyword = false;
    b8 hasParameterParens = false;
    b8 hasInitializerEqual = false;
    b8 isNonVariableDeclaration = false;
    b8 hasInlineLinkage = false;
    b8 hasStaticStorage = false;
    b8 hasExternStorage = false;
    u32 declaratorTokenCount = 0u;
    b8 declaratorInline = false;
    b8 declaratorStatic = false;
    b8 declaratorExtern = false;
    b8 declaratorConst = false;
    b8 declaratorVolatile = false;
    b8 declaratorQualified = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void NativeLinkageAnalyzer::ClassifyTranslationUnit(TranslationUnit& unit){
    unit.topLevelNativeBlocks.clear();
    unit.sourceNativeBlocks.clear();
    unit.hasImportedNativeBlocks = false;

    for(const NativeBlockDecl& block : unit.nativeBlocks){
        if(block.isImported){
            if(block.hasFollowingLocalDecl || !HasExternalCppDefinition(block.code))
                unit.hasImportedNativeBlocks = true;
            continue;
        }

        b8 emitInSource = false;
        if(block.hasFollowingLocalDecl)
            emitInSource = false;
        else if(block.hasPrecedingLocalDecl)
            emitInSource = true;
        else
            emitInSource = HasExternalCppDefinition(block.code);

        if(emitInSource)
            unit.sourceNativeBlocks.push_back(block.code);
        else
            unit.topLevelNativeBlocks.push_back(block.code);
    }
}

b8 NativeLinkageAnalyzer::HasExternalCppDefinition(const MStringView code)noexcept{
    using namespace __hidden_adhd_native_linkage_analyzer;

    Lexer lexer(code, true);
    u32 namespaceDepth = 0u;
    u32 innerScopeDepth = 0u;
    u32 parenDepth = 0u;
    u32 bracketDepth = 0u;
    NamespaceDeclarationState decl{};

    for(;;){
        const Token tok = lexer.next();
        if(tok.type == TokenType::EndOfFile || tok.type == TokenType::Error)
            break;

        if(innerScopeDepth > 0u){
            if(tok.type == TokenType::LeftBrace){
                ++innerScopeDepth;
            }
            else if(tok.type == TokenType::RightBrace){
                --innerScopeDepth;
                if(innerScopeDepth == 0u)
                    decl = NamespaceDeclarationState{};
            }
            continue;
        }

        if(tok.type == TokenType::LeftBracket){
            ++bracketDepth;
            continue;
        }
        if(tok.type == TokenType::RightBracket){
            if(bracketDepth > 0u)
                --bracketDepth;
            continue;
        }
        if(bracketDepth > 0u)
            continue;

        if(tok.type == TokenType::LeftParen){
            if(parenDepth == 0u){
                const b8 isSpecialParen = decl.previousToken.type == TokenType::Identifier && IsCppNonParameterParenKeyword(decl.previousToken.text);
                if(decl.angleDepth == 0u && !isSpecialParen && !decl.hasInitializerEqual)
                    decl.hasParameterParens = true;
                ++decl.tokenCount;
            }
            ++parenDepth;
            continue;
        }

        if(tok.type == TokenType::RightParen){
            if(parenDepth > 0u){
                --parenDepth;
                if(parenDepth == 0u){
                    ++decl.tokenCount;
                    decl.previousToken = tok;
                }
            }
            continue;
        }

        if(parenDepth > 0u)
            continue;

        const b8 afterOperatorKeyword = decl.previousToken.type == TokenType::Identifier && decl.previousToken.text == "operator";

        if(tok.type == TokenType::Less && !afterOperatorKeyword){
            ++decl.angleDepth;
            ++decl.tokenCount;
            decl.previousToken = tok;
            continue;
        }

        if(tok.type == TokenType::Greater && decl.angleDepth > 0u && !afterOperatorKeyword){
            --decl.angleDepth;
            ++decl.tokenCount;
            decl.previousToken = tok;
            continue;
        }

        if(tok.type == TokenType::GreaterGreater && decl.angleDepth > 0u && !afterOperatorKeyword){
            decl.angleDepth = decl.angleDepth >= 2u ? decl.angleDepth - 2u : 0u;
            ++decl.tokenCount;
            decl.previousToken = tok;
            continue;
        }

        if(tok.type == TokenType::LeftBrace){
            const b8 isExternLanguageBlock = decl.firstToken.type == TokenType::Identifier
                && decl.firstToken.text == "extern"
                && decl.secondToken.type == TokenType::StringLiteral
                && !decl.hasParameterParens
                && !decl.hasInitializerEqual;

            if(decl.isNamespace){
                if(decl.tokenCount == 1u)
                    return true;
                ++namespaceDepth;
            }
            else if(isExternLanguageBlock){
                ++namespaceDepth;
            }
            else if(decl.hasTypeKeyword && !decl.hasParameterParens){
                innerScopeDepth = 1u;
            }
            else if(decl.hasParameterParens && !decl.hasInitializerEqual){
                if(!decl.hasInlineLinkage && !decl.hasStaticStorage)
                    return true;
                innerScopeDepth = 1u;
            }
            else{
                if(decl.tokenCount > 0u && !decl.hasInlineLinkage && !decl.hasStaticStorage && !decl.hasExternStorage && !decl.isNonVariableDeclaration)
                    return true;
                innerScopeDepth = 1u;
            }
            decl = NamespaceDeclarationState{};
            continue;
        }

        if(tok.type == TokenType::RightBrace){
            if(namespaceDepth > 0u)
                --namespaceDepth;
            decl = NamespaceDeclarationState{};
            continue;
        }

        if(tok.type == TokenType::Semicolon){
            if(decl.hasInitializerEqual && !decl.isNonVariableDeclaration && !decl.isNamespace){
                if(decl.declaratorTokenCount > 0u && !decl.declaratorInline && !(decl.declaratorStatic && !decl.declaratorQualified) && !decl.declaratorExtern){
                    const b8 hasInternalConstLinkage = decl.declaratorConst && !decl.declaratorQualified && !decl.declaratorVolatile;
                    if(!hasInternalConstLinkage)
                        return true;
                }
            }
            decl = NamespaceDeclarationState{};
            continue;
        }

        if(tok.type == TokenType::Equal && decl.angleDepth == 0u && !afterOperatorKeyword){
            if(!decl.hasInitializerEqual){
                decl.hasInitializerEqual = true;
                decl.declaratorTokenCount = decl.tokenCount;
                decl.declaratorInline = decl.hasInlineLinkage;
                decl.declaratorStatic = decl.hasStaticStorage;
                decl.declaratorExtern = decl.hasExternStorage;
            }
            ++decl.tokenCount;
            decl.previousToken = tok;
            continue;
        }

        if(decl.tokenCount == 0u)
            decl.firstToken = tok;
        else if(decl.tokenCount == 1u)
            decl.secondToken = tok;
        ++decl.tokenCount;

        if(tok.type == TokenType::Identifier){
            if(tok.text == "namespace")
                decl.isNamespace = true;
            else if(IsCppTypeSpecifierKeyword(tok.text))
                decl.hasTypeKeyword = true;
            else if(IsCppNonVariableDeclarationKeyword(tok.text))
                decl.isNonVariableDeclaration = true;

            if(IsCppHeaderSafeInlineKeyword(tok.text))
                decl.hasInlineLinkage = true;
            else if(tok.text == "static")
                decl.hasStaticStorage = true;
            else if(tok.text == "extern")
                decl.hasExternStorage = true;

            if(!decl.hasInitializerEqual){
                if(tok.text == "const")
                    decl.declaratorConst = true;
                else if(tok.text == "volatile")
                    decl.declaratorVolatile = true;
            }
        }
        else if(!decl.hasInitializerEqual && tok.type == TokenType::ColonColon){
            decl.declaratorQualified = true;
        }

        decl.previousToken = tok;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

