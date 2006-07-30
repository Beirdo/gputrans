#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(gputrans, SVN, gjhurlbu@gmail.com)
AC_CONFIG_AUX_DIR(scripts)
AC_CONFIG_SRCDIR(src/test.c)
AM_CONFIG_HEADER(include/config.h)

AC_CANONICAL_SYSTEM

AM_INIT_AUTOMAKE(gputrans, "SVN")


# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_PID_T

# Checks for library functions.
AC_FUNC_FORK
AC_OUTPUT([Makefile src/Makefile])
