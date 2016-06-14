# cppnom
A C++ tokenizer that retains all input information (comments, blank spaces, preprocessor directives) in order to perform treatment at the source code level, for example for syntax highlighting or to verify coding style. After parsing, the original source file can be rebuilt from the tokens. The only loss of information concerns files with mixed newline character styles.

Does not use C++ exceptions. Does not use the C++ Standard Library.

Usage:
```
const char cpp[] = "int my_function(float* val); // do something";

cppnom::Result result;
if( !tokenize(cpp, strlen(cpp), 0, result) )
{
	printf("tokenize failed at line %d: %s\n", result.error_line, result.error);
	return;
}
for(int i = 0; i < result.count; ++i)
{
	// Do something with the tokens
}
cppnom::free_result(result);

```
See the examples folder for a complete example. See also the main header (cppnom.h) for more information.