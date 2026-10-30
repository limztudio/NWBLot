// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_ADHD_EXPORT_OPERATOR_TOKEN
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, isCompoundAssignment)
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMD_BINARY_OP
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol, floatIntrinsic, doubleIntrinsic, int128Intrinsic, int256Intrinsic)
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP_AUTO_UNDEF
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Equal,               "=",   false, true,  false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Plus,                "+",   true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Minus,               "-",   true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Star,                "*",   true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Slash,               "/",   true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Percent,             "%",   true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PlusEqual,           "+=",  true,  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::MinusEqual,          "-=",  true,  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::StarEqual,           "*=",  true,  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::SlashEqual,          "/=",  true,  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PercentEqual,        "%=",  true,  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Ampersand,           "&",   true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Pipe,                "|",   true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Caret,               "^",   true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Tilde,               "~",   true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Bang,                "!",   false, false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::LessLess,            "<<",  true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::GreaterGreater,      ">>",  true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::AmpersandEqual,      "&=",  true,  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PipeEqual,           "|=",  true,  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::CaretEqual,          "^=",  true,  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::LessLessEqual,       "<<=", true,  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::GreaterGreaterEqual, ">>=", true,  true,  true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::EqualEqual,          "==",  true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::BangEqual,           "!=",  true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Less,                "<",   true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::LessEqual,           "<=",  true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Greater,             ">",   true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::GreaterEqual,        ">=",  true,  false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PlusPlus,            "++",  false, false, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::MinusMinus,          "--",  false, false, false)

NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Plus,           TokenType::PlusEqual,           " + ",  "::VectorAdd(",      "::VectorDoubleAdd(",      "::VectorIntAdd(",      "::VectorInt256Add(")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Minus,          TokenType::MinusEqual,          " - ",  "::VectorSubtract(", "::VectorDoubleSubtract(", "::VectorIntSubtract(", "::VectorInt256Subtract(")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Star,           TokenType::StarEqual,           " * ",  "::VectorMultiply(", "::VectorDoubleMultiply(", "::VectorIntMultiply(", "")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Slash,          TokenType::SlashEqual,          " / ",  "::VectorDivide(",   "::VectorDoubleDivide(",   "",                     "")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Percent,        TokenType::PercentEqual,        " % ",  "",                  "",                        "",                     "")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Ampersand,      TokenType::AmpersandEqual,      " & ",  "",                  "",                        "::VectorIntAnd(",      "::VectorInt256And(")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Pipe,           TokenType::PipeEqual,           " | ",  "",                  "",                        "::VectorIntOr(",       "::VectorInt256Or(")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Caret,          TokenType::CaretEqual,          " ^ ",  "",                  "",                        "::VectorIntXor(",      "::VectorInt256Xor(")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::LessLess,       TokenType::LessLessEqual,       " << ", "",                  "",                        "",                     "")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::GreaterGreater, TokenType::GreaterGreaterEqual, " >> ", "",                  "",                        "",                     "")


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

