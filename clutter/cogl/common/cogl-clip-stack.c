/*
 * Clutter COGL
 *
 * A basic GL/GLES Abstraction/Utility Layer
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2008 OpenedHand
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
#include "cogl-clip-stack.h"
#include "cogl-util.h"
#include "cogl-internal.h"

#include <stdlib.h>
#include <math.h>

/* These are defined in the particular backend (float in GL vs fixed
   in GL ES) */
void _cogl_set_clip_planes (ClutterFixed x,
			    ClutterFixed y,
			    ClutterFixed width,
			    ClutterFixed height);
void _cogl_init_stencil_buffer (void);
void _cogl_add_stencil_clip (ClutterFixed x,
			     ClutterFixed y,
			     ClutterFixed width,
			     ClutterFixed height,
			     gboolean     first);
void _cogl_disable_clip_planes (void);
void _cogl_disable_stencil_buffer (void);
void _cogl_set_matrix_f (const float *matrix);

typedef struct _CoglClipStackEntry CoglClipStackEntry;

struct _CoglClipStackEntry
{
  /* If this is set then this entry clears the clip stack. This is
     used to clear the stack when drawing an FBO put to keep the
     entries so they can be restored when the FBO drawing is
     completed */
  gboolean            clear;

  /* The rectangle for this clip */
  ClutterFixed        x_offset;
  ClutterFixed        y_offset;
  ClutterFixed        width;
  ClutterFixed        height;

  /* The matrix that was current when the clip was set */
  float               matrix[16];
  ClutterFixed        viewport[4];

  /* this is used as a shortcut if the clipping region is rectangular */
  gboolean            is_rectangular;
  gint                scr_x_1;
  gint                scr_y_1;
  gint                scr_x_2;
  gint                scr_y_2;
};

static GList *cogl_clip_stack_top = NULL;
static int    cogl_clip_stack_depth = 0;

gboolean _cogl_clip_stack_scissor_rebuild()
{
  GList *node;
  gint x_1, y_1, x_2, y_2;
  ClutterFixed fviewport[4];
  gint viewport[4];
  gint i;

  /* go through all elements and check they are rectangular. Return if
     they're not */
  for (node = cogl_clip_stack_top; node != NULL; node = node->next)
    {
      const CoglClipStackEntry *entry = (CoglClipStackEntry *) node->data;
      if (!entry->is_rectangular)
        return FALSE;
    }
  /* start off with a rectangle representing the screen, and clip it down
   * by each successive rectangle */
   cogl_get_viewport( fviewport );
   for (i=0;i<4;i++)
        viewport[i] = CLUTTER_FIXED_TO_INT(fviewport[i]);
   x_1 = viewport[0];
   y_1 = viewport[1];
   x_2 = viewport[0] + viewport[2];
   y_2 = viewport[1] + viewport[3];

   for (node = cogl_clip_stack_top; node != NULL; node = node->next)
    {
      const CoglClipStackEntry *entry = (CoglClipStackEntry *) node->data;
      if (entry->scr_x_1 > x_1) x_1 = entry->scr_x_1;
      if (entry->scr_x_2 < x_2) x_2 = entry->scr_x_2;
      if (entry->scr_y_1 > y_1) y_1 = entry->scr_y_1;
      if (entry->scr_y_2 < y_2) y_2 = entry->scr_y_2;
    }
   /* clip negative values... */
   if (x_2 < x_1) x_2 = x_1;
   if (y_2 < y_1) y_2 = y_1;
   /* set scissoring */
   if ((x_2-x_1)!=viewport[2] || (y_2-y_1)!=viewport[3])
     {
       GE( glEnable( GL_SCISSOR_TEST ) );
       GE( glScissor( x_1-viewport[0], y_1-viewport[1], x_2-x_1, y_2-y_1 ) );
     }
   else
     {
       GE( glScissor( 0, 0, viewport[2], viewport[3] ) );
       GE( glDisable( GL_SCISSOR_TEST ) );
     }

  // disable in case stencilling/clip planes got turned on...
  _cogl_disable_clip_planes ();
  _cogl_disable_stencil_buffer ();

  return TRUE;
}

static void
_cogl_clip_stack_add (const CoglClipStackEntry *entry, int depth)
{
  int has_clip_planes = cogl_features_available (COGL_FEATURE_FOUR_CLIP_PLANES);
  int stencil_bits;

  /* if we can do it all with scissoring, then just do that and return */
  if (_cogl_clip_stack_scissor_rebuild())
    return;

  glGetIntegerv(GL_STENCIL_BITS, &stencil_bits);

  /* If this is the first entry and we support clip planes then use
     that instead */
  if (depth == 1 && has_clip_planes)
    _cogl_set_clip_planes (entry->x_offset,
			   entry->y_offset,
			   entry->width,
			   entry->height);
  else if (stencil_bits)
    _cogl_add_stencil_clip (entry->x_offset,
			    entry->y_offset,
			    entry->width,
			    entry->height,
			    depth == (has_clip_planes ? 2 : 1));
}

