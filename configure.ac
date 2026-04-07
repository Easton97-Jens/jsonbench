#                      -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([jsonbench], [0.2], [airween@gmail.com])
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

JSONC_CFLAGS=""
JSONC_LDADD=""
JSONC_LDFLAGS=""
JANSSON_CFLAGS=""
JANSSON_LDADD=""
JANSSON_LDFLAGS=""
CJSON_CFLAGS=""
CJSON_LDADD=""
CJSON_LDFLAGS=""
JSONCPP_CFLAGS=""
JSONCPP_LDADD=""
JSONCPP_LDFLAGS=""
SIMDJSON_CFLAGS=""
SIMDJSON_LDADD=""
SIMDJSON_LDFLAGS=""
YYJSON_CFLAGS=""
YYJSON_LDADD=""
YYJSON_LDFLAGS=""
GLAZE_CFLAGS=""

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
#dnl End check for rapidjson


dnl Check if we want to use nlohmannjson or not
AC_MSG_CHECKING([whether to use nlohmannjson])
AC_ARG_WITH(
    [nlohmannjson],
    [AS_HELP_STRING([--with-nlohmannjson], [Enable nlohmannjson (default is enabled if exists)])],
    [case "$withval" in
        yes) wannlohmannjson=yes ;;
        no)  wannlohmannjson=no; AC_DEFINE([HAVE_NLOHMANNJSON], [0], [Mark that user does not want NlohmannJSON]) ;;
        *)   AC_MSG_ERROR([Invalid value for --with-feature: $withval]) ;;
    esac],
    [wannlohmannjson="notset"]
)

if test "$wannlohmannjson" != "no"; then
  AC_LANG_PUSH([C++])

  AC_CHECK_HEADERS([nlohmann/json.hpp], [],
    [AC_MSG_ERROR([NlohmannJSON headers not found. Install nlohmannjson or use --disable-nlohmannjson])]
  )
  AC_DEFINE([HAVE_NLOHMANNJSON], [1], [Mark that user wants NlohmannJSON])
  AC_LANG_POP([C++])
fi

AM_CONDITIONAL([HAVE_NLOHMANNJSON], [test "x$enable_nlohmannjson" = "xyes"])
#dnl End check for nlohmannjson

dnl Check if we want to use json-c or not
AC_MSG_CHECKING([whether to use json-c])
AC_ARG_WITH(
    [jsonc],
    [AS_HELP_STRING([--with-jsonc], [Enable json-c (default is enabled if exists)])],
    [case "$withval" in
        yes) wantjsonc=yes ;;
        no)  wantjsonc=no; AC_DEFINE([HAVE_JSONC], [0], [Mark that user does not want json-c]) ;;
        *)   AC_MSG_ERROR([Invalid value for --with-feature: $withval]) ;;
    esac],
    [wantjsonc="notset"]
)

if test "$wantjsonc" != "no"; then
    PKG_CHECK_MODULES([JSONC], [json-c], [
        JSONC_CFLAGS="$JSONC_CFLAGS"
        JSONC_LDADD="$JSONC_LIBS"
        AC_DEFINE([HAVE_JSONC], [1], [Mark that system has json-c])
    ], [
        AC_MSG_ERROR([json-c was requested but not found via pkg-config. Install json-c or disable with --with-jsonc=no])
    ])
fi

dnl Check if we want to use jansson or not
AC_MSG_CHECKING([whether to use jansson])
AC_ARG_WITH(
    [jansson],
    [AS_HELP_STRING([--with-jansson], [Enable jansson (default is enabled if exists)])],
    [case "$withval" in
        yes) wantjansson=yes ;;
        no)  wantjansson=no; AC_DEFINE([HAVE_JANSSON], [0], [Mark that user does not want jansson]) ;;
        *)   AC_MSG_ERROR([Invalid value for --with-feature: $withval]) ;;
    esac],
    [wantjansson="notset"]
)

if test "$wantjansson" != "no"; then
    PKG_CHECK_MODULES([JANSSON], [jansson], [
        JANSSON_CFLAGS="$JANSSON_CFLAGS"
        JANSSON_LDADD="$JANSSON_LIBS"
        AC_DEFINE([HAVE_JANSSON], [1], [Mark that system has jansson])
    ], [
        AC_MSG_ERROR([jansson was requested but not found via pkg-config. Install jansson or disable with --with-jansson=no])
    ])
fi

dnl Check if we want to use cJSON or not
AC_MSG_CHECKING([whether to use cJSON])
AC_ARG_WITH(
    [cjson],
    [AS_HELP_STRING([--with-cjson], [Enable cJSON (default is enabled if exists)])],
    [case "$withval" in
        yes) wantcjson=yes ;;
        no)  wantcjson=no; AC_DEFINE([HAVE_CJSON], [0], [Mark that user does not want cJSON]) ;;
        *)   AC_MSG_ERROR([Invalid value for --with-feature: $withval]) ;;
    esac],
    [wantcjson="notset"]
)

