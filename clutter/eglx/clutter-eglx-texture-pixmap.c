/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Johan Bilien  <johan.bilien@nokia.com>
 *             Matthew Allum <mallum@o-hand.com>
 *             Robert Bragg  <bob@o-hand.com>
 *             Kimmo Hamalainen <kimmo.hamalainen@nokia.com>
 *
 * Copyright (C) 2007 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*  TODO:
 *  - Automagically handle named pixmaps, and window resizes (i.e
 *    essentially handle window id's being passed in) ?
*/

/**
 * SECTION:clutter-eglx-texture-pixmap
 * @short_description: A texture which displays the content of an X Pixmap.
 *
 * #ClutterEGLXTexturePixmap is a class for displaying the content of an
 * X Pixmap as a ClutterActor. Used together with the X Composite extension,
 * it allows to display the content of X Windows inside Clutter.
 *
 * The class requires the EGL_EXT_texture_from_pixmap OpenGL extension.
 */

/* Whether to draw a red-bordered window when there is no valid pixmap
 * (If we don't, we just draw nothing) */
#define DEBUG_RED_RECT 0
/* We don't want to print configs in most cases, as this can take around 30ms or so */
#define DEBUG_PRINT_CONFIGS 0

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/extensions/Xcomposite.h>
#include <string.h>

#include "../x11/clutter-x11-texture-pixmap.h"
#include "clutter-eglx-texture-pixmap.h"
#include "clutter-eglx.h"
#include "clutter-backend-egl.h"

#include "../clutter-util.h"
#include "../clutter-debug.h"

#include "../clutter-debug.h"

#include "cogl/cogl.h"

static gboolean          _have_tex_from_pixmap_ext = FALSE;
static gboolean          _ext_check_done = FALSE;

struct _ClutterEGLXTexturePixmapPrivate
{
  EGLSurface    egl_surface;

  gboolean      use_fallback;

  guint         current_pixmap;
  guint         current_pixmap_depth;
  guint         current_pixmap_width;
  guint         current_pixmap_height;

  /* If the pixmap has changed, we'll want to try and recreate the surface */
  gboolean      pixmap_changed;

  gboolean      dispose_called;
};

void
clutter_eglx_texture_pixmap_paint (ClutterActor *actor);

static void
clutter_eglx_texture_pixmap_update_area (ClutterX11TexturePixmap *texture,
                                        gint x,
                                        gint y,
                                        gint width,
                                        gint height);

static EGLConfig
clutter_eglx_get_eglconfig (EGLDisplay *display,
                            EGLSurface *surface, Pixmap pixmap,
                            int depth);

static void
clutter_eglx_texture_pixmap_surface_create (ClutterActor *actor);

static void
clutter_eglx_texture_pixmap_surface_destroy (ClutterActor *actor);

static void
clutter_eglx_texture_pixmap_freeing_pixmap (ClutterEGLXTexturePixmap *self);

static ClutterX11TexturePixmapClass *parent_class = NULL;
static ClutterActorClass *clutter_actor_class = NULL;

G_DEFINE_TYPE_WITH_CODE (ClutterEGLXTexturePixmap,    \
                         clutter_eglx_texture_pixmap, \
                         CLUTTER_X11_TYPE_TEXTURE_PIXMAP,
                         G_ADD_PRIVATE (ClutterEGLXTexturePixmap));

static void
print_config_info (EGLConfig conf)
{
#if DEBUG_PRINT_CONFIGS
  EGLint red = -1, green = -1, blue = -1, alpha = -1, stencil = -1;
  EGLint rgba_bindable = -1, rgb_bindable = -1, tex_target = -1, config_id = -1;

  if (!conf) return;

  eglGetConfigAttrib (clutter_eglx_display (),
		      conf,
		      EGL_RED_SIZE, &red);
  eglGetConfigAttrib (clutter_eglx_display (),
		      conf,
		      EGL_GREEN_SIZE, &green);
  eglGetConfigAttrib (clutter_eglx_display (),
		      conf,
		      EGL_BLUE_SIZE, &blue);
  eglGetConfigAttrib (clutter_eglx_display (),
		      conf,
		      EGL_ALPHA_SIZE, &alpha);
  eglGetConfigAttrib (clutter_eglx_display (),
		      conf,
		      EGL_STENCIL_SIZE, &stencil);
  eglGetConfigAttrib (clutter_eglx_display (),
		      conf,
		      EGL_BIND_TO_TEXTURE_RGB, &rgb_bindable);
  eglGetConfigAttrib (clutter_eglx_display (),
		      conf,
		      EGL_BIND_TO_TEXTURE_RGBA, &rgba_bindable);
  eglGetConfigAttrib (clutter_eglx_display (),
		      conf,
		      EGL_TEXTURE_TARGET, &tex_target);
  eglGetConfigAttrib (clutter_eglx_display (),
		      conf,
		      EGL_CONFIG_ID, &config_id);
  g_debug ("%s: ID:0x%x R:%d G:%d B:%d A:%d S:%d RGB:%d RGBA:%d TEX:%d",
	   __FUNCTION__, config_id,
	   red, green, blue, alpha, stencil,
	   rgb_bindable, rgba_bindable, tex_target);
#endif
}

static void
clutter_eglx_texture_pixmap_init (ClutterEGLXTexturePixmap *self)
{
  ClutterEGLXTexturePixmapPrivate *priv;

  priv = self->priv = clutter_eglx_texture_pixmap_get_instance_private (self);
  priv->egl_surface = EGL_NO_SURFACE;
  priv->current_pixmap = 0;
  priv->current_pixmap_depth = 0;
  priv->current_pixmap_width = 0;
  priv->current_pixmap_height = 0;
  priv->dispose_called = FALSE;

  if (_ext_check_done == FALSE)
    {
      const char *eglx_extensions = NULL;
      ClutterBackendEGL *backend;

      backend = CLUTTER_BACKEND_EGL (clutter_get_default_backend ());
      eglx_extensions = eglQueryString (backend->edpy,
                                        EGL_EXTENSIONS);

      g_debug("%s: checking for texture_from_pixmap", __FUNCTION__);
      /* Check for a texture from pixmap extension.
       * Note: vendor-specific since there is no TFP in EGL specification. */
      if (cogl_check_extension ("EGL_NOKIA_texture_from_pixmap",
                                eglx_extensions))
        {
          g_debug("%s: found EGL_NOKIA_texture_from_pixmap", __FUNCTION__);
          _have_tex_from_pixmap_ext = TRUE;
        }

      _ext_check_done = TRUE;
    }

  priv->use_fallback = !_have_tex_from_pixmap_ext;

  /* We need to know when the pixmap is about to be freed, so we can
   * eglDestroySurface before it's gone */
  g_signal_connect (self, "pixmap-freeing",
                    G_CALLBACK (clutter_eglx_texture_pixmap_freeing_pixmap), 0);
}

static void
clutter_eglx_texture_pixmap_dispose (GObject *object)
{
  ClutterEGLXTexturePixmapPrivate *priv;

  priv = CLUTTER_EGLX_TEXTURE_PIXMAP (object)->priv;

  /* this dispose handler is called twice because of how
   * clutter_actor_destroy works */

  if (priv->egl_surface != EGL_NO_SURFACE)
    clutter_eglx_texture_pixmap_surface_destroy(CLUTTER_ACTOR(object));

  /* Texture deletion is now handled by the CoglTexture */

  G_OBJECT_CLASS (clutter_eglx_texture_pixmap_parent_class)->dispose (object);
}

static gboolean
create_cogl_texture (ClutterTexture *texture,
		     GLuint gl_handle,
		     GLuint width,
		     GLuint height,
		     CoglPixelFormat format)
{
  CoglHandle  handle;

  handle = cogl_texture_new_from_foreign (gl_handle, GL_TEXTURE_2D,
		  			  width, height,
					  0, 0, format);

  if (handle)
    {
      /* Force COGL to take ownership of this texture and destroy it
       * when the CoglTexture is destroyed */
      cogl_texture_set_foreign(handle, FALSE);
      /*g_debug ("%s: created cogl handle %x", __FUNCTION__, (int)handle);*/
      clutter_texture_set_cogl_texture (texture, handle);
      /* unref because clutter_texture_set_cogl_texture ref'd the handle */
      cogl_texture_unref (handle);
      return TRUE;
    }
  else
    g_debug ("%s: cogl_texture_new_from_foreign failed", __FUNCTION__);

  return FALSE;
}

static void
clutter_eglx_texture_pixmap_surface_create (ClutterActor *actor)
{
  ClutterEGLXTexturePixmapPrivate *priv;
  Pixmap                          pixmap;
  Window                          window;
  guint                           pixmap_depth;
  gboolean                        has_alpha;
  CoglPixelFormat                 format;
  ClutterBackendEGL		  *backend;
  GLuint	                  texture_id;

  backend = CLUTTER_BACKEND_EGL (clutter_get_default_backend ());
  priv = CLUTTER_EGLX_TEXTURE_PIXMAP (actor)->priv;

  g_object_get (actor,
        "pixmap", &priv->current_pixmap,
        "pixmap-depth", &priv->current_pixmap_depth,
        "pixmap-width", &priv->current_pixmap_width,
        "pixmap-height", &priv->current_pixmap_height,
        NULL);

  if (priv->use_fallback)
    {
      g_debug ("%s: texture from pixmap appears unsupported", __FUNCTION__);
      CLUTTER_NOTE (TEXTURE, "Falling back to X11 manual mechanism");

      CLUTTER_ACTOR_CLASS (clutter_eglx_texture_pixmap_parent_class)->
        realize (actor);
      return;
    }

  g_object_get (actor,
                "pixmap", &pixmap,
                "window", &window,
                "pixmap-depth", &pixmap_depth,
                NULL);

  if (!pixmap && !window)
    {
      g_warning ("%s: no Pixmap or Window to bind to", __FUNCTION__);
      return;
    }

  has_alpha = pixmap_depth==32;
  if (!clutter_x11_texture_pixmap_get_allow_alpha(
        CLUTTER_X11_TEXTURE_PIXMAP(actor)))
    has_alpha = FALSE;

  if (pixmap)
    {
      EGLConfig conf = clutter_eglx_get_eglconfig (
                                backend->edpy, &priv->egl_surface,
                                pixmap, has_alpha);
      print_config_info (conf);
    }
  else
    {
      EGLConfig conf;

      clutter_x11_trap_x_errors ();
      pixmap = XCompositeNameWindowPixmap (clutter_x11_get_default_display (),
		      			   window);
      if (clutter_x11_untrap_x_errors ())
        {
          g_warning ("%s: XCompositeNameWindowPixmap failed for window %lx",
		   __FUNCTION__, window);
	  return;
	}
      conf = clutter_eglx_get_eglconfig (
                        backend->edpy, &priv->egl_surface,
                        pixmap, has_alpha);
      print_config_info (conf);
    }

  if (priv->egl_surface == EGL_NO_SURFACE)
    {
      g_warning ("%s: error %x, failed to create %s surface for %lx, "
                 "using X11 fallback.",
	       __FUNCTION__, eglGetError (),
	       pixmap ? "pixmap" : "window",
	       pixmap ? pixmap : window);

      priv->use_fallback = TRUE;

      CLUTTER_ACTOR_CLASS (clutter_eglx_texture_pixmap_parent_class)->
        realize (actor);

      return;
    }

  /* bind the surface to a GL texture */
  glGenTextures (1, &texture_id);
  glBindTexture (GL_TEXTURE_2D, texture_id);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  if (glIsTexture (texture_id) == GL_FALSE)
    {
      g_warning ("%s: failed to bind texture", __FUNCTION__);
      return;
    }

  if (!has_alpha)
    {
      if (priv->current_pixmap_depth == 16)
        format = COGL_PIXEL_FORMAT_RGB_565;
      else
        format = COGL_PIXEL_FORMAT_RGB_888;
    }
  else
    {
      g_debug ("%s: surface format is EGL_TEXTURE_RGBA", __FUNCTION__);
      format = COGL_PIXEL_FORMAT_RGBA_8888;
    }

  if (!create_cogl_texture (CLUTTER_TEXTURE (actor), texture_id,
              priv->current_pixmap_width, priv->current_pixmap_height, format))
    {
      g_debug ("%s: Unable to create cogl texture", __FUNCTION__);

      glDeleteTextures (1, &texture_id);

      CLUTTER_NOTE (TEXTURE, "Falling back to X11 manual mechanism");
      priv->use_fallback = TRUE;

      CLUTTER_ACTOR_CLASS (clutter_eglx_texture_pixmap_parent_class)->
        realize (actor);
    }
}

static void
clutter_eglx_texture_pixmap_surface_destroy (ClutterActor *actor)
{
  ClutterEGLXTexturePixmapPrivate *priv;
  ClutterBackendEGL		  *backend;
  Display                         *dpy;

  priv = CLUTTER_EGLX_TEXTURE_PIXMAP (actor)->priv;
  backend = CLUTTER_BACKEND_EGL (clutter_get_default_backend ());
  dpy = clutter_x11_get_default_display ();

  if (priv->egl_surface != EGL_NO_SURFACE)
    {
            /*
      g_debug ("%s: doing eglDestroySurface(disp, %x)",
                __FUNCTION__, (unsigned int)priv->egl_surface);
                */
      clutter_x11_trap_x_errors ();
      eglDestroySurface (backend->edpy, priv->egl_surface);
      XSync (dpy, FALSE);
      if (clutter_x11_untrap_x_errors ())
        g_debug ("%s: X errors", __FUNCTION__);
      priv->egl_surface = EGL_NO_SURFACE;
    }

  /* It looks like we can keep the old texture as clutter
   * will free it anyway if we unrealise or set a new texture  */
}

static void clutter_eglx_texture_pixmap_freeing_pixmap (ClutterEGLXTexturePixmap *self)
{
  ClutterEGLXTexturePixmapPrivate *priv = self->priv;
  /** We need to ensure that we get rid of the surface before the pixmap
   * is XFreePixmap'd. The pixmap will be set to 0 or the new value right
   * after, so we'll be notified to create a new surface if we need to. */
  if (priv->egl_surface != EGL_NO_SURFACE)
    clutter_eglx_texture_pixmap_surface_destroy(CLUTTER_ACTOR(self));
}

static const EGLint pixmap_creation_config_rgb[] = {
        EGL_TEXTURE_TARGET,             EGL_TEXTURE_2D,
        EGL_TEXTURE_FORMAT,             EGL_TEXTURE_RGB,
        EGL_NONE
};

static const EGLint pixmap_creation_config_rgba[] = {
        EGL_TEXTURE_TARGET,             EGL_TEXTURE_2D,
        EGL_TEXTURE_FORMAT,             EGL_TEXTURE_RGBA,
        EGL_NONE
};

static const EGLint pixmap_config[] = {
	EGL_SURFACE_TYPE,		EGL_PIXMAP_BIT,
	EGL_RENDERABLE_TYPE,		EGL_OPENGL_ES2_BIT,
	EGL_DEPTH_SIZE,			0,
	EGL_BIND_TO_TEXTURE_RGB,	EGL_TRUE,
        /*EGL_BUFFER_SIZE,              16,*/
	EGL_NONE
};


static EGLConfig
clutter_eglx_get_eglconfig (EGLDisplay *display,
                            EGLSurface *surface, Pixmap pixmap,
                            gboolean has_alpha)
{
  EGLConfig configs[20];
  int i, nconfigs = 0;
  EGLBoolean ret;

  ret = eglChooseConfig (display, pixmap_config, configs,
                         G_N_ELEMENTS (configs), &nconfigs);

  if (ret != EGL_TRUE)
    {
      g_debug ("%s: eglChooseConfig failed: %x", __FUNCTION__, eglGetError());
      return NULL;
    }
   /*else
     {
       g_debug ("%s: got %d matching configs", __FUNCTION__, nconfigs);
     }*/

  for (i = 0; i < nconfigs; ++i)
    {
      if (has_alpha)
        *surface = eglCreatePixmapSurface (display, configs[i],
                                           (EGLNativePixmapType)pixmap,
                                           pixmap_creation_config_rgba);
      else
        *surface = eglCreatePixmapSurface (display, configs[i],
                                           (EGLNativePixmapType)pixmap,
                                           pixmap_creation_config_rgb);

      if (*surface != EGL_NO_SURFACE)
        return configs[i];

      g_debug ("%s: eglCreatePixmapSurface failed for config:",
               __FUNCTION__);
      print_config_info (configs[i]);
   }

   /* We failed to get any surface */
   return NULL;
}

static void
clutter_eglx_texture_pixmap_update_area (ClutterX11TexturePixmap *texture,
                                         gint                     x,
                                         gint                     y,
                                         gint                     width,
                                         gint                     height)
{
  guint                pixmap, pixmap_depth, pixmap_width, pixmap_height;
  ClutterEGLXTexturePixmapPrivate *priv;


  priv = CLUTTER_EGLX_TEXTURE_PIXMAP (texture)->priv;

  if (priv->use_fallback)
    {
      g_debug ("%s: Falling back to X11", __FUNCTION__);
      parent_class->update_area (texture, x, y, width, height);
      return;
    }

  if (!CLUTTER_ACTOR_IS_REALIZED (texture))
    {
      g_debug ("%s: Not realised, returning", __FUNCTION__);
      return;
    }

  /*g_debug ("%s: Updating texture pixmap %p, %d, %d, %d, %d",
           __FUNCTION__, texture,
           x, y, width, height);*/

  g_object_get (texture,
                "pixmap", &pixmap,
                "pixmap-depth", &pixmap_depth,
                "pixmap-width", &pixmap_width,
                "pixmap-height", &pixmap_height,
                NULL);

  if (pixmap != priv->current_pixmap ||
      pixmap_depth != priv->current_pixmap_depth ||
      pixmap_width != priv->current_pixmap_width ||
      pixmap_height != priv->current_pixmap_height ||
      priv->egl_surface == EGL_NO_SURFACE)
    {
      priv->pixmap_changed = TRUE;
    }

  if (/*priv->egl_surface != EGL_NO_SURFACE
      && */CLUTTER_ACTOR_IS_VISIBLE (CLUTTER_ACTOR (texture)))
    clutter_actor_queue_redraw (CLUTTER_ACTOR (texture));
}

static void
clutter_eglx_texture_pixmap_realize (ClutterActor *actor)
{
  CLUTTER_ACTOR_CLASS (clutter_actor_class)->realize (actor);

  CLUTTER_ACTOR_SET_FLAGS (actor, CLUTTER_ACTOR_REALIZED);
}

static void
clutter_eglx_texture_pixmap_class_init (ClutterEGLXTexturePixmapClass *klass)
{
  GObjectClass                 *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass            *actor_class = CLUTTER_ACTOR_CLASS (klass);

  ClutterX11TexturePixmapClass *x11_texture_class =
      CLUTTER_X11_TEXTURE_PIXMAP_CLASS (klass);

  parent_class = g_type_class_peek_parent(klass);
  clutter_actor_class = g_type_class_peek_parent(parent_class);

  object_class->dispose = clutter_eglx_texture_pixmap_dispose;

  actor_class->paint = clutter_eglx_texture_pixmap_paint;
  actor_class->realize = clutter_eglx_texture_pixmap_realize;

  x11_texture_class->update_area = clutter_eglx_texture_pixmap_update_area;
}

/**
 * clutter_eglx_texture_pixmap_using_extension:
 * @texture: A #ClutterEGLXTexturePixmap
 *
 * Return value: A boolean indicating if the texture is using the
 * EGL_EXT_texture_from_pixmap OpenGL extension or falling back to
 * slower software mechanism.
 *
 * Since: 0.8
 **/
gboolean
clutter_eglx_texture_pixmap_using_extension (ClutterEGLXTexturePixmap *texture)
{
  return _have_tex_from_pixmap_ext;
  /* Assume NPOT TFP's are supported even if regular NPOT isn't advertised
   * but tfp is. Seemingly some Intel drivers do this ?
  */
  /* && clutter_feature_available (COGL_FEATURE_TEXTURE_NPOT)); */
}

/**
 * clutter_eglx_texture_pixmap_new_with_pixmap:
 * @pixmap: the X Pixmap to which this texture should be bound
 *
 * Return value: A new #ClutterEGLXTexturePixmap bound to the given X Pixmap
 *
 * Since: 0.8
 **/
ClutterActor*
clutter_eglx_texture_pixmap_new_with_pixmap (Pixmap pixmap)
{
  ClutterActor *actor;

  actor = g_object_new (CLUTTER_EGLX_TYPE_TEXTURE_PIXMAP,
                        "pixmap", pixmap,
                        NULL);

  return actor;
}

/**
 * clutter_eglx_texture_pixmap_new_with_window:
 * @window: the X window to which this texture should be bound
 *
 * Return value: A new #ClutterEGLXTexturePixmap bound to the given X window
 *
 * Since: 0.8
 **/
ClutterActor*
clutter_eglx_texture_pixmap_new_with_window (Window window)
{
  ClutterActor *actor;

  actor = g_object_new (CLUTTER_EGLX_TYPE_TEXTURE_PIXMAP,
                        "window", window,
                        NULL);

  return actor;
}

/**
 * clutter_eglx_texture_pixmap_new:
 *
 * Return value: A new #ClutterEGLXTexturePixmap
 *
 * Since: 0.8
 **/
ClutterActor *
clutter_eglx_texture_pixmap_new (void)
{
  ClutterActor *actor;

  actor = g_object_new (CLUTTER_EGLX_TYPE_TEXTURE_PIXMAP, NULL);

  return actor;
}

#if DEBUG_RED_RECT
static void
clutter_eglx_texture_pixmap_paint_fail (ClutterActor *actor)
{
  ClutterGeometry          geom;
  ClutterColor             col_black = {0,0,0,255};
  ClutterColor             col_red = {255,0,0,255};
  guint                    w,h;

  clutter_actor_get_allocation_geometry (actor, &geom);
  w = geom.width;
  h = geom.height;

  col_black.alpha = col_red.alpha = clutter_actor_get_paint_opacity (actor);

  /* red border on black rectangle */
  cogl_color (&col_black);
  cogl_rectangle (10, 10, w-10, h-10);
  cogl_color (&col_red);
  cogl_rectangle (0, 0, w, 10);
  cogl_rectangle (0, h-10, w, 10);
  cogl_rectangle (0, 10, 10, h-20);
  cogl_rectangle (w-10, 10, 10, h-20);
}
#endif //DEBUG_RED_RECT

void
clutter_eglx_texture_pixmap_paint (ClutterActor *actor)
{
  guint         pixmap, pixmap_depth, pixmap_width, pixmap_height;
  ClutterEGLXTexturePixmapPrivate       *priv;
  int do_release = 1;
  GLuint texture_id;
  CoglHandle handle;

  priv = CLUTTER_EGLX_TEXTURE_PIXMAP (actor)->priv;

  if (priv->pixmap_changed) {
    priv->pixmap_changed = FALSE;
    g_debug ("%s: Pixmap has changed, destroying surface", __FUNCTION__);
    clutter_eglx_texture_pixmap_surface_destroy(actor);
    clutter_eglx_texture_pixmap_surface_create(actor);
  }

  if (priv->use_fallback)
    {
      CLUTTER_ACTOR_CLASS(clutter_actor_class)->paint(actor);
      return;
    }

  if (!CLUTTER_ACTOR_IS_REALIZED (actor))
    {
      g_debug ("%s: Not realised, returning", __FUNCTION__);
      return;
    }

  g_object_get (actor,
                "pixmap", &pixmap,
                "pixmap-depth", &pixmap_depth,
                "pixmap-width", &pixmap_width,
                "pixmap-height", &pixmap_height,
                NULL);

  handle = clutter_texture_get_cogl_texture(CLUTTER_TEXTURE(actor));

  if (priv->egl_surface == EGL_NO_SURFACE ||
      handle == COGL_INVALID_HANDLE)
    {
      /*
      guint         window;
      const gchar *name = clutter_actor_get_name(actor);
      g_object_get (actor, "window", &window, NULL);
      g_debug ("%s: Buffer not created "
               "(name '%s', pixmap %d, window 0x%x, width %d, height %d, depth %d)",
               __FUNCTION__,
               name ? name : "null",
               pixmap, window, pixmap_width, pixmap_height, pixmap_depth);*/

#if DEBUG_RED_RECT
      clutter_eglx_texture_pixmap_paint_fail (actor);
#endif //DEBUG_RED_RECT
      return;
    }

  if ((pixmap != priv->current_pixmap ||
       pixmap_depth != priv->current_pixmap_depth ||
       pixmap_width != priv->current_pixmap_width ||
       pixmap_height != priv->current_pixmap_height) &&
       priv->egl_surface != EGL_NO_SURFACE)
    {
      g_warning ("%s: Pixmap has changed but update not called, returning",
                __FUNCTION__);
      return;
    }

  clutter_x11_trap_x_errors ();
  XSync (clutter_x11_get_default_display (), FALSE);

  if (!handle || !cogl_texture_get_gl_texture (handle, &texture_id, NULL))
    return;

  glEnable (GL_TEXTURE_2D);
  glBindTexture (GL_TEXTURE_2D, texture_id);

  if (eglBindTexImage (clutter_eglx_display (),
                       priv->egl_surface,
                       EGL_BACK_BUFFER) == EGL_FALSE)
    {
      g_debug ("%s: eglBindTexImage(disp, %x) failed (tex %x): %x",
               __FUNCTION__, (unsigned int)priv->egl_surface,
               texture_id, eglGetError ());
      do_release = 0;
    }

  if (clutter_x11_untrap_x_errors ())
    g_debug ("%s: X errors", __FUNCTION__);

  CLUTTER_ACTOR_CLASS(clutter_actor_class)->paint(actor);

  if (do_release &&
      eglReleaseTexImage (clutter_eglx_display (), priv->egl_surface,
                        EGL_BACK_BUFFER) == EGL_FALSE)
    {
      g_debug ("%s: eglReleaseTexImage(disp, %x) failed: %x", __FUNCTION__,
                (unsigned int)priv->egl_surface, eglGetError ());
    }
}
