/*
MIT License

Copyright (c) 2016 Hadrien Nilsson - psydk.org

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

#include "cppnom.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

// A better version is possible but it will be enough here
#define ARRAY_COUNT(x) (int(sizeof(x) / sizeof(x[0])))

// Make the source compatible with old compiler but keep nullptr
#if __cplusplus < 201103L
#define nullptr 0
#endif

namespace cppnom {

// Hide all symbols except public interface
// Side note: the implementation uses camelCase while the public
// interface uses snake_case for variables and functions.
namespace {

///////////////////////////////////////////////////////////////////////////////
// A stack like allocator that does not free() for maximum performances.
class TokenAllocator
{
public:
	TokenAllocator()
	{
		m_pool = nullptr;
		m_size = 0;
		m_index = 0;
	}
	
	// [in] initialPoolSize    In number of tokens
	bool init(int initialPoolSize)
	{
		size_t byteSize = sizeof(Token) * initialPoolSize;
		m_pool = reinterpret_cast<Token*>(malloc(byteSize));
		if( !m_pool )
		{
			return false;
		}
		m_size = initialPoolSize;
		m_index = 0;
		return true;
	}
	
	// Allocates one token. If the pool is exhausted, more memory is
	// allocated from the heap.
	Token* alloc()
	{
		if( m_pool )
		{
			if( m_index < m_size )
			{
				return m_pool + m_index++;
			}
			size_t byteSize = sizeof(Token) * m_size * 2;
			Token* pool2 = reinterpret_cast<Token*>(realloc(m_pool, byteSize));
			if( !pool2 )
			{
				return nullptr;
			}
			m_pool = pool2;
			m_size *= 2;
			return m_pool + m_index++;
		}
		return nullptr;
	}
	
	// Gives ownership of the pool. It is used to give the token array
	// to the outside world through the public interface of cppnom
	void detach(const Token*& tokens, int& count)
	{
		tokens = m_pool;
		count = m_index;
		m_pool = nullptr;
		m_index = 0;
	}
	
	// Tokens are allocated in the order of the parsing. Sometimes we
	// want to go back to check what was the kind of token previously
	// allocated.
	Token* getPrev(Token* pTok) const
	{
		if( !pTok )
		{
			pTok = m_pool + m_index;
		}
		pTok -= 1;
		if( pTok < m_pool )
		{
			return nullptr;
		}
		return pTok;
	}

private:
	Token* m_pool;
	int    m_size;
	int    m_index;
};

///////////////////////////////////////////////////////////////////////////////
// To give a nice error description.
class Error
{
public:
	Error()
	{
		int size = 512; // Default size
		m_buf = reinterpret_cast<char*>(malloc(size));
		if( !m_buf )
		{
			m_len = 0;
			m_maxLen = 0;
			return;
		}
		m_len = 0;
		m_maxLen = size-1;
		m_buf[m_len] = 0;
	}
	
	Error& operator+=(const char* arg)
	{
		append(arg, strlen(arg));
		return *this;
	}
	
	void append(const char* arg, int argLen)
	{
		if( m_len + argLen > m_maxLen )
		{
			int newSize = (m_maxLen+1) * 2;
			char* buf2 = reinterpret_cast<char*>(realloc(m_buf, newSize));
			if( !buf2 )
			{
				// The user will get a truncated string
				return;
			}
		}
		memcpy(m_buf+m_len, arg, argLen);
		m_len += argLen;
		m_buf[m_len] = 0;
	}
	
	void detach(const char*& err)
	{
		err = m_buf;
		m_buf = nullptr;
		m_len = 0;
		m_maxLen = 0;
	}

private:
	char* m_buf;
	int   m_len;
	int   m_maxLen;
};

///////////////////////////////////////////////////////////////////////////////
bool isKeyword(const char* p, int len)
{
	static const char* const kws[] = {
		"alignof"     , "asm"             , "auto"      , "bool"         ,
		"break"       , "case"            , "catch"     , "char"         ,
		"char16_t"    , "char32_t"        , "class"     , "const"        ,
		"constexpr"   , "const_cast"      , "continue"  , "decltype"     ,
		"default"     , "delete"          , "do"        , "double"       ,
		"dynamic_cast", "else"            , "enum"      , "explicit"     ,
		"export"      , "extern"          , "false"     , "float"        ,
		"for"         , "friend"          , "goto"      , "if"           ,
		"inline"      , "int"             , "long"      , "mutable"      ,
		"namespace"   , "new"             , "noexcept"  , "nullptr"      ,
		"operator"    , "private"         , "protected" , "public"       ,
		"register"    , "reinterpret_cast", "return"    , "short"        ,
		"signed"      , "sizeof"          , "static"    , "static_assert",
		"static_cast" , "struct"          , "switch"    , "template"     ,
		"this"        , "thread_local"    , "throw"     , "true"         ,
		"try"         , "typedef"         , "typeid"    , "typename"     ,
	    "union"       , "unsigned"        , "using"     , "virtual"      ,
		"void"        , "volatile"        , "wchar_t"   , "while"
	};
	for(int i = 0; i < ARRAY_COUNT(kws); ++i )
	{
		int len2 = strlen(kws[i]);
		if( len2 == len )
		{
			if( memcmp(kws[i], p, len2) == 0 )
			{
				return true;
			}
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////
void trimTokenRight(const char* tokStr, int& tokLen)
{
	while(tokLen > 0 )
	{
		if( tokStr[tokLen-1] == '\n' || tokStr[tokLen-1] == '\r' )
		{
			tokLen--;
		}
		else
		{
			break;
		}
	}
}

void trimTokenLeft(const char*& tokStr, int& tokLen)
{
	while(tokLen > 0 )
	{
		if( tokStr[0] == '\n' || tokStr[0] == '\r' )
		{
			tokStr++;
			tokLen--;
		}
		else
		{
			break;
		}
	}
}

bool isPrintableChar(char c)
{
	return (32 <= c && c <= 126);
}

///////////////////////////////////////////////////////////////////////////////
enum State
{
	State_Idle,
	State_Space,
	State_Identifier,
	State_IdentifierOrLiteral,
	State_Macro,
	State_CommentOrOperator,
	State_CommentLine,
	State_CommentBlock,
	State_CommentBlockEnd,
	State_OperatorOrPunctuator,
	State_StringLiteral,
	State_StringLiteralEsc,
	State_CharacterLiteral,
	State_CharacterLiteralEsc,
	State_OctOrHexLiteral,
	State_DecLiteral,
	State_OctLiteral,
	State_HexLiteralX,
	State_HexLiteral,
	State_IntegerSuffix,
	State_IntegerSuffix2,
	State_NewLine,
	State_Error = 100
};

const char* StringFromState(State state)
{
	switch(state)
	{
	case State_Idle:                return "idle";
	case State_Space:               return "space";
	case State_Identifier:          return "identifier";
	case State_IdentifierOrLiteral: return "identifier or literal";
	case State_Macro:               return "macro";
	case State_CommentOrOperator:   return "comment or /";
	case State_CommentLine:         return "comment line";
	case State_CommentBlock:        return "comment block";
	case State_CommentBlockEnd:     return "comment block end";
	case State_OperatorOrPunctuator:return "operator or punctuator";
	case State_StringLiteral:       return "string literal";
	case State_StringLiteralEsc:    return "string literal escape sequence";
	case State_CharacterLiteral:    return "character literal";
	case State_CharacterLiteralEsc: return "character literal escape sequence";
	case State_OctOrHexLiteral:     return "octal or hexadecimal literal";
	case State_DecLiteral:          return "decimal literal";
	case State_OctLiteral:          return "octal literal";
	case State_HexLiteralX:         return "hexadecimal literal x";
	case State_HexLiteral:          return "hexadecimal literal";
	case State_IntegerSuffix:       return "integer suffix";
	case State_IntegerSuffix2:      return "integer suffix 2";
	case State_NewLine:             return "new line";
	case State_Error:               return "error";
	default: break;
	}
	return "";
};

///////////////////////////////////////////////////////////////////////////////
struct Context
{
	char c;
	char prevC;

	TokenAllocator m_tokens;
	Error          m_error;

	// Statistics
	int m_unixNlCount;
	int m_dosNlCount;
	int m_macNlCount;
	
	bool m_hasUtf8Bom;

	Context(const char* content, int len)
	{
		c = 0;
		prevC = 0;
		m_tokenStartAt = -1;

		m_p = content;
		m_len = len;
		
		// Skip utf-8 bom
		if( m_len >= 3
		  && (m_p[0] & 0x00ff) == 0xef
		  && (m_p[1] & 0x00ff) == 0xbb
		  && (m_p[2] & 0x00ff) == 0xbf )
		{
			m_len -= 3;
			m_p += 3;
			m_hasUtf8Bom = true;
		}
		else
		{
			m_hasUtf8Bom = false;
		}

		m_index = -1;
		m_tokenStartAt = 0;
		m_tokenLineNum = 0;
		m_lineStartAt = 0;
		m_lineCounter = 1;
		m_multi = ML_Single;

		m_unixNlCount = 0;
		m_dosNlCount = 0;
		m_macNlCount = 0;

		m_state = State_NewLine;
		m_tokenStartAt = 0;
		m_tokenLineNum = 1;
		m_insideMacro = false;
		m_errorLine = 0;
	}

	// Fetch next char
	// Here we also do the work of the preprocessor regarding backslash-newlines
	bool next(State& state)
	{
		m_index++;
		prevC = c;
		
		// Loop until we found a satisfying character
		for(;;)
		{
			if( m_index >= m_len )
			{
				if( m_index > m_len )
				{
					return false;
				}
				// Use 0 to push the last token
				c = 0;
				break;
			}
			
			if( !nextLegitChar() )
			{
				break;
			}
		}
		
		state = m_state;
		return true;
	}

	// Declares the context as error and fills internal information about the error
	void error()
	{
		int lineNum = m_lineCounter;
		if( c == '\n' )
		{
			m_lineCounter--;
		}
		m_errorLine = lineNum;
		
		m_error += "state: ";
		m_error += StringFromState(m_state);
		m_error += "\n";

		char ci = c;
		if( !isPrintableChar(ci) )
		{
			ci = '?';
		}
		char buf[200];
		sprintf(buf, "char: '%c' u+%04x\n", ci, c & 0x000000ff);
		m_error += buf;

		// Display current line
		const char* pLine = m_p + m_lineStartAt;
		int i = m_index;
		for(; i < m_len; ++i)
		{
			if( m_p[i] == '\n' )
			{
				break;
			}
		}
		int lineLen = i - m_lineStartAt;
		m_error.append(pLine, lineLen);
		m_error += "\n";
		
		int infoLen = (m_index - m_lineStartAt)+1;
		char* pInfo = (char*)malloc(infoLen + 1);
		for(int i = 0; i < infoLen-1; ++i)
		{
			if( pLine[i] == '\t' )
			{
				pInfo[i] = '\t';
			}
			else
			{
				pInfo[i] = '~';
			}
		}
		pInfo[infoLen-1] = '^';
		pInfo[infoLen] = 0;
		m_error += pInfo;
		m_error += "\n";
		free(pInfo);
	}

	void newState(State newState)
	{
		if( m_state == State_Idle || m_state == State_NewLine )
		{
			newToken();
		}
		if( newState == State_Macro )
		{
			m_insideMacro = true;
		}
		m_state = newState;
	}

	void convertTokenMacro(TokenType& type)
	{
		if( m_insideMacro )
		{
			// Macros are hard to deal with.
			// Do not parse macro except comments.
			// Divide symbol is ambiguous and may lead to the identification
			// of a symbol inside the macro. Cancel that.
			if( !(type == TT_CommentLine || type == TT_CommentBlock) )
			{
				if( type == TT_Macro )
				{
					// End of macro
					m_insideMacro = false;
				}
				else
				{
					// Other type of token, make it a dependent macro token
					type = TT_Macro;
				}
			}
		}
	}
	
	// Called by the parsing function to notify the end of one C++ token
	// [ret] State to do immediately
	State pushToken(TokenType type, bool wantsCurrentChar = false)
	{
		convertTokenMacro(type);

		// Update types in case previous tokens are linked with the one we are
		// going to push after parsing
		if( m_multi != ML_Single )
		{
			fixPrevTokenTypes(type);
		}
		
		pushTokenNoStateChange(type, wantsCurrentChar);
		m_tokenStartAt = -1;

		if( !m_insideMacro )
		{
			m_state = State_Idle;
			m_multi = ML_Single;
			fixMacroTokenMulti();
		}
		else
		{
			if( type == TT_CommentBlock || type == TT_Macro )
			{
				// Continue macro parsing
				m_state = State_Macro;
				m_multi = ML_Next;
				m_tokenStartAt = m_index;
				m_tokenLineNum = m_lineCounter;
				
				// The current char was consumed, next token begins after
				if( wantsCurrentChar )
				{
					m_tokenStartAt++;
				}
			}
			else if( type == TT_CommentLine )
			{
				m_insideMacro = false;
				m_state = State_Idle;
				m_multi = ML_Single;
				fixMacroTokenMulti();
			}
			else
			{
				m_state = State_Idle;
			}
		}
		return m_state;
	}

	// Initialy done for /* */ comments on multiple lines.
	// If the token type is none it will be set when the final token is pushed
	void pushTokenMultiline(TokenType type = TT_None)
	{
		// Push current token though it is uncomplete
		if( m_multi == ML_Single )
		{
			m_multi = ML_First;
		}
		else if( m_multi == ML_First )
		{
			m_multi = ML_Next;
		}
		pushTokenNoStateChange(type, false);

		// continue parsing the current C++ token
		newToken();

		// New tokens will be linked
		if( m_multi == ML_First )
		{
			m_multi = ML_Next;
		}
	}
	
	// For operators and punctuators
	const char* getTokenStr() const
	{
		if( m_tokenStartAt < 0 )
		{
			return nullptr;
		}
		return m_p + m_tokenStartAt;
	}

	// For operators and punctuators
	int getTokenLen() const
	{
		if( m_tokenStartAt < 0 )
		{
			return 0;
		}
		return (m_index - m_tokenStartAt) + 1;
	}
	
	int getErrorLine() const
	{
		return m_errorLine;
	}

