/*
 *  Copyright (C) 2006 Jonathan Matthew <jonathan@kaolin.wh9.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_STREAMING_SOURCE_H
#define __RB_STREAMING_SOURCE_H

#include "rb-source.h"

G_BEGIN_DECLS

#define RB_TYPE_STREAMING_SOURCE         (rb_streaming_source_get_type ())
#define RB_STREAMING_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_STREAMING_SOURCE, RBStreamingSource))
#define RB_STREAMING_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_STREAMING_SOURCE, RBStreamingSourceClass))
#define RB_IS_STREAMING_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_STREAMING_SOURCE))
#define RB_IS_STREAMING_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_STREAMING_SOURCE))
#define RB_STREAMING_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_STREAMING_SOURCE, RBStreamingSourceClass))

typedef struct RBStreamingSourcePrivate RBStreamingSourcePrivate;

typedef struct
{
	RBSource parent;

	RBStreamingSourcePrivate *priv;
} RBStreamingSource;

typedef struct
{
	RBSourceClass parent;
} RBStreamingSourceClass;

GType		rb_streaming_source_get_type	(void);

/* methods for subclasses */
void		rb_streaming_source_get_progress (RBStreamingSource *source,
						  char **progress_text,
						  float *progress);
void		rb_streaming_source_set_streaming_title (RBStreamingSource *source,
							 const char *title);
void		rb_streaming_source_set_streaming_artist (RBStreamingSource *source,
							  const char *artist);
void		rb_streaming_source_set_streaming_album (RBStreamingSource *source,
							 const char *album);

G_END_DECLS

#endif /* __RB_STREAMING_SOURCE_H */
