/*
 * Clutter COGL
 *
 * A basic GL/GLES Abstraction/Utility Layer
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cogl.h"
#include "cogl-internal.h"
#include "cogl-bitmap.h"

#include <string.h>


/* we really want everything inlined here, or we'll be insanely slow */

/* TO rgba */

inline static void
_cogl_swap_r_b (guchar *dst)
{
  guchar tmp = dst[0];
  dst[0] = dst[2];
  dst[2] = tmp;
}

inline static void
_cogl_g_to_rgba (const guchar *src, guchar *dst)
{
  dst[0] = src[0];
  dst[1] = src[0];
  dst[2] = src[0];
  dst[3] = 255;
}

inline static void
_cogl_a_to_rgba (const guchar *src, guchar *dst)
{
  dst[0] = 255;
  dst[1] = 255;
  dst[2] = 255;
  dst[3] = src[0];
}

inline static void
_cogl_rgb_to_rgba (const guchar *src, guchar *dst)
{
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
  dst[3] = 255;
}

inline static void
_cogl_xrgb_to_rgba (const guchar *src, guchar *dst)
{
  dst[0] = src[1];
  dst[1] = src[2];
  dst[2] = src[3];
  dst[3] = 255;
}

inline static void
_cogl_rgbx_to_rgba (const guchar *src, guchar *dst)
{
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
  dst[3] = 255;
}

inline static void
_cogl_argb_to_rgba (const guchar *src, guchar *dst)
{
  dst[0] = src[1];
  dst[1] = src[2];
  dst[2] = src[3];
  dst[3] = src[0];
}

inline static void
_cogl_rgba_to_rgba (const guchar *src, guchar *dst)
{
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
  dst[3] = src[3];
}

inline static void
_cogl_rgb565_to_rgba (const guchar *src, guchar *dst)
{
  gushort *c = (gushort*)src;
  dst[0] = (((*c >> 8) & 0xf8) | ((*c >> 13) & 0x7));
  dst[1] = (((*c >> 3) & 0xfc) | ((*c >> 9) & 0x3));
  dst[2] = (((*c << 3) & 0xf8) | ((*c >> 2) & 0x7));
  dst[3] = 255;
}

inline static void
_cogl_rgba5551_to_rgba (const guchar *src, guchar *dst)
{
  gushort *c = (gushort*)src;
  dst[0] = (((*c >> 8) & 0xf8) | ((*c >> 13) & 0x7));
  dst[1] = (((*c >> 3) & 0xf8) | ((*c >> 8) & 0x7));
  dst[2] = (((*c << 2) & 0xf8) | ((*c >> 3) & 0x7));
  dst[3] = (*c & 0x0001) ? 255 : 0;
}

inline static void
_cogl_rgba4444_to_rgba (const guchar *src, guchar *dst)
{
  gushort *c = (gushort*)src;
  dst[0] = (((*c >> 8) & 0xf0) | ((*c >> 12) & 0xF));
  dst[1] = (((*c >> 4) & 0xf0) | ((*c >> 8) & 0xF));
  dst[2] = (((*c     ) & 0xf0) | ((*c >> 4) & 0xF));
  dst[3] = (((*c << 4) & 0xf0) | ((*c     ) & 0xF));
}

/* FROM rgba */

inline static void
_cogl_rgba_to_g (const guchar *src, guchar *dst)
{
  dst[0] = (src[0] + src[1] + src[2]) / 3;
}

inline static void
_cogl_rgba_to_a (const guchar *src, guchar *dst)
{
  dst[0] = src[3];
}

inline static void
_cogl_rgba_to_rgb (const guchar *src, guchar *dst)
{
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
}

inline static void
_cogl_rgba_to_xrgb (const guchar *src, guchar *dst)
{
  dst[0] = 0;
  dst[1] = src[0];
  dst[2] = src[1];
  dst[3] = src[2];
}

inline static void
_cogl_rgba_to_rgbx (const guchar *src, guchar *dst)
{
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
  dst[3] = 0;
}

