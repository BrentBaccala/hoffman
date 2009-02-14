dnl Process this file with autoconf to produce a configure script.

AC_INIT(hoffman, 1.2)

if test x"$CFLAGS" = x; then
        CFLAGS="-O3"
fi

AC_PROG_CC
AC_PROG_CXX

if test x"$GCC" = xyes; then
        CFLAGS="$CFLAGS -Wall"
else
	AC_MSG_WARN(You don't have gcc; Hoffman uses several gcc extensions)
fi

AC_CHECK_SIZEOF(unsigned short)
AC_CHECK_SIZEOF(unsigned int)
AC_CHECK_SIZEOF(unsigned long)
AC_CHECK_SIZEOF(unsigned long long)

AC_DEFUN([CHECK_INTEL_ATOMIC_OPS],
[
	AC_MSG_CHECKING([whether Intel atomic operations are available])
        AC_TRY_LINK([],
                [
			int x;
			__sync_fetch_and_or(&x,1);
                ],
                intel_atomic_ops_ok=yes,
                intel_atomic_ops_ok=no)

        if test x"$intel_atomic_ops_ok" = xyes; then
		ifelse([$1],,AC_DEFINE(HAVE_INTEL_ATOMIC_OPS,1,[Define if you have Intel atomic operations]),[$1])
		:
		AC_MSG_RESULT(yes)
	else
		AC_MSG_RESULT(no)
        fi
])

AC_DEFUN([FREESOFT_CHECK_SYNC_FETCH_AND_ADD_8],
[
	AC_MSG_CHECKING([whether __sync_fetch_and_add works on 64-bit integers])
        AC_TRY_LINK([],
                [
			long long int x;
			__sync_fetch_and_add(&x,1);
                ],
                ac_cv_sync_fetch_and_add_8=yes,
                ac_cv_sync_fetch_and_add_8=no)

        if test x"$ac_cv_sync_fetch_and_add_8" = "xyes"
        then
                AC_DEFINE(HAVE_SYNC_FETCH_AND_ADD_8, 1,
                        [ Define if your system has a __sync_fetch_and_add_8 ])
		AC_MSG_RESULT(yes)
	else
		AC_MSG_RESULT(no)
        fi
])

AC_ARG_WITH([pthreads],
  [AS_HELP_STRING([--without-pthreads], [Don't use pthreads])],
  [],
  [with_pthreads=check])

AS_IF([test "x$with_pthreads" != xno],
[ACX_PTHREAD(CHECK_INTEL_ATOMIC_OPS
	AS_IF([test $intel_atomic_ops_ok = yes],[
		    AC_DEFINE(HAVE_PTHREAD,1,[Define if you have POSIX threads libraries and header files.])
		    AC_DEFINE(USE_THREADS,1,[Define to use POSIX threads libraries and header files.])
		    CC="$PTHREAD_CC"; CFLAGS="$CFLAGS $PTHREAD_CFLAGS"; LIBS="$PTHREAD_LIBS $LIBS"
		    FREESOFT_CHECK_SYNC_FETCH_AND_ADD_8
	],[AC_MSG_WARN([Intel atomic operations unavailable - disabling threading])]
))])


dnl Check for the required libraries

AC_CHECK_LIB(m, sqrt,, AC_MSG_ERROR([You must have the math library]))
AC_CHECK_LIB(z, inflate,, AC_MSG_ERROR([You must have zlib]))
AM_PATH_XML2(,,AC_MSG_ERROR(You must have libxml2))

dnl Check for readline

AC_CHECK_LIB(readline, readline,, AC_MSG_WARN([No readline support]))

if test $ac_cv_lib_readline_readline = yes; then
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
AC_OUTPUT(Makefile)