if test "$wantcjson" != "no"; then
    PKG_CHECK_MODULES([CJSON], [libcjson], [
        CJSON_CFLAGS="$CJSON_CFLAGS"
        CJSON_LDADD="$CJSON_LIBS"
        AC_DEFINE([HAVE_CJSON], [1], [Mark that system has cJSON])
    ], [
        AC_MSG_ERROR([cJSON was requested but not found via pkg-config as libcjson. Install libcjson or disable with --with-cjson=no])
    ])
fi

dnl Check if we want to use jsoncpp or not
AC_MSG_CHECKING([whether to use jsoncpp])
AC_ARG_WITH(
    [jsoncpp],
    [AS_HELP_STRING([--with-jsoncpp], [Enable jsoncpp (default is enabled if exists)])],
    [case "$withval" in
        yes) wantjsoncpp=yes ;;
        no)  wantjsoncpp=no; AC_DEFINE([HAVE_JSONCPP], [0], [Mark that user does not want JsonCpp]) ;;
        *)   AC_MSG_ERROR([Invalid value for --with-feature: $withval]) ;;
    esac],
    [wantjsoncpp="notset"]
)

if test "$wantjsoncpp" != "no"; then
    PKG_CHECK_MODULES([JSONCPP], [jsoncpp], [
        JSONCPP_CFLAGS="$JSONCPP_CFLAGS"
        JSONCPP_LDADD="$JSONCPP_LIBS"
        AC_DEFINE([HAVE_JSONCPP], [1], [Mark that system has JsonCpp])
    ], [
        AC_LANG_PUSH([C++])
        jsoncpp_header="no"
        AC_CHECK_HEADERS([json/json.h], [jsoncpp_header="json/json.h"])
        if test "$jsoncpp_header" = "no"; then
            AC_CHECK_HEADERS([jsoncpp/json/json.h], [
                jsoncpp_header="jsoncpp/json/json.h"
                JSONCPP_CFLAGS="-I/usr/include/jsoncpp"
            ])
        fi
        if test "$jsoncpp_header" = "no"; then
            AC_MSG_ERROR([JsonCpp header not found (tried json/json.h and jsoncpp/json/json.h). Install jsoncpp development headers or disable with --with-jsoncpp=no])
        fi
        old_LIBS="$LIBS"
        LIBS="$LIBS -ljsoncpp"
        AC_LINK_IFELSE(
            [AC_LANG_PROGRAM([[#include <$jsoncpp_header>]], [[Json::Value root; return root.isNull() ? 0 : 1;]])],
            [
                JSONCPP_LDADD="-ljsoncpp"
                AC_DEFINE([HAVE_JSONCPP], [1], [Mark that system has JsonCpp])
            ],
            [AC_MSG_ERROR([JsonCpp library not found. Install libjsoncpp or disable with --with-jsoncpp=no])]
        )
        LIBS="$old_LIBS"
        AC_LANG_POP([C++])
    ])
fi



dnl Check if we want to use simdjson or not
AC_MSG_CHECKING([whether to use simdjson])
AC_ARG_WITH(
    [simdjson],
    [AS_HELP_STRING([--with-simdjson], [Enable simdjson (default is enabled if exists)])],
    [case "$withval" in
        yes) wantsimdjson=yes ;;
        no)  wantsimdjson=no; AC_DEFINE([HAVE_SIMDJSON], [0], [Mark that user does not want simdjson]) ;;
        *)   AC_MSG_ERROR([Invalid value for --with-feature: $withval]) ;;
    esac],
    [wantsimdjson="notset"]
)

if test "$wantsimdjson" != "no"; then
    PKG_CHECK_MODULES([SIMDJSON], [simdjson], [
        SIMDJSON_CFLAGS="$SIMDJSON_CFLAGS"
        SIMDJSON_LDADD="$SIMDJSON_LIBS"
        AC_DEFINE([HAVE_SIMDJSON], [1], [Mark that system has simdjson])
    ], [
        AC_LANG_PUSH([C++])
        AC_CHECK_HEADERS([simdjson.h], [],
            [AC_MSG_ERROR([simdjson.h not found. Install simdjson headers or disable with --with-simdjson=no])]
        )
        old_LIBS="$LIBS"
        LIBS="$LIBS -lsimdjson"
        AC_LINK_IFELSE(
            [AC_LANG_PROGRAM([[#include <simdjson.h>]], [[simdjson::ondemand::parser parser; return 0;]])],
            [
                SIMDJSON_LDADD="-lsimdjson"
                AC_DEFINE([HAVE_SIMDJSON], [1], [Mark that system has simdjson])
            ],
            [AC_MSG_ERROR([simdjson library not found. Install libsimdjson or disable with --with-simdjson=no])]
        )
        LIBS="$old_LIBS"
        AC_LANG_POP([C++])
    ])
fi

dnl Check if we want to use yyjson or not
AC_MSG_CHECKING([whether to use yyjson])
AC_ARG_WITH(
    [yyjson],
    [AS_HELP_STRING([--with-yyjson], [Enable yyjson (default is enabled if exists)])],
    [case "$withval" in
        yes) wantyyjson=yes ;;
        no)  wantyyjson=no; AC_DEFINE([HAVE_YYJSON], [0], [Mark that user does not want yyjson]) ;;
        *)   AC_MSG_ERROR([Invalid value for --with-feature: $withval]) ;;
    esac],
    [wantyyjson="notset"]
)

if test "$wantyyjson" != "no"; then
    PKG_CHECK_MODULES([YYJSON], [yyjson], [
        YYJSON_CFLAGS="$YYJSON_CFLAGS"
        YYJSON_LDADD="$YYJSON_LIBS"
        AC_DEFINE([HAVE_YYJSON], [1], [Mark that system has yyjson])
    ], [
        AC_MSG_ERROR([yyjson was requested but not found via pkg-config. Install yyjson or disable with --with-yyjson=no])
    ])
fi

dnl Check if we want to use glaze or not
AC_MSG_CHECKING([whether to use glaze])
AC_ARG_WITH(
    [glaze],
    [AS_HELP_STRING([--with-glaze], [Enable glaze (default is enabled if exists)])],
    [case "$withval" in
        yes) wantglaze=yes ;;
        no)  wantglaze=no; AC_DEFINE([HAVE_GLAZE], [0], [Mark that user does not want glaze]) ;;
        *)   AC_MSG_ERROR([Invalid value for --with-feature: $withval]) ;;
    esac],
    [wantglaze="notset"]
)

