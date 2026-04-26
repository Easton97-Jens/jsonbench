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
GLAZE_CXXFLAGS=""

have_yajl=no
have_rapidjson=no
have_nlohmannjson=no
have_jsonc=no
have_jansson=no
have_cjson=no
have_jsoncpp=no
have_simdjson=no
have_yyjson=no
have_glaze=no
have_jsoncons=no

AC_MSG_CHECKING([whether all JSON libraries are required])
AC_ARG_WITH(
    [all-json],
    [AS_HELP_STRING([--with-all-json], [Require all JSON libraries (default: no)])],
    [case "$withval" in
        yes) wantalljson=yes ;;
        no)  wantalljson=no ;;
        *)   AC_MSG_ERROR([Invalid value for --with-all-json: $withval (use yes or no)]) ;;
    esac],
    [wantalljson=no]
)
AC_MSG_RESULT([$wantalljson])

if test "$wantalljson" = "yes"; then
    if test "x$with_yajl" = "xno"; then
        AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but yajl was explicitly disabled with --with-yajl=no. Remove --with-yajl=no, or do not use --with-all-json=yes.])
    fi
    if test "x$with_rapidjson" = "xno"; then
        AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but rapidjson was explicitly disabled with --with-rapidjson=no. Remove --with-rapidjson=no, or do not use --with-all-json=yes.])
    fi
    if test "x$with_nlohmannjson" = "xno"; then
        AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but nlohmannjson was explicitly disabled with --with-nlohmannjson=no. Remove --with-nlohmannjson=no, or do not use --with-all-json=yes.])
    fi
    if test "x$with_jsonc" = "xno"; then
        AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but json-c was explicitly disabled with --with-jsonc=no. Remove --with-jsonc=no, or do not use --with-all-json=yes.])
    fi
    if test "x$with_jansson" = "xno"; then
        AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but jansson was explicitly disabled with --with-jansson=no. Remove --with-jansson=no, or do not use --with-all-json=yes.])
    fi
    if test "x$with_cjson" = "xno"; then
        AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but cJSON was explicitly disabled with --with-cjson=no. Remove --with-cjson=no, or do not use --with-all-json=yes.])
    fi
    if test "x$with_jsoncpp" = "xno"; then
        AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but jsoncpp was explicitly disabled with --with-jsoncpp=no. Remove --with-jsoncpp=no, or do not use --with-all-json=yes.])
    fi
    if test "x$with_simdjson" = "xno"; then
        AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but simdjson was explicitly disabled with --with-simdjson=no. Remove --with-simdjson=no, or do not use --with-all-json=yes.])
    fi
    if test "x$with_yyjson" = "xno"; then
        AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but yyjson was explicitly disabled with --with-yyjson=no. Remove --with-yyjson=no, or do not use --with-all-json=yes.])
    fi
    if test "x$with_glaze" = "xno"; then
        AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but glaze was explicitly disabled with --with-glaze=no. Remove --with-glaze=no, or do not use --with-all-json=yes.])
    fi
    if test "x$with_jsoncons" = "xno"; then
        AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but jsoncons was explicitly disabled with --with-jsoncons=no. Remove --with-jsoncons=no, or do not use --with-all-json=yes.])
    fi
fi

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
if test "$wantalljson" = "yes" && test "$wantyajl" = "no"; then
    AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but yajl was explicitly disabled with --with-yajl=no. Remove --with-yajl=no, or do not use --with-all-json=yes.])
fi

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
        have_yajl=yes
    else
        AC_MSG_NOTICE(["Can't find YAJL package"]);
        if test "$wantalljson" = "yes" || test "$wantyajl" = "yes"; then
            AC_MSG_ERROR([Can't find YAJL package. Required by --with-all-json=yes or --with-yajl=yes])
        fi
        AC_DEFINE([HAVE_YAJL], [0], [Mark that system has YAJL])
        have_yajl=no
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
if test "$wantalljson" = "yes" && test "$wantrapidjson" = "no"; then
    AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but rapidjson was explicitly disabled with --with-rapidjson=no. Remove --with-rapidjson=no, or do not use --with-all-json=yes.])