inline static void
_cogl_rgba_to_argb (const guchar *src, guchar *dst)
{
  dst[0] = src[3];
  dst[1] = src[0];
  dst[2] = src[1];
  dst[3] = src[2];
}

inline static void
_cogl_rgba_to_rgb565 (const guchar *src, guchar *dst)
{
  gushort *c = (gushort*)dst;
  *c = ((src[2]       ) >> 3) |
       ((src[1] & 0xFC) << 3) |
       ((src[0] & 0xF8) << 8);
}

inline static void
_cogl_rgba_to_rgba5551 (const guchar *src, guchar *dst)
{
  gushort *c = (gushort*)dst;
  *c = ((src[3] & 0xF0) >> 7) |
       ((src[2] & 0xF8) >> 2) |
       ((src[1] & 0xF8) << 3) |
       ((src[0] & 0xF8) << 8);
}

inline static void
_cogl_rgba_to_rgba4444 (const guchar *src, guchar *dst)
{
  gushort *c = (gushort*)dst;
  *c = ((src[3]       ) >> 4) |
       ((src[2] & 0xF0)     ) |
       ((src[1] & 0xF0) << 4) |
       ((src[0] & 0xF0) << 8);
}

/* (Un)Premultiplication */

inline static void
_cogl_unpremult_alpha_0 (const guchar *src, guchar *dst)
{
  dst[0] = 0;
  dst[1] = 0;
  dst[2] = 0;
  dst[3] = 0;
}

inline static void
_cogl_unpremult_alpha_last (const guchar *src, guchar *dst)
{
  guchar alpha = src[3];

  dst[0] = ((((gulong) src[0] >> 16) & 0xff) * 255 ) / alpha;
  dst[1] = ((((gulong) src[1] >> 8) & 0xff) * 255 ) / alpha;
  dst[2] = ((((gulong) src[2] >> 0) & 0xff) * 255 ) / alpha;
  dst[3] = alpha;
}

inline static void
_cogl_unpremult_alpha_first (const guchar *src, guchar *dst)
{
  guchar alpha = src[0];

  dst[0] = alpha;
  dst[1] = ((((gulong) src[1] >> 16) & 0xff) * 255 ) / alpha;
  dst[2] = ((((gulong) src[2] >> 8) & 0xff) * 255 ) / alpha;
  dst[3] = ((((gulong) src[3] >> 0) & 0xff) * 255 ) / alpha;
}

gboolean
_cogl_bitmap_fallback_can_unpremult (CoglPixelFormat format)
{
  return ((format & COGL_UNORDERED_MASK) == COGL_PIXEL_FORMAT_32);
}

