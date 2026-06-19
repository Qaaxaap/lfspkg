AC_INIT([lfspkg], [0.1.2], [github.com/Qaaxaap/lfspkg/issues])

AC_CONFIG_SRCDIR([src/main.cpp])
AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_PROG_CXX
AC_PROG_RANLIB

AC_CONFIG_HEADERS([config.h])

AC_CHECK_HEADER([libintl.h], [], [AC_MSG_ERROR([libintl.h not found; install gettext])])
AC_CHECK_FUNC([gettext], [], [AC_MSG_ERROR([gettext() not found])])

AC_CONFIG_FILES([
    Makefile
    po/Makefile
    src/Makefile
    tests/Makefile
])

AC_OUTPUT
