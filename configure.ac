dnl Process this file with autoconf to produce a configure script.

AC_INIT([hoffman],[1.7])

AM_INIT_AUTOMAKE([foreign subdir-objects])

if test x"$CXXFLAGS" = x; then
        CXXFLAGS="-O3 -march=native"
fi

AC_PROG_CXX

AX_CXX_COMPILE_STDCXX_11
AX_CXXFLAGS_WARN_ALL

dnl Check for threading support

AC_DEFUN([CHECK_INTEL_ATOMIC_OPS], [
	AC_MSG_CHECKING([whether Intel atomic operations are available])
	AC_LANG_PUSH([C])
        AC_LINK_IFELSE([AC_LANG_PROGRAM([], [[int x; __sync_fetch_and_or(&x,1);]])],
                [intel_atomic_ops_ok=yes],
                [intel_atomic_ops_ok=no])

        if test x"$intel_atomic_ops_ok" = xyes; then
		ifelse([$1],,AC_DEFINE(HAVE_INTEL_ATOMIC_OPS,1,[Define if you have Intel atomic operations]),[$1])
		:
		AC_MSG_RESULT(yes)
	else
		AC_MSG_RESULT(no)
        fi
	AC_LANG_POP
])

AC_ARG_WITH([pthreads],
  [AS_HELP_STRING([--without-pthreads], [Don't use pthreads])],
  [],
  [with_pthreads=check])

AS_IF([test "x$with_pthreads" != xno],
[AX_PTHREAD([CHECK_INTEL_ATOMIC_OPS
	AS_IF([test $intel_atomic_ops_ok = yes],[
		    AC_DEFINE(HAVE_PTHREAD,1,[Define if you have POSIX threads libraries and header files.])
		    AC_DEFINE(USE_THREADS,1,[Define to use POSIX threads libraries and header files.])
		    CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"; LIBS="$PTHREAD_LIBS $LIBS"
	],[AC_MSG_WARN([Intel atomic operations unavailable - disabling threading])]
)])])

dnl Check for the required libraries

AC_CHECK_LIB(z, inflate,, AC_MSG_ERROR([You must have zlib]))

PKG_CHECK_MODULES(libxml, libxml++-2.6 >= 2.10.0)
AC_SUBST(libxml_CFLAGS)
AC_SUBST(libxml_LIBS)

dnl Boost C++ library

AX_BOOST_BASE

dnl AX_BOOST_IOSTREAMS doesn't work on my Ubuntu 14.10 system, so I hand code -lboost_iostreams
dnl in the Makefile instead of using the macro.

dnl Check for readline

AX_LIB_READLINE

if test "$ac_cv_header_readline_readline_h" = "yes"; then
   AC_SUBST([READLINE_CPPFLAGS], ["-I /usr/include/readline"])
fi

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
AC_CONFIG_FILES([Makefile test/Makefile])
AC_OUTPUT