gboolean
_cogl_bitmap_fallback_convert (const CoglBitmap *bmp,
			       CoglBitmap       *dst_bmp,
			       CoglPixelFormat   dst_format)
{
  guchar  *src;
  guchar  *dst;
  gint     src_bpp;
  gint     dst_bpp;
  gint     x,y;
  guchar   temp_rgba[4] = {0,0,0,0};

  src_bpp = _cogl_get_format_bpp (bmp->format);
  dst_bpp = _cogl_get_format_bpp (dst_format);

  /* Initialize destination bitmap */
  *dst_bmp = *bmp;
  dst_bmp->rowstride = sizeof(guchar) * dst_bpp * dst_bmp->width;
  dst_bmp->format = ((bmp->format & COGL_PREMULT_BIT) |
                     (dst_format & COGL_UNPREMULT_MASK));

  /* Allocate a new buffer to hold converted data */
  dst_bmp->data = g_malloc (sizeof(guchar)
                            * dst_bmp->height
                            * dst_bmp->rowstride);
  /* This preprocessor hack is able to generate all the fast conversion
   * code we need */

#define TRY_CONVERSION(TFROM, TTO, TSWAP, FMT_DST)                    \
  if (FMT_DST == dst_format)                                          \
    {                                                                 \
      for (y = 0; y < bmp->height; y++)                               \
        {                                                             \
          src = (guchar*)bmp->data      + y * bmp->rowstride;         \
          dst = (guchar*)dst_bmp->data  + y * dst_bmp->rowstride;     \
                                                                      \
          for (x = 0; x < bmp->width; x++)                            \
            {                                                         \
              _cogl_##TFROM##_to_rgba (src, temp_rgba);               \
                                                                      \
              if (TSWAP)                                              \
                _cogl_swap_r_b(temp_rgba);                            \
                                                                      \
              _cogl_rgba_to_##TTO (temp_rgba, dst);                   \
                                                                      \
              src += src_bpp;                                         \
              dst += dst_bpp;                                         \
            }                                                         \
        }                                                             \
      return TRUE;                                                    \
    }

#define TRY_CONVERSIONS(TFROM, SWAP) \
  {                              \
  TRY_CONVERSION(TFROM, a, SWAP, COGL_PIXEL_FORMAT_A_8) \
  TRY_CONVERSION(TFROM, g, SWAP, COGL_PIXEL_FORMAT_G_8) \
  TRY_CONVERSION(TFROM, rgb565, SWAP, COGL_PIXEL_FORMAT_RGB_565)  \
  TRY_CONVERSION(TFROM, rgb565, !SWAP, COGL_PIXEL_FORMAT_BGR_565)  \
  TRY_CONVERSION(TFROM, rgba5551, SWAP, COGL_PIXEL_FORMAT_RGBA_5551)  \
  TRY_CONVERSION(TFROM, rgba5551, !SWAP, COGL_PIXEL_FORMAT_BGRA_5551)  \
  TRY_CONVERSION(TFROM, rgba4444, SWAP, COGL_PIXEL_FORMAT_RGBA_4444)  \
  TRY_CONVERSION(TFROM, rgba4444, !SWAP, COGL_PIXEL_FORMAT_BGRA_4444)  \
  TRY_CONVERSION(TFROM, rgb, SWAP, COGL_PIXEL_FORMAT_RGB_888)  \
  TRY_CONVERSION(TFROM, rgb, !SWAP, COGL_PIXEL_FORMAT_BGR_888)  \
  TRY_CONVERSION(TFROM, rgbx, SWAP, COGL_PIXEL_FORMAT_RGBX_8888)  \
  TRY_CONVERSION(TFROM, rgbx, !SWAP, COGL_PIXEL_FORMAT_BGRX_8888)  \
  TRY_CONVERSION(TFROM, xrgb, SWAP, COGL_PIXEL_FORMAT_XRGB_8888)  \
  TRY_CONVERSION(TFROM, xrgb, !SWAP, COGL_PIXEL_FORMAT_XBGR_8888)  \
  TRY_CONVERSION(TFROM, rgba, SWAP, COGL_PIXEL_FORMAT_RGBA_8888)  \
  TRY_CONVERSION(TFROM, rgba, !SWAP, COGL_PIXEL_FORMAT_BGRA_8888)  \
  TRY_CONVERSION(TFROM, argb, SWAP, COGL_PIXEL_FORMAT_ARGB_8888)  \
  TRY_CONVERSION(TFROM, argb, !SWAP, COGL_PIXEL_FORMAT_ABGR_8888)  \
  }

  switch (bmp->format & COGL_UNPREMULT_MASK)
  {
  case COGL_PIXEL_FORMAT_A_8:
      TRY_CONVERSIONS(a, FALSE); break;
  case COGL_PIXEL_FORMAT_G_8:
    TRY_CONVERSIONS(g, FALSE); break;
  case COGL_PIXEL_FORMAT_RGB_565:
    TRY_CONVERSIONS(rgb565, FALSE); break;
  case COGL_PIXEL_FORMAT_BGR_565:
    TRY_CONVERSIONS(rgb565, TRUE); break;
  case COGL_PIXEL_FORMAT_RGBA_5551:
    TRY_CONVERSIONS(rgba5551, FALSE); break;
  case COGL_PIXEL_FORMAT_BGRA_5551:
    TRY_CONVERSIONS(rgba5551, TRUE); break;
  case COGL_PIXEL_FORMAT_RGBA_4444:
    TRY_CONVERSIONS(rgba4444, FALSE); break;
  case COGL_PIXEL_FORMAT_BGRA_4444:
    TRY_CONVERSIONS(rgba4444, TRUE); break;
  case COGL_PIXEL_FORMAT_RGB_888:
    TRY_CONVERSIONS(rgb, FALSE); break;
  case COGL_PIXEL_FORMAT_BGR_888:
    TRY_CONVERSIONS(rgb, TRUE); break;
  case COGL_PIXEL_FORMAT_RGBX_8888:
    TRY_CONVERSIONS(rgbx, FALSE); break;
  case COGL_PIXEL_FORMAT_BGRX_8888:
    TRY_CONVERSIONS(rgbx, TRUE); break;
  case COGL_PIXEL_FORMAT_XRGB_8888:
    TRY_CONVERSIONS(xrgb, FALSE); break;
  case COGL_PIXEL_FORMAT_XBGR_8888:
    TRY_CONVERSIONS(xrgb, TRUE); break;
  case COGL_PIXEL_FORMAT_RGBA_8888:
    TRY_CONVERSIONS(rgba, FALSE); break;
  case COGL_PIXEL_FORMAT_BGRA_8888:
    TRY_CONVERSIONS(rgba, TRUE); break;
  case COGL_PIXEL_FORMAT_ARGB_8888:
    TRY_CONVERSIONS(argb, FALSE); break;
  case COGL_PIXEL_FORMAT_ABGR_8888:
    TRY_CONVERSIONS(argb, TRUE); break;
  default:
    break;
  }

#undef TRY_CONVERSIONS
#undef TRY_CONVERSION

  g_free(dst_bmp->data);
  dst_bmp->data = 0;
  /* We can't do anything we haven't got here */
  return FALSE;
}

