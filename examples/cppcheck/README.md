# cppcheck

This program reads a C++ file, tokenizes it with cppnom, prints the tokens
in the terminal with differents colors, regenerates the C++ file and compare
the regenerated file with the original file.

If you are on Ubuntu, ensure you have the GNU C++ toolchain installed:
```
sudo apt install build-essential
```

Then open a terminal, cd in this directory and build&run the example:
```
make run
```
The example parses the Test.h file in the same directory. The regenerated
file is named test.h.rebuilt
