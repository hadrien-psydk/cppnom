#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <string>

#include "../../src/cppnom.h"

// Make the source compatible with old compiler but keep nullptr
#if __cplusplus < 201103L
#define nullptr 0
#endif

///////////////////////////////////////////////////////////////////////////////
void print_rgb(int r, int g, int b, bool ln, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	if( r >= 0 )
	{
		printf("\x1b[38;2;%d;%d;%dm", r, g, b);
	}
	vprintf(format, args);
	if( r >= 0 )
	{
		printf("\x1b[0m");
	}
	if( ln )
	{
		printf("\n");
	}
	va_end(args);
}

///////////////////////////////////////////////////////////////////////////////
void println(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	printf("\n");
	va_end(args);
}

///////////////////////////////////////////////////////////////////////////////
bool read_file(const std::string& file_path, std::string& str)
{
	FILE* pf = fopen(file_path.c_str(), "rb");
	if( !pf )
	{
		return false;
	}
	fseek(pf, 0, SEEK_END); // seek to end of file
	size_t size = ftell(pf); // get current file pointer
	fseek(pf, 0, SEEK_SET); // seek back to beginning of file
	if( size > 0x7fffffffu)
	{
		fclose(pf);
		return false;
	}
	str.resize(size);
	char* buf = &str[0];
	if( fread(buf, 1, size, pf) != size )
	{
		fclose(pf);
		return false;
	}
	fclose(pf);
	return true;
}

///////////////////////////////////////////////////////////////////////////////
// Helper class to rebuild the input C++ file from the cppnom tokens.
class RebuiltFile
{
public:
	RebuiltFile()
	{
		m_pf = nullptr;
		m_dos_style = false;
	}
	
	~RebuiltFile()
	{
		if( m_pf )
		{
			fclose(m_pf);
		}
	}
	
	bool create(const char* file_path, bool dos_style)
	{
		m_pf = fopen(file_path, "wb");
		m_dos_style = dos_style;
		return !!m_pf;
	}
	
	void newline()
	{
		
		if( m_dos_style )
		{
			fwrite("\r\n", 1, 2, m_pf);
		}
		else
		{
			fwrite("\n", 1, 1, m_pf);
		}
	}
	
	void write(const char* str, int len)
	{
		fwrite(str, 1, len, m_pf);
	}
	
	void write_utf8_bom()
	{
		static const unsigned char bom[] = { 0xef, 0xbb, 0xbf };
		fwrite(bom, 1, 3, m_pf);
	}

private:
	FILE* m_pf;
	bool m_dos_style;
};

struct Color { int r, g, b; };

///////////////////////////////////////////////////////////////////////////////
// Prints the token in the terminal by using different colors according to
// the token type.
// [in] tokens       cppnom parsed tokens
// [in] token_count  size of tokens array
// [in] debug_info   true to separate visually tokens and show 1:n mapping
//                   from C++ idiomatic tokens against cppnom tokens
void print_tokens(const cppnom::Token* tokens, int token_count, bool debug_info)
{
	using namespace cppnom;
	
	static const Color colors[] = {
		{  -1,   0,   0 }, // TT_None
		{  70,  70, 120 }, // TT_Space
		{  70, 120,   0 }, // TT_EmptyLine
		{  50, 255,  50 }, // TT_CommentLine
		{ 100, 200, 100 }, // TT_CommentBlock
		{  -1,   0,   0 }, // TT_Identifier
		{  10, 150, 255 }, // TT_Keyword
		{ 200, 100, 200 }, // TT_OperatorOrPunctuator
		{ 200, 230,   0 }, // TT_Macro
		{ 255, 255, 255 }, // TT_BackslashNewline
		{ 200,  90,  90 }, // TT_StringLiteral
		{ 200, 150,  90 }, // TT_CharacterLiteral
		{ 100, 100,  50 }, // TT_IntegerLiteral
	};
	
	int line = 1;
	for(int i = 0; i < token_count; ++i)
	{
		const Token& tok = tokens[i];
		for(; line <= tok.line; ++line)
		{
			if( line != 1 )
			{
				print_rgb(-1,-1,-1, false, "\n");
			}
			print_rgb(-1,-1,-1, false, "%3d: ", line);
		}
		
		int r = colors[tok.type].r;
		int g = colors[tok.type].g;
		int b = colors[tok.type].b;
		if( debug_info )
		{
			const char* mlStr = "";
			if( tok.multi == ML_First )
			{
				mlStr = "₁";
			}
			else if( tok.multi == ML_Next )
			{
				mlStr = "ₙ";
			}
			print_rgb(r,g,b, false, "«%.*s»%s", tok.len, tok.str, mlStr);
		}
		else
		{
			print_rgb(r,g,b, false, "%.*s", tok.len, tok.str);
		}
	}
	println("");
}