fi

if test "$wantrapidjson" != "no"; then
  AC_LANG_PUSH([C++])

  AC_CHECK_HEADERS([rapidjson/reader.h], [
      AC_DEFINE([HAVE_RAPIDJSON], [1], [Mark that user wants RapidJSON])
      have_rapidjson=yes
    ],
    [
      AC_DEFINE([HAVE_RAPIDJSON], [0], [Mark that user wants RapidJSON])
      if test "$wantalljson" = "yes" || test "$wantrapidjson" = "yes"; then
          AC_MSG_ERROR([RapidJSON headers not found. Required by --with-all-json=yes or --with-rapidjson=yes])
      else
          AC_MSG_NOTICE([RapidJSON headers not found. Continuing without rapidjson.])
      fi
    ]
  )
  AC_LANG_POP([C++])
else
  AC_DEFINE([HAVE_RAPIDJSON], [0], [Mark that user does not want RapidJSON])
fi

AM_CONDITIONAL([HAVE_RAPIDJSON], [test "x$have_rapidjson" = "xyes"])
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
if test "$wantalljson" = "yes" && test "$wannlohmannjson" = "no"; then
    AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but nlohmannjson was explicitly disabled with --with-nlohmannjson=no. Remove --with-nlohmannjson=no, or do not use --with-all-json=yes.])
fi

if test "$wannlohmannjson" != "no"; then
  AC_LANG_PUSH([C++])

  AC_CHECK_HEADERS([nlohmann/json.hpp], [
      AC_DEFINE([HAVE_NLOHMANNJSON], [1], [Mark that user wants NlohmannJSON])
      have_nlohmannjson=yes
    ],
    [
      AC_DEFINE([HAVE_NLOHMANNJSON], [0], [Mark that user wants NlohmannJSON])
      if test "$wantalljson" = "yes" || test "$wannlohmannjson" = "yes"; then
          AC_MSG_ERROR([NlohmannJSON headers not found. Required by --with-all-json=yes or --with-nlohmannjson=yes])
      else
          AC_MSG_NOTICE([NlohmannJSON headers not found. Continuing without nlohmannjson.])
      fi
    ]
  )
  AC_LANG_POP([C++])
else
  AC_DEFINE([HAVE_NLOHMANNJSON], [0], [Mark that user does not want NlohmannJSON])
  have_nlohmannjson=no
fi

AM_CONDITIONAL([HAVE_NLOHMANNJSON], [test "x$have_nlohmannjson" = "xyes"])
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
if test "$wantalljson" = "yes" && test "$wantjsonc" = "no"; then
    AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but json-c was explicitly disabled with --with-jsonc=no. Remove --with-jsonc=no, or do not use --with-all-json=yes.])
fi

if test "$wantjsonc" != "no"; then
    PKG_CHECK_MODULES([JSONC], [json-c], [
        JSONC_CFLAGS="$JSONC_CFLAGS"
        JSONC_LDADD="$JSONC_LIBS"
        AC_DEFINE([HAVE_JSONC], [1], [Mark that system has json-c])
        have_jsonc=yes
    ], [
        AC_DEFINE([HAVE_JSONC], [0], [Mark that system has json-c])
        if test "$wantalljson" = "yes" || test "$wantjsonc" = "yes"; then
            AC_MSG_ERROR([json-c was requested with --with-all-json=yes or --with-jsonc=yes but not found via pkg-config. Install json-c or use --with-jsonc=no])
        else
            AC_MSG_NOTICE([json-c not found via pkg-config. Continuing without json-c.])
        fi
    ])
else
    AC_DEFINE([HAVE_JSONC], [0], [Mark that user does not want json-c])
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
if test "$wantalljson" = "yes" && test "$wantjansson" = "no"; then
    AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but jansson was explicitly disabled with --with-jansson=no. Remove --with-jansson=no, or do not use --with-all-json=yes.])
fi

