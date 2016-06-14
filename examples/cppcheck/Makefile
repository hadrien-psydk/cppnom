all: build/cppcheck

build/cppcheck: build main.cpp ../../src/cppnom.cpp ../../src/cppnom.h
	g++ main.cpp ../../src/cppnom.cpp -o build/cppcheck

build:
	mkdir -p build

run: build/cppcheck
	./build/cppcheck

clean:
	rm -rf build