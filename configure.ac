dnl Process this file with autoconf to produce a configure script.

AC_INIT([hoffman],[1.4])

if test x"$CXXFLAGS" = x; then
        CXXFLAGS="-O3"
fi

AC_PROG_CXX

AC_LANG_PUSH([C++])
AX_CHECK_COMPILE_FLAG([-std=c++0x], [CXXFLAGS="$CXXFLAGS -std=c++0x"])
AC_LANG_POP

AC_ARG_WITH([pthreads],
  [AS_HELP_STRING([--without-pthreads], [Don't use pthreads])],
  [],
  [with_pthreads=check])

AS_IF([test "x$with_pthreads" != xno],
	    [AX_PTHREAD(CC="$PTHREAD_CC"; CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"; LIBS="$PTHREAD_LIBS $LIBS")])

dnl Check for the required libraries

AC_CHECK_LIB(m, sqrt,, AC_MSG_ERROR([You must have the math library]))
AC_CHECK_LIB(z, inflate,, AC_MSG_ERROR([You must have zlib]))
AM_PATH_XML2(,,AC_MSG_ERROR(You must have libxml2))

dnl Check for readline

m4_include(vl_lib_readline.m4)
VL_LIB_READLINE

if test "$ac_cv_header_readline_readline_h" = "yes"; then
   AC_SUBST([READLINE_CPPFLAGS], ["-I /usr/include/readline"])
fi

dnl Check for ftplib
dnl
dnl Do some extra work to make sure we can turn it off using
dnl --without-ftplib, keep the default action (if found) of adding
dnl library to LIBS and defining HAVE_LIBFTP, plus substituting
dnl FTPOBJS in Makefile.

AC_ARG_WITH([ftplib],
  [AS_HELP_STRING([--without-ftplib], [Don't link with ftplib for ftp: URL support])],
  [],
  [with_ftplib=check])

AS_IF([test "x$with_ftplib" != xno],
  [AC_CHECK_LIB([ftp], [FtpConnect],,
    [if test "x$with_ftplib" != xcheck; then
       AC_MSG_FAILURE([--with-ftplib was given, but test for ftplib failed])
     fi
    ])
   if test $ac_cv_lib_ftp_FtpConnect = yes; then
     AC_SUBST([FTPOBJS], ["ftp_fopen.o"])
   fi
])

dnl Check for libcurl
dnl
dnl PKG_CHECK_MODULES(CURL, libcurl, AC_DEFINE([USE_LIBCURL], [1], [Define if you have the Curl library])
dnl 				 AC_SUBST([CURLOBJS], [url_fopen.o]))

AC_ARG_WITH([libcurl],
  [AS_HELP_STRING([--without-libcurl], [Don't link with libcurl for http: URL support])],
  [],
  [with_libcurl=check])

AS_IF([test "x$with_libcurl" != xno],
  [PKG_CHECK_MODULES([CURL], [libcurl],
    [AC_DEFINE([USE_LIBCURL], [1], [Define if you have the Curl library])
     AC_SUBST([CURLOBJS], [url_fopen.o])],
    [if test "x$with_libcurl" != xcheck; then
       AC_MSG_FAILURE([--with-libcurl was given, but test for libcurl failed])
     fi
    ])])


dnl If we don't have C++, then we want to link with CC and leave out
dnl Nalimov support.  If we have C++, then we want to link with C++
dnl and include Nalimov support.

AC_SUBST(LINKER)

if test $ac_cv_prog_cxx_g = yes; then
   AC_DEFINE([USE_NALIMOV], [1], [Define to use Nalimov probe code to verify tablebase integrity])
   AC_SUBST([NALIMOVOBJS], ["probe.o egtb.o"])
   LINKER=$CXX
else
   LINKER=$CC
fi

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
