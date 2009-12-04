/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#ifndef __RB_ENCODER_GST_H__
#define __RB_ENCODER_GST_H__

#include <glib-object.h>

#include "rb-encoder.h"

G_BEGIN_DECLS

#define RB_TYPE_ENCODER_GST            (rb_encoder_gst_get_type ())
#define RB_ENCODER_GST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_ENCODER, RBEncoderGst))
#define RB_ENCODER_GST_CLASS(k)        (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_ENCODER_GST, RBEncoderGstClass))
#define RB_IS_ENCODER_GST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_ENCODER))
#define RB_IS_ENCODER_GST_CLASS(k)     (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_ENCODER_GST))
#define RB_ENCODER_GST_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_ENCODER_GST, RBEncoderGstClass))

typedef struct _RBEncoderGstPrivate RBEncoderGstPrivate;

typedef struct
{
	GObjectClass obj_class;

	GHashTable *mime_caps_table;
} RBEncoderGstClass;

typedef struct
{
	GObject obj;
	RBEncoderGstPrivate *priv;
} RBEncoderGst;

RBEncoder*	rb_encoder_gst_new		(void);
GType rb_encoder_gst_get_type (void);

G_END_DECLS

#endif /* __RB_ENCODER_GST_H__ */
