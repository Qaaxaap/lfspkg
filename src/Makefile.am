noinst_LIBRARIES = liblfspkg.a

liblfspkg_a_SOURCES = \
    config.cpp \
    db.cpp \
    package.cpp \
    util.cpp \
    elf.cpp

liblfspkg_a_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir) -DLOCALEDIR='"$(localedir)"'
liblfspkg_a_CXXFLAGS = -std=c++17 -Wall -Wextra

bin_PROGRAMS = lfspkg

lfspkg_SOURCES = \
    main.cpp \
    cli.cpp \
    cli.hpp

lfspkg_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir) -DLOCALEDIR='"$(localedir)"'
lfspkg_CXXFLAGS = -std=c++17 -Wall -Wextra
lfspkg_LDADD = liblfspkg.a
