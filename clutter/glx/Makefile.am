libclutter_glx_includedir = $(includedir)/clutter-@CLUTTER_API_VERSION@/clutter/glx
libclutter_glx_include_HEADERS = clutter-glx.h clutter-glx-texture-pixmap.h

AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"ClutterGLX\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/clutter \
	-I$(top_srcdir)/clutter/x11 \
	-I$(top_builddir)/clutter \
	$(CLUTTER_CFLAGS) \
	$(CLUTTER_DEBUG_CFLAGS) \
	$(GCC_FLAGS)

LDADD = $(CLUTTER_LIBS) $(top_builddir)/clutter/x11/libclutter-x11.la

noinst_LTLIBRARIES = libclutter-glx.la

libclutter_glx_la_DEPENDENCIES = \
	$(top_builddir)/clutter/x11/libclutter-x11.la

libclutter_glx_la_SOURCES = \
	clutter-backend-glx.h \
	clutter-backend-glx.c \
	clutter-stage-glx.h \
	clutter-stage-glx.c \
        clutter-glx-texture-pixmap.h \
        clutter-glx-texture-pixmap.c \
	clutter-glx.h
