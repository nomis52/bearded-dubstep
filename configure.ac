#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([bearded-dubstep], [0.0.1], [])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign])

# Checks for programs.
AC_PROG_CXX

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdint.h stdlib.h string.h termios.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_UINT16_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([strerror])

# pkg-config
PKG_PROG_PKG_CONFIG
AS_IF([test -z "$PKG_CONFIG"],
            [AC_MSG_ERROR([Missing pkg-config, please install it])])

# Check for libusb
PKG_CHECK_MODULES(
  [libusb],
  [libusb-1.0 >= 1.0.2],
  [true],
  [AC_MSG_ERROR([Please install libusb >= 1.0.2])])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
