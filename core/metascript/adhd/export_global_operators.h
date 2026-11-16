// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Equal,               "=",   false, true,  false, false, false, false, 0u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PipePipe,            "||",  false, false, true,  false, false, false, 1u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::AmpersandAmpersand,  "&&",  false, false, true,  false, false, false, 2u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Plus,                "+",   true,  false, false, false, true,  false, 9u,  0u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Minus,               "-",   true,  false, false, false, true,  false, 9u,  0u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Star,                "*",   true,  false, false, false, false, false, 10u, 1u, 1u, true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Slash,               "/",   true,  false, false, false, false, false, 10u, 1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Percent,             "%",   true,  false, false, false, false, false, 10u, 1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PlusEqual,           "+=",  true,  true,  false, false, false, false, 0u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::MinusEqual,          "-=",  true,  true,  false, false, false, false, 0u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::StarEqual,           "*=",  true,  true,  false, false, false, false, 0u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::SlashEqual,          "/=",  true,  true,  false, false, false, false, 0u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PercentEqual,        "%=",  true,  true,  false, false, false, false, 0u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Pipe,                "|",   true,  false, false, true,  false, false, 3u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Ampersand,           "&",   true,  false, false, true,  false, false, 5u,  1u, 1u, true)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Caret,               "^",   true,  false, false, true,  false, false, 4u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Tilde,               "~",   true,  false, false, false, true,  false, 0u,  0u, 0u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Bang,                "!",   false, false, true,  false, true,  false, 0u,  0u, 0u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::LessLess,            "<<",  true,  false, false, false, false, false, 8u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::GreaterGreater,      ">>",  true,  false, false, false, false, false, 8u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PipeEqual,           "|=",  true,  true,  false, true,  false, false, 0u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::AmpersandEqual,      "&=",  true,  true,  false, true,  false, false, 0u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::CaretEqual,          "^=",  true,  true,  false, true,  false, false, 0u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::LessLessEqual,       "<<=", true,  true,  false, false, false, false, 0u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::GreaterGreaterEqual, ">>=", true,  true,  false, false, false, false, 0u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::EqualEqual,          "==",  true,  false, true,  false, false, false, 6u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::BangEqual,           "!=",  true,  false, true,  false, false, false, 6u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Less,                "<",   true,  false, true,  false, false, false, 7u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::LessEqual,           "<=",  true,  false, true,  false, false, false, 7u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::Greater,             ">",   true,  false, true,  false, false, false, 7u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::GreaterEqual,        ">=",  true,  false, true,  false, false, false, 7u,  1u, 1u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::PlusPlus,            "++",  false, false, false, false, true,  true,  0u,  0u, 0u, false)
NWB_ADHD_EXPORT_OPERATOR_TOKEN(TokenType::MinusMinus,          "--",  false, false, false, false, true,  true,  0u,  0u, 0u, false)

NWB_ADHD_EXPORT_PROHIBITED_UNARY_OP(TokenType::Star,      "unary '*' dereference is prohibited in adhd")
NWB_ADHD_EXPORT_PROHIBITED_UNARY_OP(TokenType::Ampersand, "unary '&' address-of is prohibited in adhd")

NWB_ADHD_EXPORT_PROHIBITED_OPERATOR_OVERLOAD(TokenType::AmpersandAmpersand, "overloading 'operator&&' and 'operator||' is prohibited in adhd")
NWB_ADHD_EXPORT_PROHIBITED_OPERATOR_OVERLOAD(TokenType::PipePipe,           "overloading 'operator&&' and 'operator||' is prohibited in adhd")

NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Plus,           TokenType::PlusEqual,           " + ",  false, "",                  "")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Minus,          TokenType::MinusEqual,          " - ",  false, "",                  "")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Star,           TokenType::StarEqual,           " * ",  true,  "",                  "")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Slash,          TokenType::SlashEqual,          " / ",  true,  "",                  "")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Percent,        TokenType::PercentEqual,        " % ",  true,  "",                  "")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Ampersand,      TokenType::AmpersandEqual,      " & ",  false, "::VectorAndInt(",   "::VectorDoubleAndInt(")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Pipe,           TokenType::PipeEqual,           " | ",  false, "::VectorOrInt(",    "::VectorDoubleOrInt(")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::Caret,          TokenType::CaretEqual,          " ^ ",  false, "::VectorXorInt(",   "::VectorDoubleXorInt(")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::LessLess,       TokenType::LessLessEqual,       " << ", true,  "",                  "")
NWB_ADHD_EXPORT_SIMD_BINARY_OP(TokenType::GreaterGreater, TokenType::GreaterGreaterEqual, " >> ", true,  "",                  "")

NWB_ADHD_EXPORT_BRACKET_OPERATOR(TokenType::LeftBracket, TokenType::RightBracket, "[]", 1u, 1u, "expected ']' after '[' in 'operator[]'")

NWB_ADHD_EXPORT_MEMBER_OPERATOR_ARITY_ERROR(
    "unary 'operator*' (dereference) and unary 'operator&' (address-of) overloads are prohibited in adhd",
    "member 'operator~' cannot take parameters",
    "member 'operator+' and 'operator-' may take at most 1 parameter",
    "binary or indexing member operator overload must take exactly 1 parameter"
)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