///////////////////////////////////////////////////////////////////////////////
// Rebuilds the C++ file from cppnom tokens.
std::string rebuild(const cppnom::Result& result, const char* ori_file_path)
{
	using namespace cppnom;
	
	std::string ret = ori_file_path;
	ret += ".rebuilt";
	
	RebuiltFile rf;
	if( !rf.create(ret.c_str(), result.dos_nl_count > result.unix_nl_count) )
	{
		println("file create failed");
		return ret;
	}
	
	if( result.has_utf8_bom )
	{
		rf.write_utf8_bom();
	}

	int line = 1;
	for(int i = 0; i < result.count; ++i)
	{
		const Token& tok = result.tokens[i];
		for(; line <= tok.line; ++line)
		{
			if( line != 1 )
			{
				rf.newline();
			}
		}
		rf.write(tok.str, tok.len);
	}
	return ret;
}

///////////////////////////////////////////////////////////////////////////////
bool get_line(const char* buf, int& len1, int& len2)
{
	if( buf[0] == 0 )
	{
		return true;
	}
	// Find line ori
	int i = 0;
	for(;;)
	{
		if( buf[i] == '\r' || buf[i] == '\n' || buf[i] == 0 )
		{
			break;
		}
		++i;
	}
	len1 = i;
	if( buf[i] == 0 )
	{
		len2 = len1;
	}
	else
	{
		if( buf[i] == '\r' && buf[i+1] == '\n' )
		{
			len2 = len1+2;
		}
		else
		{
			len2 = len1+1;
		}
	}
	return false;
}

enum CCRet
{
	CCR_Equal,       // Strictly the same
	CCR_MostlyEqual, // Only newlines are different
	CCR_Different    // Different contents
};

CCRet compare_contents(const std::string& content, const std::string& content2)
{
	CCRet ret = CCR_Equal;
	
	const char* ori_ptr = content.c_str();
	const char* reb_ptr = content2.c_str();

	bool ori_end = false;
	bool reb_end = false;
	
	int line_num = 0;
	for(;;)
	{
		line_num++;

		int ori_line_len1, ori_line_len2;
		ori_end = get_line(ori_ptr, ori_line_len1, ori_line_len2);
		
		int reb_line_len1, reb_line_len2;
		reb_end = get_line(reb_ptr, reb_line_len1, reb_line_len2);
		
		if( ori_end || reb_end )
		{
			break;
		}
		
		if( ori_line_len1 != reb_line_len1 || (memcmp(ori_ptr, reb_ptr, ori_line_len1) != 0) )
		{
			print_rgb(255,0,0, true, "mismatch at line %d", line_num);
			ret = CCR_Different;
			break;
		}
		if( ori_line_len2 != reb_line_len2 )
		{
			print_rgb(255,120,0, true, "unconsistent newline char at line %d", line_num);
			ret = CCR_MostlyEqual;
			// Continue
		}
		else
		{
			const size_t memcmp_size = ori_line_len2 - ori_line_len1;
			if( memcmp(ori_ptr+ori_line_len1, reb_ptr+reb_line_len1, memcmp_size) != 0 )
			{
				print_rgb(255,120,0, true, "unconsistent newline char at line %d", line_num);
				ret = CCR_MostlyEqual;
				// Continue
			}
		}
		ori_ptr += ori_line_len2;
		reb_ptr += reb_line_len2;
	}
	
	if( ori_end != reb_end )
	{
		print_rgb(255,100,0, true, "length mismatch");
		ret = CCR_Different;
	}
	return ret;
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv)
{
	bool print_tokens_enabled = true;
	const char* file_path = "Test.h";
	if( argc > 1 )
	{
		file_path = argv[1];
		print_tokens_enabled = false;
	}
	println("=========================== %s", file_path);
	
	std::string content;
	if( !read_file(file_path, content) )
	{
		print_rgb(255,0,0, true, "Cannot load file %s", file_path);
		return 1;
	}
	
	cppnom::Result result;
	if( !cppnom::tokenize(content.c_str(), content.length(), 0, result) )
	{
		print_rgb(255,0,0, true, "[error]");
		print_rgb(255,0,0, true, "%s", file_path);
		print_rgb(255,0,0, true, "line %d", result.error_line);
		print_rgb(255,0,0, false, "%s", result.error);
		cppnom::free_result(result);
		return 1;
	}

	if( print_tokens_enabled )
	{
		print_tokens(result.tokens, result.count, false);
	}
	
	std::string new_file_path = rebuild(result, file_path);
	cppnom::free_result(result);

	std::string content2;
	if( !read_file(new_file_path, content2) )
	{
		print_rgb(255,0,0, true, "Cannot load file rebuilt");
		return 1;
	}
	CCRet ccr = compare_contents(content, content2);
	if( ccr == CCR_Different )
	{
		print_rgb(255,0,0, true, "Bad rebuild of %s", file_path);
		return 1;
	}
	else if( ccr == CCR_MostlyEqual )
	{
		print_rgb(127,255,0, true, "[~ok]");
		return 1;
	}
	print_rgb(0,255,0, true, "[ok]");
	return 0;
}
