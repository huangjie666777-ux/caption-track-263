CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iinclude -Ithird_party/include/harfbuzz -Ithird_party/include/fribidi -Ithird_party/include/freetype2 -Ithird_party/include/libpng16 -Ithird_party/include
LDFLAGS := -Lthird_party/lib -lharfbuzz -lfribidi -lfreetype -lpng16 -lz -Wl,-rpath,'$$ORIGIN/../third_party/lib'

LIB_OBJS := build/utf8.o build/segments.o build/engine.o build/selection.o build/raster.o build/png_save.o build/track.o

all: bin/demo bin/selftest

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/libcaption_track263.a: $(LIB_OBJS)
	ar rcs $@ $^

bin/demo: examples/demo.cpp build/libcaption_track263.a | bin
	$(CXX) $(CXXFLAGS) $< build/libcaption_track263.a -o $@ $(LDFLAGS)

bin/selftest: tests/selftest.cpp build/libcaption_track263.a | bin
	$(CXX) $(CXXFLAGS) $< build/libcaption_track263.a -o $@ $(LDFLAGS)

build bin:
	mkdir -p $@

test: bin/selftest
	./bin/selftest

clean:
	rm -rf build bin

.PHONY: all test clean