private:
	State m_state;

	const char* m_p;         // File content buffer
	int   m_len;             // File length
	int   m_index;           // Position of the current char
	
	int   m_tokenStartAt;    // Position of the first char of the token
	int   m_tokenLineNum;    // Line index when the token was started
	int   m_lineStartAt;     // Position of the current line, for error reporting
	int   m_lineCounter;     // Current line number, 1-based
	Multi m_multi;           // C++ token split?
	bool  m_insideMacro;     // Macro contains tokens too
	int   m_errorLine;       // Filled on error()

private:
	// Try to merge the new token with the previous token if they are both
	// of macro type
	// [ret] true if merge done
	bool tryToMergeMacro(int tokLen)
	{
		Token* pPrev = m_tokens.getPrev(nullptr);
		if( !(pPrev && pPrev->type == TT_Macro) )
		{
			return false;
		}

		if( (pPrev->multi == ML_First || pPrev->multi == ML_Next)
		        && m_multi == ML_Next )
		{
			pPrev->len += tokLen;
			return true;
		}
		return false;
	}
	
	// Maybe after merging we do not need a multi token anymore
	// Call this function when the C++ macro ends
	void fixMacroTokenMulti()
	{
		Token* pPrev = m_tokens.getPrev(nullptr);
		if( !(pPrev && pPrev->type == TT_Macro) )
		{
			return;
		}

		if( pPrev->multi == ML_First )
		{
			// Indeed
			pPrev->multi = ML_Single;
		}
	}

	void fixPrevTokenTypes(TokenType type)
	{
		Token* pTok = nullptr;
		for(;;)
		{
			pTok = m_tokens.getPrev(pTok);
			if( !pTok )
			{
				break;
			}
			if( pTok->multi == ML_Single )
			{
				break;
			}
			if( pTok->type == TT_None )
			{
				pTok->type = type;
			}
		}
	}

	// Helper function for next()
	// [ret] true to continue looping
	bool nextLegitChar()
	{
		if( c == '\n' )
		{
			m_lineStartAt = m_index;
		}
		c = m_p[m_index];
		
		// We do not expose \r, only return \n to make things simplier
		if( c == '\n' )
		{
			m_lineCounter++;
			m_unixNlCount++;
		}
		else if( c == '\r' )
		{
			if( m_p[m_index+1] == '\n' )
			{
				// Yes, DOS newline
				m_index++;
				
				c = '\n';
				m_lineCounter++;
				m_dosNlCount++;
			}
			else
			{
				// Old Mac style newline
				c = '\n';
				m_lineCounter++;
				m_macNlCount++;
			}
		}
		else if( c == '\\' )
		{
			// We do not expose backslash+newline neither
			char nextC = m_p[m_index+1];
			if( nextC == '\r' || nextC == '\n' )
			{
				// If the statement is split across multiple lines, it means
				// we have several tokens for one statement.
				// The first token will have its linked member set to false,
				// and the following ones will have their linked member set to true.
				
				// Push current token though it is uncomplete
				// If the state is Idle, it means no token parsing was occurring
				if( m_state != State_Idle )
				{
					pushTokenMultiline();
				}
				else
				{
					m_state = State_NewLine;
				}
				
				// Push a new token for the backslash
				Token* pTok = m_tokens.alloc();
				pTok->type = TT_BackslashNewline;
				pTok->line = m_lineCounter;
				pTok->str = m_p+m_index;
				pTok->len = 1;
				pTok->multi = m_multi;
				
				char nextNextC = m_p[m_index+2];
				if( nextC == '\r' )
				{
					if( nextNextC == '\n' )
					{
						// Skip 3 characters
						m_dosNlCount++;
						m_index += 3;
					}
					else
					{
						// Skip 2 only
						m_macNlCount++;
						m_index += 2;
					}
				}
				else
				{
					// Skip just 2
					m_unixNlCount++;
					m_index += 2;
				}
				
				m_lineStartAt = m_index;
				m_lineCounter++;
	
				// continue parsing
				newToken();
				return true;
			}
		}
		return false;
	}
	
	void newToken()
	{
		m_tokenStartAt = m_index;
		m_tokenLineNum = m_lineCounter;
	}

	// Push current token without changing m_state
	void pushTokenNoStateChange(TokenType type, bool wantsCurrentChar)
	{
		assert(m_tokenStartAt >= 0);
		const char* tokStr = m_p + m_tokenStartAt;
		int tokLen = (m_index - m_tokenStartAt) + 1;
		
		// Include current character for tokens that have an end tag
		// TT_CommentBlock
		// TT_StringLiteral
		// TT_CharacterLiteral
		// Unambigous TT_Operator
		// Unambigous TT_Integer

		if( !wantsCurrentChar )
		{
			tokLen--;
		};
		
		trimTokenRight(tokStr, tokLen);
		trimTokenLeft(tokStr, tokLen);
		
		if( type == TT_Macro )
		{
			if( tokLen == 0 )
			{
				// In case of comment inside a macro and a macro parsing
				// restart, an empty token may be generated.
				// We do not want to store that empty token.
				return;
			}
			if( tryToMergeMacro(tokLen) )
			{
				return;
			}
		}
		
		if( type == TT_Identifier && isKeyword(tokStr, tokLen) )
		{
			type = TT_Keyword;
		}

		Token* pTok = m_tokens.alloc();
		pTok->type = type;
		pTok->str = tokStr;
		pTok->len = tokLen;
		pTok->line = m_tokenLineNum;
		pTok->multi = m_multi;
	}
};

