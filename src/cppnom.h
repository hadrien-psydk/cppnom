/*
The MIT License (MIT)

Copyright (c) 2016 Hadrien Nilsson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef CPPNOM_H
#define CPPNOM_H

// cppnom is a C++ tokenizer that keeps both preprocessor and C++ symbols in
// order to perform treatment at the source code level, for example for
// syntax highlighting or to verify coding style. After parsing, the original
// source file can be rebuilt from the tokens. The only loss of information
// concerns files with mixed newline character styles.
// Does not use C++ exceptions. Does not use the C++ Standard Library.
// https://github.com/hadrien-psydk/cppnom

#define CPPNOM_VERSION 1

namespace cppnom {

enum TokenType
{
	TT_None,                 // For internal management only
	TT_Space,                // May be empty if preceded by a backslash-newline
	TT_EmptyLine,            // Not a single character in the line
	TT_CommentLine,          // Single line comment
	TT_CommentBlock,         // C style block comment
	TT_Identifier,           // Identifier that is not a keyword
	TT_Keyword,              // C++ keyword
	TT_OperatorOrPunctuator, // Symbol-based only (thus no new and delete)
	TT_Macro,                // The most probable cause of multiline token
	TT_BackslashNewline,     // Backslash at the end of a line is tokenized
	TT_StringLiteral,        // "some string"
	TT_CharacterLiteral,     // 'c'
	TT_IntegerLiteral        // 123ULL
};

// A C++ token may be split into several cppnom tokens.
enum Multi
{
	ML_Single, // The C++ token is represented by a single cppnom token
	ML_First,  // The C++ token is split, this is the first part
	ML_Next    // The C++ token is split, this is one of the next parts
};

// A cppnom token. One or many represents one C++ token.
// Situation examples that lead to many cppnom tokens for one C++ token:
// - multiple lines /**/ comment;
// - backslash-newline in the middle of a C++ token;
// - comment within a macro - note: a comment at the end of a macro is
//   considered as being part of the macro but will get its own cppnom token;
// - newline inside a string literal.
struct Token
{
	TokenType     type;   // See enum declaration
	int           line;   // Line number, starting at 1
	const char*   str;    // Points on the buffer provided in tokenize()
	int           len;    // Length of the token, as 'str' does not end with 0
	Multi         multi;  // 1:1 or n:1 mapping between cppnom and C++ tokens
};

// tokenize() output structure
struct Result
{
	const Token* tokens;        // Non-null when tokenize() is successful
	int          count;         // Number of tokens
	const char*  error;         // Non-null when tokenize() fails. 0 terminated.
	int          error_line;    // Line number when the error was detected
	int          unix_nl_count; // Number of lines ending with U+000A
	int          dos_nl_count;  // Number of lines ending with U+000D,U+000A
	int          mac_nl_count;  // Number of lines ending with U+000D
	bool         has_utf8_bom;  // true if the UTF-8 BOM sequence is found
};

// Tokenizes a C++ file.
//
// Precondition:
// - the C++ code must be valid;
// - macro usage must be parsable without being expanded.
//
// [in]  content     The C++ file to tokenize.
// [in]  len         Size in bytes of the C++ file.
// [in]  options     Use 0 (reserved for future use).
// [out] result      Structure filled with the tokenization result.
//                   Must always be freed with free_result() after usage.
// [ret] true upon success. When false, result.tokens contains as much
//       as tokens that could be created, 'error' and 'errorLine' contain
//       error information.

bool tokenize(const char* content, int len, int options, Result& result);
void free_result(Result&);

}

#endif
