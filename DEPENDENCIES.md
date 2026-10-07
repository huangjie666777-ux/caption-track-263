# Build dependencies

C++17, g++11.4.0 and GNU Make4.3 are directly available in WSL.
HarfBuzz2.7.4, FriBidi1.0.8, FreeType2.11.1, libpng1.6.37 and zlib1.2.11
headers and Linux x86_64 shared libraries are vendored under third_party.
Existing library code and build targets are described in README.md.

Compiler flags for these libraries:
`-Ithird_party/include/harfbuzz -Ithird_party/include/fribidi -Ithird_party/include/freetype2 -Ithird_party/include/libpng16 -Ithird_party/include`
Linker flags:
`-Lthird_party/lib -lharfbuzz -lfribidi -lfreetype -lpng16 -lz`
For executables under bin or build, use this relative runtime search path:
`-Wl,-rpath,'$ORIGIN/../third_party/lib'`
No PATH or LD_LIBRARY_PATH setup is needed with the correct runtime path.

The supplied font is fonts/DejaVuSans.ttf, with its original license.
third_party/dependencies.json records exact package revisions, official
download URLs, checksums and OS transitive dependencies. Corresponding
FriBidi source and Ubuntu patches remain in third_party/source. Notices
are in third_party/licenses. FreeType is redistributed under its FTL
option. PNG and zlib notices are retained. These files are dependency
resources; the existing Makefile has not been extended for new features.
