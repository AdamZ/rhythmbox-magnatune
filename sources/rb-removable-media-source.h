/*
 *  arch-tag: Header for removable media source object
 *
 *  Copyright (C) 2005 James Livingston  <doclivingston@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#ifndef __RB_REMOVABLE_MEDIA_SOURCE_H
#define __RB_REMOVABLE_MEDIA_SOURCE_H

#include "rb-shell.h"
#include "rb-browser-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_REMOVABLE_MEDIA_SOURCE         (rb_removable_media_source_get_type ())
#define RB_REMOVABLE_MEDIA_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_REMOVABLE_MEDIA_SOURCE, RBRemovableMediaSource))
#define RB_REMOVABLE_MEDIA_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_REMOVABLE_MEDIA_SOURCE, RBRemovableMediaSourceClass))
#define RB_IS_REMOVABLE_MEDIA_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_REMOVABLE_MEDIA_SOURCE))
#define RB_IS_REMOVABLE_MEDIA_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_REMOVABLE_MEDIA_SOURCE))
#define RB_REMOVABLE_MEDIA_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_REMOVABLE_MEDIA_SOURCE, RBRemovableMediaSourceClass))

typedef struct
{
	RBBrowserSource parent;
} RBRemovableMediaSource;

typedef struct
{
	RBBrowserSourceClass parent;

	char*		(*impl_build_dest_uri)	(RBRemovableMediaSource *source,
						 RhythmDBEntry *entry,
						 const char *mimetype,
						 const char *extension);
	GList*		(*impl_get_mime_types)	(RBRemovableMediaSource *source);
	gboolean	(*impl_track_added)	(RBRemovableMediaSource *source,
						 RhythmDBEntry *entry,
						 const char *uri,
						 guint64 filesize,
						 const char *mimetype);
	gboolean	(*impl_should_paste)	(RBRemovableMediaSource *source,
						 RhythmDBEntry *entry);
} RBRemovableMediaSourceClass;

typedef gboolean	(*RBRemovableMediaSourceShouldPasteFunc) (RBRemovableMediaSource *source,
								  RhythmDBEntry *entry);

GType			rb_removable_media_source_get_type	(void);

char*		rb_removable_media_source_build_dest_uri 	(RBRemovableMediaSource *source,
								 RhythmDBEntry *entry,
								 const char *mimetype,
								 const char *extension);
void		rb_removable_media_source_track_added		(RBRemovableMediaSource *source,
								 RhythmDBEntry *entry,
								 const char *uri,
								 guint64 filesize,
								 const char *mimetype);
GList *		rb_removable_media_source_get_mime_types	(RBRemovableMediaSource *source);
gboolean	rb_removable_media_source_should_paste		(RBRemovableMediaSource *source,
								 RhythmDBEntry *entry);

G_END_DECLS

#endif /* __RB_REMOVABLE_MEDIA_SOURCE_H */
