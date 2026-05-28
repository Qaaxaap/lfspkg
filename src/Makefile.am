bin_PROGRAMS = lfspkg

lfspkg_SOURCES = \
    main.cpp \
    cli.cpp \
    db.cpp \
    package.cpp \
    util.cpp \
    elf.cpp \
    cli.hpp

lfspkg_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir) -DLOCALEDIR='"$(localedir)"'

lfspkg_CXXFLAGS = -std=c++17 -Wall -Wextra