gboolean
_cogl_bitmap_fallback_unpremult (const CoglBitmap *bmp,
				 CoglBitmap       *dst_bmp)
{
  guchar  *src;
  guchar  *dst;
  gint     bpp;
  gint     x,y;

  /* Make sure format supported for un-premultiplication */
  if (!_cogl_bitmap_fallback_can_unpremult (bmp->format))
    return FALSE;

  bpp = _cogl_get_format_bpp (bmp->format);

  /* Initialize destination bitmap */
  *dst_bmp = *bmp;
  dst_bmp->format = (bmp->format & COGL_UNPREMULT_MASK);

  /* Allocate a new buffer to hold converted data */
  dst_bmp->data = g_malloc (sizeof(guchar)
			    * dst_bmp->height
			    * dst_bmp->rowstride);

  /* FIXME: Optimize */
  for (y = 0; y < bmp->height; y++)
    {
      src = (guchar*)bmp->data      + y * bmp->rowstride;
      dst = (guchar*)dst_bmp->data  + y * dst_bmp->rowstride;

      for (x = 0; x < bmp->width; x++)
	{
	  /* FIXME: Would be nice to at least remove this inner
           * branching, but not sure it can be done without
           * rewriting of the whole loop */
	  if (bmp->format & COGL_AFIRST_BIT)
	    {
	      if (src[0] == 0)
		_cogl_unpremult_alpha_0 (src, dst);
	      else
		_cogl_unpremult_alpha_first (src, dst);
	    }
	  else
	    {
	      if (src[3] == 0)
		_cogl_unpremult_alpha_0 (src, dst);
	      else
		_cogl_unpremult_alpha_last (src, dst);
	    }

	  src += bpp;
	  dst += bpp;
	}
    }

  return TRUE;
}

gboolean
_cogl_bitmap_fallback_from_file (CoglBitmap  *bmp,
				 const gchar *filename)
{
  /* FIXME: use jpeglib, libpng, etc. manually maybe */
  return FALSE;
}