///////////////////////////////////////////////////////////////////////////////
bool isDigit(char c)
{
	return ('0' <= c && c <= '9');
}

bool isOctDigit(char c)
{
	return ('0' <= c && c <= '7');
}

bool isHexDigit(char c)
{
	return isDigit(c) || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F');
}

bool isIdentifierCharNonDigit(char c)
{
	return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || (c == '_');
}

bool isIdentifierChar(char c)
{
	return isIdentifierCharNonDigit(c) || isDigit(c);
}

// Check if a string is exactly or only partially equal to another
enum SERet { SER_NotEqual, SER_Maybe, SER_Equal };

inline SERet strEquals(const char* psz, const char* pRight, int rightLen)
{
	for(int j = 0; j < rightLen; ++j)
	{
		if( psz[j] != pRight[j])
		{
			return SER_NotEqual;
		}
	}
	if( psz[rightLen] != 0 )
	{
		return SER_Maybe;
	}
	return SER_Equal;
}

// Checks if a string appears in an array of strings.
// SER_Equal:    If the string appears exactly once
// SER_Maybe:    The string appears several times
// SER_NotEqual: The string cannot be one of the strings in the array
SERet isOneOfThose(const char* pCandidate, int candidateLen,
                   const char*const* those, int count)
{
	int maybeCount = 0;
	int equalCount = 0;
	for(int i = 0; i < count; ++i)
	{
		SERet eq = strEquals(those[i], pCandidate, candidateLen);
		if( eq == SER_Maybe )
		{
			maybeCount++;
		}
		else if( eq == SER_Equal )
		{
			equalCount++;
		}
	}
	if( maybeCount == 0 && equalCount == 0 )
	{
		return SER_NotEqual;
	}
	if( maybeCount > 0 || equalCount > 1 )
	{
		return SER_Maybe; // Multiple possibilities
	}
	return SER_Equal;
}

