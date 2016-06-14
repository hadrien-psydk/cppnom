
#aa\
\
cc
/**/
////////
//comment
/* another
// one
here **/
/**
 bks **/ \
/* stuck*/\

#ifndef TEST_H
#define TEST_H

#define POWER_MACRO(x) if( (!x) ) { \
	do_something(x); \
	and_then(x)

namespace hop {\

class Test
{
public:
	Test();
	~Test();


	std::string getName() const { return m_name; }
	bool setName(const std::string& name = "default");
	
	void misc()
	{
		size_t x = z / a;
		wchar_t p[] = L"onk";
		int L = 0;
		int Lx = 0x02;
		int R = L&4;
		int o = 022;
		int vals = { 1u, 1l, 1ul, 1ll, 1ull };
		int VALS = { 1U, 1L, 1UL, 1LL, 1ULL };
		int z = 0Ull;
		bool bs = { true, false };
		void* p = nullptr;
		
		char c = 'x';
		wchar_t cw = L'x';
		char cm0[] = { '\'', '\"', '\?', '\\' };
		char cm1[] = { '\a', '\b', '\f' };
		char cm2[] = { '\n', '\r', '\t', '\v' };
		char cm3[] = { '\x3F', '\033' };

		char sz[] = "\'\"\?\\\a\b\f\n\r\t\vABC\xFF\033ZZ";
		char cm3[] = { '\x3F', '\033' };
	
		const char* s1 = ""
		  "aa"
		  "bb";
		const char* s2 = "\
		  abc";
		        
		// compiler extensions for backslash
		if (ch < '\2' || ch > '\4') {}
		const char* other = "\123";
		char* pc = new char[42];
		  
	}

private:
	std::string m_name;
};

}

#endif // TEST_H

// end