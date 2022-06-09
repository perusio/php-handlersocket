dnl config.m4 for extension handlersocket

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl Check PHP version:

AC_MSG_CHECKING([PHP version])
tmp_version=$PHP_VERSION
if test -z "$tmp_version"; then
  if test -z "$PHP_CONFIG"; then
    AC_MSG_ERROR([php-config not found])
  fi
  php_version=`$PHP_CONFIG --version 2> /dev/null | head -n 1 | sed -e 's#\([0-9]\.[0-9]*\.[0-9]*\)\(.*\)#\1#'`
else
  php_version=`echo "$PHP_VERSION" | sed -e 's#\([0-9]\.[0-9]*\.[0-9]*\)\(.*\)#\1#'`
fi

if test -z "$php_version"; then
  AC_MSG_ERROR([failed to detect PHP version, please report])
fi

ac_IFS=$IFS
IFS="."
set $php_version
IFS=$ac_IFS
hs_php_version=`expr [$]1 \* 1000000 + [$]2 \* 1000 + [$]3`

if test "$hs_php_version" -le "5000000"; then
  AC_MSG_ERROR([You need at least PHP 5.0.0 to be able to use this version of HandlerSocket. PHP $php_version found])
else
  AC_MSG_RESULT([$php_version, ok])
fi

dnl If your extension references something external, use with:

PHP_ARG_WITH(handlersocket, for handlersocket support,
Make sure that the comment is aligned:
[  --with-handlersocket                 Include handlersocket support])

if test "$PHP_HANDLERSOCKET" != "no"; then

  dnl Check use hsclient

  PHP_ARG_ENABLE(handlersocket-hsclient, whether to enable handlersocket hsclient,
  [  --disable-handlersocket-hsclient      Disable hsclient], yes, no)

  if test "$PHP_HANDLERSOCKET_HSCLIENT" != "no"; then

    dnl compiler C++:

    PHP_REQUIRE_CXX()

    dnl HandlerSocket include dir

    PHP_ARG_WITH(handlersocket-includedir, for handlersocket header,
    [  --with-handlersocket-includedir=DIR  handlersocket header files], yes)

    if test "$PHP_HANDLERSOCKET_INCLUDEDIR" != "no" && test "$PHP_HANDLERSOCKET_INCLUDEDIR" != "yes"; then
      if test -r "$PHP_HANDLERSOCKET_INCLUDEDIR/hstcpcli.hpp"; then
        HANDLERSOCKET_DIR="$PHP_HANDLERSOCKET_INCLUDEDIR"
      else
        AC_MSG_ERROR([Can't find handlersocket headers under "$PHP_HANDLERSOCKET_INCLUDEDIR"])
      fi
    else
      SEARCH_PATH="/usr/local /usr"
      SEARCH_FOR="/include/handlersocket/hstcpcli.hpp"
      if test -r $PHP_HANDLERSOCKET/$SEARCH_FOR; then
        HANDLERSOCKET_DIR="$PHP_HANDLERSOCKET/include/handlersocket"
      else
        AC_MSG_CHECKING([for hsclient files in default path])
        for i in $SEARCH_PATH ; do
          if test -r $i/$SEARCH_FOR; then
            HANDLERSOCKET_DIR="$i/include/handlersocket"
            AC_MSG_RESULT(found in $i)
          fi
        done
      fi
    fi

    if test -z "$HANDLERSOCKET_DIR"; then
      AC_MSG_RESULT([not found])
      AC_MSG_ERROR([Can't find hsclient headers])
    fi

    PHP_ADD_INCLUDE($HANDLERSOCKET_DIR)

    dnl Check for lib

    LIBNAME=hsclient
    AC_MSG_CHECKING([for hsclient])
    AC_LANG_SAVE
    AC_LANG_CPLUSPLUS
    AC_TRY_COMPILE(
    [
      #include "$HANDLERSOCKET_DIR/hstcpcli.hpp"
    ],[
      dena::hstcpcli_ptr cli;
    ],[
      AC_MSG_RESULT(yes)
      PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $HANDLERSOCKET_DIR/lib, HANDLERSOCKET_SHARED_LIBADD)
      AC_DEFINE(HAVE_HANDLERSOCKETLIB,1,[ ])
    ],[
      AC_MSG_RESULT([error])
      AC_MSG_ERROR([wrong hsclient lib version or lib not found : $HANDLERSOCKET_DIR])
    ])
    AC_LANG_RESTORE

    dnl Check for stdc++
    LIBNAME=stdc++
    AC_MSG_CHECKING([for stdc++])
    AC_LANG_SAVE
    AC_LANG_CPLUSPLUS
    AC_TRY_COMPILE(
    [
      #include <string>
      using namespace std;
    ],[
      string dummy;
    ],[
      AC_MSG_RESULT(yes)
      PHP_ADD_LIBRARY($LIBNAME, , HANDLERSOCKET_SHARED_LIBADD)
    ],[
      AC_MSG_ERROR([wrong stdc++ lib version or lib not found])
    ])
    AC_LANG_RESTORE

    PHP_SUBST(HANDLERSOCKET_SHARED_LIBADD)

    dnl Extension files (hsclient)

    ifdef([PHP_INSTALL_HEADERS],
    [
      PHP_INSTALL_HEADERS([ext/handlersocket], [php_handlersocket.h])
    ], [
      PHP_ADD_MAKEFILE_FRAGMENT
    ])

    PHP_NEW_EXTENSION(handlersocket, handlersocket.cc, $ext_shared)
  else

    dnl Extension files (native)

    ifdef([PHP_INSTALL_HEADERS],
    [
      PHP_INSTALL_HEADERS([ext/handlersocket], [php_handlersocket.h])
    ], [
      PHP_ADD_MAKEFILE_FRAGMENT
    ])

    PHP_NEW_EXTENSION(handlersocket, handlersocket.c, $ext_shared)
  fi
fi

dnl debug:

PHP_ARG_ENABLE(handlersocket-debug, whether to enable handlersocket debug mode,
[  --enable-handlersocket-debug      Enable handlersocket debug mode], no, no)

if test "$PHP_HANDLERSOCKET_DEBUG" != "no"; then
   AC_DEFINE(HAVE_DEBUG, 1, [Whether handlersocket debug mode is enabled])
fi