/* Get angles of rotation from the model matrix.
 * Use degrees instead of radians because of the precision loss */
static void
cogl_rotation_get_from_mtx(float m[16], gint rotation[3])
{
  float heading, attitude, bank;
  if (m[4] > 0.998)
  {
    /* singularity at north pole */
    heading = atan2(m[2], m[10]);
    attitude = G_PI / 2;
    bank = 0;
  }
  else  if (m[4] < -0.998)
  {
    /* singularity at south pole */
    heading = atan2(m[2], m[10]);
    attitude = -G_PI / 2;
    bank = 0;
  }
  else
  {
    heading = atan2(-m[4], m[0]);
    bank = atan2(-m[7], m[6]);
    attitude = asin(m[4]);
  }
  /* convert to degrees, rounding up */
  rotation[0] = (heading * 180.0f / G_PI) + (heading < 0.0f ? -0.5f : 0.5f);
  rotation[1] = (attitude * 180.0f / G_PI) + (attitude < 0.0f ? -0.5f : 0.5f);
  rotation[2] = (bank * 180.0f / G_PI) + (bank < 0.0f ? -0.5f : 0.5f);
}

static gboolean
cogl_get_clip_rectangular(gint rotation[3])
{
  guint x, y, z;

  x = abs(rotation[0]);
  y = abs(rotation[1]);
  z = abs(rotation[2]);

  return (x == 0 || x == 180) && (y == 0 || y == 180) && (z == 0 || z == 180);
}

static gboolean
cogl_get_clip_rectangular_90(gint rotation[3])
{
  guint x, y, z;

  x = abs(rotation[0]);
  y = abs(rotation[1]);
  z = abs(rotation[2]);

  /* Well, there are other cases as well, but for now those should be enough */
  return
      ((x == 90 || x == 270) && (y ==  0 || y == 180) && (z ==  0 || z == 180)) ||
      ((x == 0  || x == 180) && (y == 90 || y == 270) && (z ==  0 || z == 180)) ||
      ((x == 0  || x == 180) && (y ==  0 || y == 180) && (z == 90 || z == 270));
}

void
cogl_clip_set (ClutterFixed x_offset,
	       ClutterFixed y_offset,
	       ClutterFixed width,
	       ClutterFixed height)
{
  CoglClipStackEntry *entry = g_slice_new (CoglClipStackEntry);
  gint rotation[3];
  gboolean rotated90 = FALSE;

  /* Make a new entry */
  entry->clear = FALSE;
  entry->x_offset = x_offset;
  entry->y_offset = y_offset;
  entry->width = width;
  entry->height = height;

  cogl_get_modelview_matrix_f (entry->matrix);
  cogl_rotation_get_from_mtx (entry->matrix, rotation);

  /* now check for simple (rectangular!) case */
  entry->is_rectangular = FALSE;
  if ( (rotated90 = cogl_get_clip_rectangular_90(rotation)) || cogl_get_clip_rectangular(rotation) )
    {
        ClutterFixed viewport[4];
        float proj[16];
        ClutterVertex pta,ptb;

        entry->is_rectangular = TRUE;
        cogl_get_viewport (viewport);

        if((rotated90 && (viewport[3] > viewport[2])) || rotation[0] == -180)
          {
            pta.x = entry->y_offset;
            pta.y = entry->x_offset;
            pta.z = 0;
            ptb.x = entry->y_offset + entry->height;
            ptb.y = entry->x_offset + entry->width;
            ptb.z = 0;
          }
        else
          {
            pta.x = entry->x_offset;
            pta.y = entry->y_offset;
            pta.z = 0;
            ptb.x = entry->x_offset + entry->width;
            ptb.y = entry->y_offset + entry->height;
            ptb.z = 0;
          }

        cogl_get_projection_matrix_f (proj);

        pta = cogl_util_unproject_f(entry->matrix, proj, viewport, pta);
        ptb = cogl_util_unproject_f(entry->matrix, proj, viewport, ptb);

        entry->scr_x_1 = CLUTTER_FIXED_TO_INT_ROUND(pta.x);
        entry->scr_y_1 = CLUTTER_FIXED_TO_INT_ROUND(pta.y);
        entry->scr_x_2 = CLUTTER_FIXED_TO_INT_ROUND(ptb.x);
        entry->scr_y_2 = CLUTTER_FIXED_TO_INT_ROUND(ptb.y);
        /* make sure _1 is < _2 for x and y */
        if (entry->scr_x_1 > entry->scr_x_2)
          {
            gint t = entry->scr_x_1;
            entry->scr_x_1 = entry->scr_x_2;
            entry->scr_x_2 = t;
          }
        if (entry->scr_y_1 > entry->scr_y_2)
          {
            gint t = entry->scr_y_1;
            entry->scr_y_1 = entry->scr_y_2;
            entry->scr_y_2 = t;
          }
    }
  /* Store it in the stack */
  cogl_clip_stack_top = g_list_prepend (cogl_clip_stack_top, entry);

  /* Add the entry to the current clip */
  _cogl_clip_stack_add (entry, ++cogl_clip_stack_depth);
}

