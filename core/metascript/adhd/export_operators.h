// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_ADHD_EXPORT_OPERATOR_TOKEN
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment)
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMD_BINARY_OP
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol)
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP_AUTO_UNDEF
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Equal,               "=",   false, true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Plus,                "+",   true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Minus,               "-",   true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Star,                "*",   true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Slash,               "/",   true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Percent,             "%",   true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PlusEqual,           "+=",  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::MinusEqual,          "-=",  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::StarEqual,           "*=",  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::SlashEqual,          "/=",  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PercentEqual,        "%=",  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Ampersand,           "&",   true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Pipe,                "|",   true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Caret,               "^",   true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Tilde,               "~",   true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Bang,                "!",   false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::LessLess,            "<<",  true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::GreaterGreater,      ">>",  true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::AmpersandEqual,      "&=",  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PipeEqual,           "|=",  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::CaretEqual,          "^=",  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::LessLessEqual,       "<<=", true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::GreaterGreaterEqual, ">>=", true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::EqualEqual,          "==",  true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::BangEqual,           "!=",  true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Less,                "<",   true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::LessEqual,           "<=",  true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Greater,             ">",   true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::GreaterEqual,        ">=",  true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PlusPlus,            "++",  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::MinusMinus,          "--",  false, false)

NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Plus,           TokenType::PlusEqual,           " + ")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Minus,          TokenType::MinusEqual,          " - ")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Star,           TokenType::StarEqual,           " * ")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Slash,          TokenType::SlashEqual,          " / ")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Percent,        TokenType::PercentEqual,        " % ")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Ampersand,      TokenType::AmpersandEqual,      " & ")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Pipe,           TokenType::PipeEqual,           " | ")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Caret,          TokenType::CaretEqual,          " ^ ")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::LessLess,       TokenType::LessLessEqual,       " << ")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::GreaterGreater, TokenType::GreaterGreaterEqual, " >> ")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_ADHD_EXPORT_SIMD_BINARY_OP_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
#endif

#ifdef NWB_ADHD_EXPORT_OPERATOR_TOKEN_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

