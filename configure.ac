#                      -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([jsonbench], [0.1], [airween@gmail.com])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
#AC_CONFIG_MACRO_DIR([macros])

PKG_PROG_PKG_CONFIG
AC_MSG_RESULT([$PKG_CONFIG --variable pc_path pkg-config])
PKGPATH=$($PKG_CONFIG --variable pc_path pkg-config)

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

# Verbose output
AC_ARG_ENABLE(verbose-output,
    AS_HELP_STRING([--enable-verbose-output],
                   [Enable more verbose configure output.]),
    [
        if test "$enableval" != "no"; then
            verbose_output=1
        else
            verbose_output=0
        fi
    ],
    [
        verbose_output=0
    ]
)

# Checks for header files.

dnl # BEGIN try to find YAJL
yajl_header_exists="no"
yajl_lib_exists="no"

dnl # Try known package names
YAJL_CONFIG=${PKG_CONFIG}
YAJL_PKGNAMES="yajl2 yajl"

OLDIFS=$IFS

dnl Check if we want to use yajl or not
AC_MSG_CHECKING([whether to use yajl])
AC_ARG_WITH(
    [yajl],
    [AS_HELP_STRING([--with-yajl], [Enable yajl (default is enabled if exists)])],
    [case "$withval" in
        yes) wantyajl=yes ;;
        no)  wantyajl=no; AC_DEFINE([HAVE_YAJL], [0], [Mark that user does not want YAJL]) ;;
        *)   AC_MSG_ERROR([Invalid value for --with-feature: $withval]) ;;
    esac],
    [wantyajl="notset"]
)

if test "$wantyajl" != "no"; then
    if test -n "${YAJL_CONFIG}"; then
        YAJL_PKGNAME=""
        for x in ${YAJL_PKGNAMES}; do
            export IFS=":"
            for p in $PKGPATH; do
                AC_MSG_RESULT([PKG_CONFIG_PATH=${p} ${YAJL_CONFIG} --exists ${x}])
                if PKG_CONFIG_PATH=${p} ${YAJL_CONFIG} --exists ${x}; then
    #            AC_MSG_RESULT([${YAJL_CONFIG} --exists ${x}])
    #            if ${YAJL_CONFIG} --exists ${x}; then
                    YAJL_PKGNAME="$x"
                    break
                fi
            done
            unset IFS
        done
    fi
    export IFS=$OLDIFS

    if test -n "${YAJL_PKGNAME}"; then
        AC_MSG_RESULT([${YAJL_CONFIG}])
        YAJL_VERSION="`${YAJL_CONFIG} ${YAJL_PKGNAME} --modversion`"
        if test "$verbose_output" -eq 1; then AC_MSG_NOTICE(yajl VERSION: $YAJL_VERSION); fi
        YAJL_CFLAGS="`${YAJL_CONFIG} ${YAJL_PKGNAME} --cflags`"
        if test "$verbose_output" -eq 1; then AC_MSG_NOTICE(yajl CFLAGS: $YAJL_CFLAGS); fi
        YAJL_LDADD="`${YAJL_CONFIG} ${YAJL_PKGNAME} --libs-only-l`"
        if test "$verbose_output" -eq 1; then AC_MSG_NOTICE(yajl LDADD: $YAJL_LDADD); fi
        YAJL_LDFLAGS="`${YAJL_CONFIG} ${YAJL_PKGNAME} --libs-only-L --libs-only-other`"
        if test "$verbose_output" -eq 1; then AC_MSG_NOTICE(yajl LDFLAGS: $YAJL_LDFLAGS); fi
        AC_DEFINE([HAVE_YAJL], [1], [Mark that system has YAJL])
    else
        AC_MSG_NOTICE(["Can't find YAJL package"]);
    fi
else
    AC_DEFINE([HAVE_YAJL], [0], [Mark that user does not want YAJL])
    YAJL_VERSION=""
    YAJL_CFLAGS=""
    YAJL_LDADD=""
    YAJL_LDFLAGS=""
fi

AC_SUBST(YAJL_VERSION)
AC_SUBST(YAJL_CFLAGS)
AC_SUBST(YAJL_LDADD)
AC_SUBST(YAJL_LDFLAGS)

if test "$wantyajl" = "yes"; then
    if test -z "$YAJL_CONFIG" = 0; then
        AC_MSG_ERROR(["Can't find YAJL package"])
    fi
fi

dnl # END try to find YAJL

dnl Check if we want to use rapidjson or not
AC_MSG_CHECKING([whether to use rapidjson])
AC_ARG_WITH(
    [rapidjson],
    [AS_HELP_STRING([--with-rapidjson], [Enable rapidjson (default is enabled if exists)])],
    [case "$withval" in
        yes) wantrapidjson=yes ;;
        no)  wantrapidjson=no; AC_DEFINE([HAVE_RAPIDJSON], [0], [Mark that user does not want RapidJSON]) ;;
        *)   AC_MSG_ERROR([Invalid value for --with-feature: $withval]) ;;
    esac],
    [wantrapidjson="notset"]
)

if test "$wantrapidjson" != "no"; then
  AC_LANG_PUSH([C++])

  AC_CHECK_HEADERS([rapidjson/reader.h], [],
    [AC_MSG_ERROR([RapidJSON headers not found. Install rapidjson or use --disable-rapidjson])]
  )
  AC_DEFINE([HAVE_RAPIDJSON], [1], [Mark that user wants RapidJSON])
  AC_LANG_POP([C++])
fi

AM_CONDITIONAL([HAVE_RAPIDJSON], [test "x$enable_rapidjson" = "xyes"])

AC_ARG_WITH(
    [file-buffer-size],
    AS_HELP_STRING([--with-file-buffer-size=SIZE], [Set buffer size (default: 524288 bytes (512kB))]),
    [FILE_BUFFER_SIZE=$withval],
    [FILE_BUFFER_SIZE=524288]  # default file buffer size
)
AC_DEFINE_UNQUOTED([FILE_BUFFER_SIZE], [$FILE_BUFFER_SIZE], [Define file buffer size])

AC_ARG_WITH(
    [json-string-size],
    AS_HELP_STRING([--with-json-string-size=SIZE], [Set buffer size for JSON strings (key, values) (default: 8192 bytes (8kB))]),
    [JSON_STRING_SIZE=$withval],
    [JSON_STRING_SIZE=8192]  # default file buffer size
)
AC_DEFINE_UNQUOTED([JSON_STRING_SIZE], [$JSON_STRING_SIZE], [Define JSON string size])

AC_CHECK_HEADERS([time.h], [], [AC_MSG_ERROR([unable to find header time.h], 1)])
AC_CHECK_HEADERS([math.h], [], [AC_MSG_ERROR([unable to find header math.h], 1)])

# Checks for libraries.
AC_CHECK_LIB([rt], [clock_gettime], [], AC_MSG_ERROR([librt is not installed.], 1))
AC_CHECK_LIB([m], [powl], [], AC_MSG_ERROR([libm is not installed.], 1))

# Checks for typedefs, structures, and compiler characteristics.
#AC_TYPE_SIZE_T
#AC_TYPE_SSIZE_T

# Checks for library functions.

AC_CONFIG_FILES([Makefile src/Makefile])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_OUTPUT