if test "$wantglaze" != "no"; then
    AC_LANG_PUSH([C++])
    AC_CHECK_HEADERS([glaze/glaze.hpp], [],
        [AC_MSG_ERROR([glaze/glaze.hpp not found. Install glaze headers or disable with --with-glaze=no])]
    )
    AC_LANG_POP([C++])
    AC_DEFINE([HAVE_GLAZE], [1], [Mark that system has glaze])
fi

dnl Check if we want to use jsoncons or not
AC_MSG_CHECKING([whether to use jsoncons])
AC_ARG_WITH(
    [jsoncons],
    [AS_HELP_STRING([--with-jsoncons], [Enable jsoncons (default is enabled if exists)])],
    [case "$withval" in
        yes) wantjsoncons=yes ;;
        no)  wantjsoncons=no; AC_DEFINE([HAVE_JSONCONS], [0], [Mark that user does not want jsoncons]) ;;
        *)   AC_MSG_ERROR([Invalid value for --with-feature: $withval]) ;;
    esac],
    [wantjsoncons="notset"]
)

if test "$wantjsoncons" != "no"; then
    AC_LANG_PUSH([C++])
    AC_CHECK_HEADERS([jsoncons/json.hpp], [],
        [AC_MSG_ERROR([jsoncons/json.hpp not found. Install jsoncons headers or disable with --with-jsoncons=no])]
    )
    AC_LANG_POP([C++])
    AC_DEFINE([HAVE_JSONCONS], [1], [Mark that system has jsoncons])
fi

AC_SUBST(JSONC_CFLAGS)
AC_SUBST(JSONC_LDADD)
AC_SUBST(JSONC_LDFLAGS)
AC_SUBST(JANSSON_CFLAGS)
AC_SUBST(JANSSON_LDADD)
AC_SUBST(JANSSON_LDFLAGS)
AC_SUBST(CJSON_CFLAGS)
AC_SUBST(CJSON_LDADD)
AC_SUBST(CJSON_LDFLAGS)
AC_SUBST(JSONCPP_CFLAGS)
AC_SUBST(JSONCPP_LDADD)
AC_SUBST(JSONCPP_LDFLAGS)
AC_SUBST(SIMDJSON_CFLAGS)
AC_SUBST(SIMDJSON_LDADD)
AC_SUBST(SIMDJSON_LDFLAGS)
AC_SUBST(YYJSON_CFLAGS)
AC_SUBST(YYJSON_LDADD)
AC_SUBST(YYJSON_LDFLAGS)
AC_SUBST(GLAZE_CFLAGS)

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