if test "$wantjansson" != "no"; then
    PKG_CHECK_MODULES([JANSSON], [jansson], [
        JANSSON_CFLAGS="$JANSSON_CFLAGS"
        JANSSON_LDADD="$JANSSON_LIBS"
        AC_DEFINE([HAVE_JANSSON], [1], [Mark that system has jansson])
        have_jansson=yes
    ], [
        AC_DEFINE([HAVE_JANSSON], [0], [Mark that system has jansson])
        if test "$wantalljson" = "yes" || test "$wantjansson" = "yes"; then
            AC_MSG_ERROR([jansson was requested with --with-all-json=yes or --with-jansson=yes but not found via pkg-config. Install jansson or use --with-jansson=no])
        else
            AC_MSG_NOTICE([jansson not found via pkg-config. Continuing without jansson.])
        fi
    ])
else
    AC_DEFINE([HAVE_JANSSON], [0], [Mark that user does not want jansson])
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
if test "$wantalljson" = "yes" && test "$wantcjson" = "no"; then
    AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but cJSON was explicitly disabled with --with-cjson=no. Remove --with-cjson=no, or do not use --with-all-json=yes.])
fi

if test "$wantcjson" != "no"; then
    PKG_CHECK_MODULES([CJSON], [libcjson], [
        CJSON_CFLAGS="$CJSON_CFLAGS"
        CJSON_LDADD="$CJSON_LIBS"
        AC_DEFINE([HAVE_CJSON], [1], [Mark that system has cJSON])
        have_cjson=yes
    ], [
        AC_DEFINE([HAVE_CJSON], [0], [Mark that system has cJSON])
        if test "$wantalljson" = "yes" || test "$wantcjson" = "yes"; then
            AC_MSG_ERROR([cJSON was requested with --with-all-json=yes or --with-cjson=yes but not found via pkg-config as libcjson. Install libcjson or use --with-cjson=no])
        else
            AC_MSG_NOTICE([cJSON not found via pkg-config as libcjson. Continuing without cJSON.])
        fi
    ])
else
    AC_DEFINE([HAVE_CJSON], [0], [Mark that user does not want cJSON])
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
if test "$wantalljson" = "yes" && test "$wantjsoncpp" = "no"; then
    AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but jsoncpp was explicitly disabled with --with-jsoncpp=no. Remove --with-jsoncpp=no, or do not use --with-all-json=yes.])
fi