void
cogl_clip_unset (void)
{
  g_return_if_fail (cogl_clip_stack_top != NULL);

  /* Remove the top entry from the stack */
  g_slice_free (CoglClipStackEntry, cogl_clip_stack_top->data);
  cogl_clip_stack_top = g_list_delete_link (cogl_clip_stack_top,
					    cogl_clip_stack_top);
  cogl_clip_stack_depth--;

  /* Rebuild the clip */
  _cogl_clip_stack_rebuild (FALSE);
}

void
_cogl_clip_stack_rebuild (gboolean just_stencil)
{
  int has_clip_planes = cogl_features_available (COGL_FEATURE_FOUR_CLIP_PLANES);
  GList *node;
  int depth = 0;

  /* Attempt to use glScissor to do all our clipping */
  if (_cogl_clip_stack_scissor_rebuild())
    return;

  /* Disable clip planes if the stack is empty */
  if (has_clip_planes && cogl_clip_stack_depth < 1)
    _cogl_disable_clip_planes ();

  /* Disable the stencil buffer if there isn't enough entries */
  if (cogl_clip_stack_depth < (has_clip_planes ? 2 : 1))
    _cogl_disable_stencil_buffer ();

  /* Find the bottom of the stack */
  for (node = cogl_clip_stack_top; depth < cogl_clip_stack_depth - 1;
       node = node->next)
    depth++;

  /* Re-add every entry from the bottom of the stack up */
  depth = 1;
  for (; depth <= cogl_clip_stack_depth; node = node->prev, depth++)
    if (!just_stencil || !has_clip_planes || depth > 1)
      {
	const CoglClipStackEntry *entry = (CoglClipStackEntry *) node->data;
	cogl_push_matrix ();
	_cogl_set_matrix_f (entry->matrix);
	_cogl_clip_stack_add (entry, depth);
	cogl_pop_matrix ();
      }
}

void
_cogl_clip_stack_merge (void)
{
  GList *node = cogl_clip_stack_top;
  int i;

  /* Merge the current clip stack on top of whatever is in the stencil
     buffer */
  if (cogl_clip_stack_depth)
    {
      for (i = 0; i < cogl_clip_stack_depth - 1; i++)
	node = node->next;

      /* Skip the first entry if we have clipping planes */
      if (cogl_features_available (COGL_FEATURE_FOUR_CLIP_PLANES))
	node = node->prev;

      while (node)
	{
	  const CoglClipStackEntry *entry = (CoglClipStackEntry *) node->data;
	  cogl_push_matrix ();
	  _cogl_set_matrix_f (entry->matrix);
	  _cogl_clip_stack_add (entry, 3);
	  cogl_pop_matrix ();

	  node = node->prev;
	}
    }
}

void
cogl_clip_stack_save (void)
{
  CoglClipStackEntry *entry = g_slice_new (CoglClipStackEntry);

  /* Push an entry into the stack to mark that it should be cleared */
  entry->clear = TRUE;
  cogl_clip_stack_top = g_list_prepend (cogl_clip_stack_top, entry);

  /* Reset the depth to zero */
  cogl_clip_stack_depth = 0;

  /* Rebuilding the stack will now disabling all clipping */
  _cogl_clip_stack_rebuild (FALSE);
}

void
cogl_clip_stack_restore (void)
{
  GList *node;

  /* The top of the stack should be a clear marker */
  g_assert (cogl_clip_stack_top);
  g_assert (((CoglClipStackEntry *) cogl_clip_stack_top->data)->clear);

  /* Remove the top entry */
  g_slice_free (CoglClipStackEntry, cogl_clip_stack_top->data);
  cogl_clip_stack_top = g_list_delete_link (cogl_clip_stack_top,
					    cogl_clip_stack_top);

  /* Recalculate the depth of the stack */
  cogl_clip_stack_depth = 0;
  for (node = cogl_clip_stack_top;
       node && !((CoglClipStackEntry *) node->data)->clear;
       node = node->next)
    cogl_clip_stack_depth++;

  _cogl_clip_stack_rebuild (FALSE);
}
