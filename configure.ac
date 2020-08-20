AC_INIT([dyplo-example-app], [0.1], [Topic Embedded Products])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CXX
AC_LANG([C++])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
AC_PROG_LIBTOOL
AX_PTHREAD(HAVE_PTHREAD=yes, AC_MSG_ERROR([Need pthreads]))
PKG_CHECK_MODULES([DYPLO], [dyplosw])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