if test "$wantjsoncpp" != "no"; then
    PKG_CHECK_MODULES([JSONCPP], [jsoncpp], [
        JSONCPP_CFLAGS="$JSONCPP_CFLAGS"
        JSONCPP_LDADD="$JSONCPP_LIBS"
        AC_DEFINE([HAVE_JSONCPP], [1], [Mark that system has JsonCpp])
        have_jsoncpp=yes
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
            AC_DEFINE([HAVE_JSONCPP], [0], [Mark that system has JsonCpp])
            if test "$wantalljson" = "yes" || test "$wantjsoncpp" = "yes"; then
                AC_MSG_ERROR([JsonCpp header not found (tried json/json.h and jsoncpp/json/json.h). Required by --with-all-json=yes or --with-jsoncpp=yes])
            else
                AC_MSG_NOTICE([JsonCpp headers not found. Continuing without jsoncpp.])
            fi
        fi
        old_LIBS="$LIBS"
        LIBS="$LIBS -ljsoncpp"
        AC_LINK_IFELSE(
            [AC_LANG_PROGRAM([[#include <$jsoncpp_header>]], [[Json::Value root; return root.isNull() ? 0 : 1;]])],
            [
                JSONCPP_LDADD="-ljsoncpp"
                AC_DEFINE([HAVE_JSONCPP], [1], [Mark that system has JsonCpp])
                have_jsoncpp=yes
            ],
            [
                AC_DEFINE([HAVE_JSONCPP], [0], [Mark that system has JsonCpp])
                if test "$wantalljson" = "yes" || test "$wantjsoncpp" = "yes"; then
                    AC_MSG_ERROR([JsonCpp library not found. Required by --with-all-json=yes or --with-jsoncpp=yes])
                else
                    AC_MSG_NOTICE([JsonCpp library not found. Continuing without jsoncpp.])
                fi
            ]
        )
        LIBS="$old_LIBS"
        AC_LANG_POP([C++])
    ])
else
    AC_DEFINE([HAVE_JSONCPP], [0], [Mark that user does not want JsonCpp])
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
if test "$wantalljson" = "yes" && test "$wantsimdjson" = "no"; then
    AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but simdjson was explicitly disabled with --with-simdjson=no. Remove --with-simdjson=no, or do not use --with-all-json=yes.])
fi

if test "$wantsimdjson" != "no"; then
    PKG_CHECK_MODULES([SIMDJSON], [simdjson], [
        SIMDJSON_CFLAGS="$SIMDJSON_CFLAGS"
        SIMDJSON_LDADD="$SIMDJSON_LIBS"
        AC_DEFINE([HAVE_SIMDJSON], [1], [Mark that system has simdjson])
        have_simdjson=yes
    ], [
        AC_LANG_PUSH([C++])
        AC_CHECK_HEADERS([simdjson.h], [],
            [
                AC_DEFINE([HAVE_SIMDJSON], [0], [Mark that system has simdjson])
                if test "$wantalljson" = "yes" || test "$wantsimdjson" = "yes"; then
                    AC_MSG_ERROR([simdjson.h not found. Required by --with-all-json=yes or --with-simdjson=yes])
                else
                    AC_MSG_NOTICE([simdjson.h not found. Continuing without simdjson.])
                fi
            ]
        )
        old_LIBS="$LIBS"
        LIBS="$LIBS -lsimdjson"
        AC_LINK_IFELSE(
            [AC_LANG_PROGRAM([[#include <simdjson.h>]], [[simdjson::ondemand::parser parser; return 0;]])],
            [
                SIMDJSON_LDADD="-lsimdjson"
                AC_DEFINE([HAVE_SIMDJSON], [1], [Mark that system has simdjson])
                have_simdjson=yes
            ],
            [
                AC_DEFINE([HAVE_SIMDJSON], [0], [Mark that system has simdjson])
                if test "$wantalljson" = "yes" || test "$wantsimdjson" = "yes"; then
                    AC_MSG_ERROR([simdjson library not found. Required by --with-all-json=yes or --with-simdjson=yes])
                else
                    AC_MSG_NOTICE([simdjson library not found. Continuing without simdjson.])
                fi
            ]
        )
        LIBS="$old_LIBS"
        AC_LANG_POP([C++])
    ])
else
    AC_DEFINE([HAVE_SIMDJSON], [0], [Mark that user does not want simdjson])
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
if test "$wantalljson" = "yes" && test "$wantyyjson" = "no"; then
    AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but yyjson was explicitly disabled with --with-yyjson=no. Remove --with-yyjson=no, or do not use --with-all-json=yes.])
fi

if test "$wantyyjson" != "no"; then
    PKG_CHECK_MODULES([YYJSON], [yyjson], [
        YYJSON_CFLAGS="$YYJSON_CFLAGS"
        YYJSON_LDADD="$YYJSON_LIBS"
        AC_DEFINE([HAVE_YYJSON], [1], [Mark that system has yyjson])
        have_yyjson=yes
    ], [
        AC_DEFINE([HAVE_YYJSON], [0], [Mark that system has yyjson])
        if test "$wantalljson" = "yes" || test "$wantyyjson" = "yes"; then
            AC_MSG_ERROR([yyjson was requested with --with-all-json=yes or --with-yyjson=yes but not found via pkg-config. Install yyjson or use --with-yyjson=no])
        else
            AC_MSG_NOTICE([yyjson not found via pkg-config. Continuing without yyjson.])
        fi
    ])
else
    AC_DEFINE([HAVE_YYJSON], [0], [Mark that user does not want yyjson])
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
if test "$wantalljson" = "yes" && test "$wantglaze" = "no"; then
    AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but glaze was explicitly disabled with --with-glaze=no. Remove --with-glaze=no, or do not use --with-all-json=yes.])
fi

if test "$wantglaze" != "no"; then
    AC_LANG_PUSH([C++])
    old_CXXFLAGS="$CXXFLAGS"
    GLAZE_CXXFLAGS=""
    AC_MSG_CHECKING([for usable glaze/glaze.hpp with modern C++ standard flags])
    for glaze_std_flag in -std=c++23 -std=c++2b -std=c++26 -std=c++2c; do
        CXXFLAGS="$old_CXXFLAGS $glaze_std_flag"
        AC_COMPILE_IFELSE(
            [AC_LANG_PROGRAM([[#include <glaze/glaze.hpp>]], [[return 0;]])],
            [GLAZE_CXXFLAGS="$glaze_std_flag"; break],
            []
        )
    done
    CXXFLAGS="$old_CXXFLAGS"
    if test -n "$GLAZE_CXXFLAGS"; then
        AC_MSG_RESULT([yes ($GLAZE_CXXFLAGS)])
        AC_DEFINE([HAVE_GLAZE], [1], [Mark that system has glaze])
        have_glaze=yes
    else
        AC_MSG_RESULT([no])
        AC_DEFINE([HAVE_GLAZE], [0], [Mark that system has glaze])
        if test "$wantalljson" = "yes" || test "$wantglaze" = "yes"; then
            AC_MSG_ERROR([glaze/glaze.hpp not found or not compilable with tested flags -std=c++23, -std=c++2b, -std=c++26, -std=c++2c. Required by --with-all-json=yes or --with-glaze=yes])
        else
            AC_MSG_NOTICE([glaze/glaze.hpp not found or not compilable with tested modern C++ flags. Continuing without glaze.])
        fi
    fi
    AC_LANG_POP([C++])
else
    AC_DEFINE([HAVE_GLAZE], [0], [Mark that user does not want glaze])
    GLAZE_CXXFLAGS=""
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
if test "$wantalljson" = "yes" && test "$wantjsoncons" = "no"; then
    AC_MSG_ERROR([Invalid configuration: --with-all-json=yes requires all JSON libraries to be enabled, but jsoncons was explicitly disabled with --with-jsoncons=no. Remove --with-jsoncons=no, or do not use --with-all-json=yes.])
fi

if test "$wantjsoncons" != "no"; then
    AC_LANG_PUSH([C++])
    AC_CHECK_HEADERS([jsoncons/json.hpp], [
            AC_DEFINE([HAVE_JSONCONS], [1], [Mark that system has jsoncons])
            have_jsoncons=yes
        ],
        [
            AC_DEFINE([HAVE_JSONCONS], [0], [Mark that system has jsoncons])
            if test "$wantalljson" = "yes" || test "$wantjsoncons" = "yes"; then
                AC_MSG_ERROR([jsoncons/json.hpp not found. Required by --with-all-json=yes or --with-jsoncons=yes])
            else
                AC_MSG_NOTICE([jsoncons/json.hpp not found. Continuing without jsoncons.])
            fi
        ]
    )
    AC_LANG_POP([C++])
else
    AC_DEFINE([HAVE_JSONCONS], [0], [Mark that user does not want jsoncons])
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
AC_SUBST(GLAZE_CXXFLAGS)

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

AC_MSG_NOTICE([JSON library summary:])
AC_MSG_NOTICE([  all-json required: $wantalljson])
AC_MSG_NOTICE([  yajl:         $have_yajl])
AC_MSG_NOTICE([  rapidjson:    $have_rapidjson])
AC_MSG_NOTICE([  nlohmannjson: $have_nlohmannjson])
AC_MSG_NOTICE([  jsonc:        $have_jsonc])
AC_MSG_NOTICE([  jansson:      $have_jansson])
AC_MSG_NOTICE([  cjson:        $have_cjson])
AC_MSG_NOTICE([  jsoncpp:      $have_jsoncpp])
AC_MSG_NOTICE([  simdjson:     $have_simdjson])
AC_MSG_NOTICE([  yyjson:       $have_yyjson])
AC_MSG_NOTICE([  glaze:        $have_glaze (GLAZE_CXXFLAGS="$GLAZE_CXXFLAGS")])
AC_MSG_NOTICE([  jsoncons:     $have_jsoncons])

AC_OUTPUT
