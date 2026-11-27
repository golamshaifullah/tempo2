# Native Giza dependency for gplk; no PGPLOT compatibility libraries are used.
AC_DEFUN([TEMPO2_LIB_GIZA], [
  AC_ARG_WITH([giza],
    [AS_HELP_STRING([--with-giza@<:@=PREFIX@:>@],
      [build native Giza gplk (auto-detected by default; --without-giza disables gplk)])],
    [], [with_giza=auto])
  AC_ARG_VAR([GIZA_CFLAGS], [C/C++ compiler flags for native Giza])
  AC_ARG_VAR([GIZA_LIBS], [linker flags for native Giza (normally -lgiza)])
  AC_PATH_TOOL([PKG_CONFIG], [pkg-config], [no])

  have_giza=no
  AS_IF([test "x$with_giza" != xno], [
    AS_CASE([$with_giza],
      [auto|yes], [
        AS_IF([test "x$PKG_CONFIG" != xno && $PKG_CONFIG --exists giza], [
          AS_IF([test -z "$GIZA_CFLAGS"], [GIZA_CFLAGS=`$PKG_CONFIG --cflags giza`])
          AS_IF([test -z "$GIZA_LIBS"], [GIZA_LIBS=`$PKG_CONFIG --libs giza`])
        ])
      ], [
        AS_IF([test -z "$GIZA_CFLAGS"], [GIZA_CFLAGS="-I$with_giza/include"])
        AS_IF([test -z "$GIZA_LIBS"], [GIZA_LIBS="-L$with_giza/lib -lgiza"])
      ])
    AS_IF([test -z "$GIZA_LIBS"], [GIZA_LIBS="-lgiza"])
    giza_save_CPPFLAGS="$CPPFLAGS"
    giza_save_LIBS="$LIBS"
    CPPFLAGS="$CPPFLAGS $GIZA_CFLAGS"
    LIBS="$GIZA_LIBS $LIBS"
    AC_LANG_PUSH([C++])
    AC_MSG_CHECKING([for the native Giza API])
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[
      extern "C" {
      #include <giza.h>
      }
    ]], [[
      giza_open_device("/null", "gplk");
      giza_set_colour_palette(GIZA_COLOUR_PALETTE_PGPLOT);
      giza_axis_float("NTS", 0, 0, 1, 1, 0, 1, 0, 0, 0.5, 0, 0.5, 0, 0);
      giza_close_devices();
    ]])], [have_giza=yes], [have_giza=no])
    AC_MSG_RESULT([$have_giza])
    AC_LANG_POP([C++])
    CPPFLAGS="$giza_save_CPPFLAGS"
    LIBS="$giza_save_LIBS"
    AS_IF([test "x$have_giza" != xyes && test "x$with_giza" != xauto], [
      AC_MSG_ERROR([native Giza requested but not linkable; set GIZA_CFLAGS and GIZA_LIBS or use --with-giza=PREFIX])
    ])
  ])
  AM_CONDITIONAL([HAVE_GIZA], [test "x$have_giza" = xyes])
  # Older Giza X11 cursor loops block until a key/mouse event. Observe window
  # sizes on a separate connection and wake idle input without refitting.
  GPLK_RESIZE_CFLAGS=""
  GPLK_RESIZE_LIBS=""
  AS_IF([test "x$have_giza" = xyes && test "x$acx_pthread_ok" = xyes], [
    giza_save_CPPFLAGS="$CPPFLAGS"
    giza_save_LIBS="$LIBS"
    CPPFLAGS="$CPPFLAGS $PTHREAD_CFLAGS"
    LIBS="-lX11 $PTHREAD_LIBS $LIBS"
    AC_LANG_PUSH([C++])
    AC_MSG_CHECKING([for X11 resize notifications for gplk])
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[
      #include <X11/Xlib.h>
      #include <pthread.h>
      #include <poll.h>
      #include <time.h>
      #include <unistd.h>
      static void *worker(void *p) { return p; }
    ]], [[
      XInitThreads();
      Display *display = XOpenDisplay(0);
      if (display) XCloseDisplay(display);
      pthread_t thread; pthread_create(&thread, 0, worker, 0);
      pthread_join(thread, 0);
      struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
      poll(0, 0, 0);
    ]])], [
      AC_MSG_RESULT([yes])
      AC_DEFINE([HAVE_GPLK_X11_RESIZE], [1],
        [Define if gplk can observe X11 resize events independently of Giza.])
      GPLK_RESIZE_CFLAGS="$PTHREAD_CFLAGS"
      GPLK_RESIZE_LIBS="-lX11 $PTHREAD_LIBS"
    ], [AC_MSG_RESULT([no; use R to redraw after resizing])])
    AC_LANG_POP([C++])
    CPPFLAGS="$giza_save_CPPFLAGS"
    LIBS="$giza_save_LIBS"
  ])
  AC_SUBST([GPLK_RESIZE_CFLAGS])
  AC_SUBST([GPLK_RESIZE_LIBS])
  AC_SUBST([GIZA_CFLAGS])
  AC_SUBST([GIZA_LIBS])
])