static const char* const k_ops[] = {
	"{", "}", "[", "]", "#", "##", "(", ")",
	"<:", ":>", "<%", "%>", "%:", "%:%:", ";", ":", "...",
	"?", "::", ".", ".*",
	"+", "-", "*", "/", "%", "^", "&", "|", "~",
	"!", "=", "<", ">", "+=", "-=", "*=", "/=", "%=",
	"ˆ=", "&=", "|=", "<<", ">>", ">>=", "<<=", "==", "!=",
	"<=", ">=", "&&", "||", "++", "--", ",", "->*", "->"
};

SERet checkOperatorOrPunctuator(const char* pCandidate, int candidateLen)
{
	return isOneOfThose(pCandidate, candidateLen, k_ops, ARRAY_COUNT(k_ops));
}

///////////////////////////////////////////////////////////////////////////////
bool doState_NoSpace(Context& ctx)
{
	char c = ctx.c;
	if( c == 'L' || c == 'u' || c == 'U' )
	{
		ctx.newState(State_IdentifierOrLiteral);
	}
	else if( isIdentifierCharNonDigit(c) )
	{
		ctx.newState(State_Identifier);
	}
	else if( c == '"' )
	{
		ctx.newState(State_StringLiteral);
	}
	else if( c == '#' )
	{
		ctx.newState(State_Macro);
	}
	else if( c == '/' )
	{
		ctx.newState(State_CommentOrOperator);
	}
	else if( c == '\'' )
	{
		ctx.newState(State_CharacterLiteral);
	}
	else if( c == '0' )
	{
		ctx.newState(State_OctOrHexLiteral);
	}
	else if( isDigit(c) )
	{
		ctx.newState(State_DecLiteral);
	}
	else if( c == '\n' )
	{
		ctx.newState(State_NewLine);
	}
	else if( c == 0 )
	{
		// End of file
	}
	else
	{
		// Special case for operators because they can be one char long only
		SERet ser = checkOperatorOrPunctuator(&c, 1);
		if( ser == SER_Maybe )
		{
			ctx.newState(State_OperatorOrPunctuator);
		}
		else if( ser == SER_Equal )
		{
			ctx.newState(State_OperatorOrPunctuator);
			ctx.pushToken(TT_OperatorOrPunctuator, true);
		}
		else
		{
			ctx.error();
			return false;
		}
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_Idle(Context& ctx)
{
	char c = ctx.c;
	if( c == ' ' || c == '\t' || c == '\f' )
	{
		ctx.newState(State_Space);
	}
	else
	{
		// A new token begins
		return doState_NoSpace(ctx);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_Space(Context& ctx)
{
	char c = ctx.c;
	if( c == ' ' || c == '\t' || c == '\f' )
	{
		// Continue
	}
	else
	{
		// Push token
		ctx.pushToken(TT_Space);
		return doState_NoSpace(ctx);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_NewLine(Context& ctx)
{
	char c = ctx.c;
	if( c == '\n' || c == 0 )
	{
		ctx.pushToken(TT_EmptyLine);
	}
	return doState_Idle(ctx);
}

///////////////////////////////////////////////////////////////////////////////
bool doState_Identifier(Context& ctx)
{
	char c = ctx.c;
	if( isIdentifierChar(c) )
	{
		// Continue
	}
	else
	{
		ctx.pushToken(TT_Identifier);
		return doState_Idle(ctx);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_IdentifierOrLiteral(Context& ctx)
{
	char c = ctx.c;
	if( c == '"' )
	{
		// Actually a string literal
		ctx.newState(State_StringLiteral);
		return true;
	}
	else if( c == '\'' )
	{
		// Actually a character literal
		ctx.newState(State_CharacterLiteral);
		return true;
	}
	else
	{
		// It was an identifier
		ctx.newState(State_Identifier);
		return doState_Identifier(ctx);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_Macro(Context& ctx)
{
	char c = ctx.c;
	if( c == '\n' || c == 0 )
	{
		ctx.pushToken(TT_Macro);
		return doState_Idle(ctx);
	}
	else
	{
		// Check comment block that can act like a backslah-newline
		if( c == '/' )
		{
			ctx.pushTokenMultiline(TT_Macro);
			ctx.newState(State_CommentOrOperator);
		}
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_CommentLine(Context& ctx)
{
	char c = ctx.c;
	if( c == '\n' || c == 0 )
	{
		ctx.pushToken(TT_CommentLine);
		return doState_Idle(ctx);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_CommentOrOperator(Context& ctx)
{
	char c = ctx.c;
	if( c == '/' )
	{
		// Actually a comment line
		ctx.newState(State_CommentLine);
	}
	else if( c == '*' )
	{
		// Actually a comment block
		ctx.newState(State_CommentBlock);
	}
	else
	{
		// It was actually an operator
		State doNow = ctx.pushToken(TT_OperatorOrPunctuator);
		if( doNow == State_Idle )
		{
			return doState_Idle(ctx);
		}
		else if( doNow == State_Macro )
		{
			return doState_Macro(ctx);
		}
		else
		{
			ctx.error();
		}
	}
	return true;
}


///////////////////////////////////////////////////////////////////////////////
bool doState_CommentBlock(Context& ctx)
{
	char c = ctx.c;
	if( c == '*' )
	{
		ctx.newState(State_CommentBlockEnd);
	}
	else if( c == '\n' )
	{
		ctx.pushTokenMultiline();
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_CommentBlockEnd(Context& ctx)
{
	char c = ctx.c;
	if( c == '*' )
	{
		// Continue
	}
	else if( c == '\n' )
	{
		ctx.pushTokenMultiline();
	}
	else if( c == '/' )
	{
		ctx.pushToken(TT_CommentBlock, true);
		// We can return immediately because this token has a end tag
		return true;
	}
	else
	{
		ctx.newState(State_CommentBlock); // Back to previous state
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_OperatorOrPunctuator(Context& ctx)
{
	SERet ser = checkOperatorOrPunctuator(ctx.getTokenStr(), ctx.getTokenLen());
	if( ser == SER_Maybe )
	{
		// Continue
	}
	else if( ser == SER_Equal )
	{
		// Done with end tag
		// We can return immediately because this token has a end tag
		ctx.pushToken(TT_OperatorOrPunctuator, true);
		return true;
	}
	else
	{
		// Operator end reached
		ctx.pushToken(TT_OperatorOrPunctuator);
		return doState_Idle(ctx);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool isOneOf(char c, const char* psz)
{
	for(;;)
	{
		char c2 = psz[0];
		if( c2 == 0 )
		{
			return false;
		}
		if( c == c2 )
		{
			return true;
		}
		psz++;
	}
}

bool isSimpleEscapeSequence(char c)
{
	return isOneOf(c, "'\"?\\abfnrtve"); // \e is GCC specific
}

///////////////////////////////////////////////////////////////////////////////
bool doState_StringLiteral(Context& ctx)
{
	char c = ctx.c;
	if( c == '\\' )
	{
		ctx.newState(State_StringLiteralEsc);
	}
	else if( c == '\n' )
	{
		ctx.pushTokenMultiline();
	}
	else if( c == '"' )
	{
		ctx.pushToken(TT_StringLiteral, true);
		// We can return immediately because this token has a end tag
		return true;
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_StringLiteralEsc(Context& ctx)
{
	char c = ctx.c;
	
	if( isSimpleEscapeSequence(c) )
	{
		ctx.newState(State_StringLiteral);
	}
	else if( c == 'x' )
	{
		ctx.newState(State_StringLiteral);
	}
	else if( isOctDigit(c) )
	{
		// The test should be c == '0' but some compilers accepts
		// expressions like \4
		ctx.newState(State_StringLiteral);
	}
	else if( c == '\n' )
	{
		// Not an escape sequence, but a backslash
		// Keep it in the string
		ctx.newState(State_StringLiteral);
	}
	else
	{
		ctx.error();
		return false;
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_CharacterLiteral(Context& ctx)
{
	char c = ctx.c;
	if( c == '\\' )
	{
		ctx.newState(State_CharacterLiteralEsc);
	}
	else if( c == '\n' )
	{
		ctx.error();
		return false;
	}
	else if( c == '\'' )
	{
		ctx.pushToken(TT_CharacterLiteral, true);
		// We can return immediately because this token has a end tag
		return true;
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_CharacterLiteralEsc(Context& ctx)
{
	char c = ctx.c;
	
	if( isSimpleEscapeSequence(c) )
	{
		ctx.newState(State_CharacterLiteral);
	}
	else if( c == 'x' )
	{
		ctx.newState(State_CharacterLiteral);
	}
	else if( isOctDigit(c) )
	{
		// The test should be c=='0' but some compilers accepts
		// expressions like \4
		ctx.newState(State_CharacterLiteral);
	}
	else
	{
		ctx.error();
		return false;
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
SERet checkIntegerSuffix(const char* p, int len)
{
	static const char* const suf[] = {
	    "l", "ll", "u", "ul", "ull",
	    "L", "LL", "U", "UL", "ULL",
	    "Ul", "Ull"
	};
	return isOneOfThose(p, len, suf, ARRAY_COUNT(suf));
}

bool isIntegerSuffixBegin(char c)
{
	return checkIntegerSuffix(&c, 1) >= SER_Maybe;
}

///////////////////////////////////////////////////////////////////////////////
bool doState_OctOrHexLiteral(Context& ctx)
{
	char c = ctx.c;
	if( c == 'x' )
	{
		ctx.newState(State_HexLiteralX);
	}
	else if( isOctDigit(c) )
	{
		ctx.newState(State_OctLiteral);
	}
	else if( isIntegerSuffixBegin(c) )
	{
		ctx.newState(State_IntegerSuffix);
	}
	else
	{
		// A decimal 0
		ctx.pushToken(TT_IntegerLiteral);
		return doState_Idle(ctx);
	}
	return true;
}

bool doState_HexLiteralX(Context& ctx)
{
	char c = ctx.c;
	if( isHexDigit(c) )
	{
		ctx.newState(State_HexLiteral);
	}
	else
	{
		ctx.error();
		return false;
	}
	return true;
}

bool doState_HexLiteral(Context& ctx)
{
	char c = ctx.c;
	if( isHexDigit(c) )
	{
		// Continue
	}
	else if( isIntegerSuffixBegin(c) )
	{
		ctx.newState(State_IntegerSuffix);
	}
	else
	{
		ctx.pushToken(TT_IntegerLiteral);
		return doState_Idle(ctx);
	}
	return true;
}

bool doState_OctLiteral(Context& ctx)
{
	char c = ctx.c;
	if( isOctDigit(c) )
	{
		// Continue
	}
	else if( isIntegerSuffixBegin(c) )
	{
		ctx.newState(State_IntegerSuffix);
	}
	else
	{
		ctx.pushToken(TT_IntegerLiteral);
		return doState_Idle(ctx);
	}
	return true;
}

bool doState_DecLiteral(Context& ctx)
{
	char c = ctx.c;
	if( isDigit(c) )
	{
		// Continue
	}
	else if( isIntegerSuffixBegin(c) )
	{
		ctx.newState(State_IntegerSuffix);
	}
	else
	{
		ctx.pushToken(TT_IntegerLiteral);
		return doState_Idle(ctx);
	}
	return true;
}

bool doState_IntegerSuffix(Context& ctx)
{
	// Handling a suffix is a bit tricky
	const int backLen = 2;
	const char* p = ctx.getTokenStr() + ctx.getTokenLen()-backLen;
	SERet ser = checkIntegerSuffix(p, backLen);
	if( ser == SER_Maybe )
	{
		ctx.newState(State_IntegerSuffix2);
	}
	else if( ser == SER_Equal )
	{
		// Done with end tag
		ctx.pushToken(TT_IntegerLiteral, true);
		return true;
	}
	else
	{
		ctx.pushToken(TT_IntegerLiteral);
		return doState_Idle(ctx);
	}
	return true;
}

bool doState_IntegerSuffix2(Context& ctx)
{
	// Handling a suffix is a bit tricky
	const int backLen = 3;
	const char* p = ctx.getTokenStr() + ctx.getTokenLen()-backLen;
	SERet ser = checkIntegerSuffix(p, backLen);
	if( ser == SER_Maybe )
	{
		ctx.newState(State_IntegerSuffix2);
	}
	else if( ser == SER_Equal )
	{
		// Done with end tag
		ctx.pushToken(TT_IntegerLiteral, true);
		return true;
	}
	else
	{
		ctx.pushToken(TT_IntegerLiteral);
		return doState_Idle(ctx);
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////
// Fils the Result error field
void errorToResult(Context& ctx, const char* err, Result& result)
{
	ctx.m_error += err;
	result.error_line = 0;
	result.tokens = nullptr;
	result.count = 0;
	ctx.m_error.detach(result.error);
}

///////////////////////////////////////////////////////////////////////////////
} // namespace

///////////////////////////////////////////////////////////////////////////////
bool tokenize(const char* content, int len, int options, Result& result)
{
	result.unix_nl_count = 0;
	result.dos_nl_count = 0;
	result.mac_nl_count = 0;

	Context ctx(content, len);
	
	if( !content )
	{
		errorToResult(ctx, "bad content address", result);
		return false;
	}
	if( len < 0 )
	{
		errorToResult(ctx, "bad content len", result);
		return false;
	}
	if( options != 0 )
	{
		errorToResult(ctx, "bad options", result);
		return false;
	}

	if( !ctx.m_tokens.init(200000) )
	{
		errorToResult(ctx, "token pool alloc failed", result);
		return false;
	}
	
	bool ret = true;

	State state;
	while( ctx.next(state) )
	{
		bool ok;
		switch( state )
		{
		case State_Idle:
			ok = doState_Idle(ctx);
			break;
		case State_Space:
			ok = doState_Space(ctx);
			break;
		case State_NewLine:
			ok = doState_NewLine(ctx);
			break;
		case State_Identifier:
			ok = doState_Identifier(ctx);
			break;
		case State_IdentifierOrLiteral:
			ok = doState_IdentifierOrLiteral(ctx);
			break;
		case State_Macro:
			ok = doState_Macro(ctx);
			break;
		case State_CommentOrOperator: 
			ok = doState_CommentOrOperator(ctx);
			break;
		case State_CommentLine:
			ok = doState_CommentLine(ctx);
			break;
		case State_CommentBlock:
			ok = doState_CommentBlock(ctx);
			break;
		case State_CommentBlockEnd:
			ok = doState_CommentBlockEnd(ctx);
			break;
		case State_OperatorOrPunctuator:
			ok = doState_OperatorOrPunctuator(ctx);
			break;
		case State_StringLiteral:
			ok = doState_StringLiteral(ctx);
			break;
		case State_StringLiteralEsc:
			ok = doState_StringLiteralEsc(ctx);
			break;
		case State_CharacterLiteral:
			ok = doState_CharacterLiteral(ctx);
			break;
		case State_CharacterLiteralEsc:
			ok = doState_CharacterLiteralEsc(ctx);
			break;
		case State_OctOrHexLiteral:
			ok = doState_OctOrHexLiteral(ctx);
			break;
		case State_DecLiteral:
			ok = doState_DecLiteral(ctx);
			break;
		case State_OctLiteral:
			ok = doState_OctLiteral(ctx);
			break;
		case State_HexLiteralX:
			ok = doState_HexLiteralX(ctx);
			break;
		case State_HexLiteral:
			ok = doState_HexLiteral(ctx);
			break;
		case State_IntegerSuffix:
			ok = doState_IntegerSuffix(ctx);
			break;
		case State_IntegerSuffix2:
			ok = doState_IntegerSuffix2(ctx);
			break;
		case State_Error:
			ok = false;
			break;
		}

		if( !ok )
		{
			ret = false;
			break;
		}
	}

	ctx.m_tokens.detach(result.tokens, result.count);
	ctx.m_error.detach(result.error);
	result.error_line = ctx.getErrorLine();
	result.unix_nl_count = ctx.m_unixNlCount;
	result.dos_nl_count = ctx.m_dosNlCount;
	result.mac_nl_count = ctx.m_macNlCount;
	result.has_utf8_bom = ctx.m_hasUtf8Bom;

	return ret;
}

///////////////////////////////////////////////////////////////////////////////
void free_result(Result& result)
{
	free(const_cast<Token*>(result.tokens));
	result.tokens = nullptr;
	result.count = 0;
	free(const_cast<char*>(result.error));
	result.error = nullptr;
	result.unix_nl_count = 0;
	result.dos_nl_count = 0;
	result.mac_nl_count = 0;
	result.has_utf8_bom = false;
}

///////////////////////////////////////////////////////////////////////////////
}
