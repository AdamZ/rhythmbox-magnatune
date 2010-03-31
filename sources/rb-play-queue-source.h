/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@redhat.com>
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

/*
 * Header for play queue source class
 */

#ifndef __RB_PLAY_QUEUE_SOURCE_H
#define __RB_PLAY_QUEUE_SOURCE_H

#include <sources/rb-static-playlist-source.h>

G_BEGIN_DECLS

#define RB_TYPE_PLAY_QUEUE_SOURCE         (rb_play_queue_source_get_type ())
#define RB_PLAY_QUEUE_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PLAY_QUEUE_SOURCE, RBPlayQueueSource))
#define RB_PLAY_QUEUE_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PLAY_QUEUE_SOURCE, RBPlayQueueSourceClass))
#define RB_IS_PLAY_QUEUE_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PLAY_QUEUE_SOURCE))
#define RB_IS_PLAY_QUEUE_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PLAY_QUEUE_SOURCE))
#define RB_PLAY_QUEUE_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PLAY_QUEUE_SOURCE, RBPlayQueueSourceClass))

typedef struct _RBPlayQueueSource RBPlayQueueSource;
typedef struct _RBPlayQueueSourceClass RBPlayQueueSourceClass;

struct _RBPlayQueueSource
{
	RBStaticPlaylistSource parent;
};

struct _RBPlayQueueSourceClass
{
	RBStaticPlaylistSourceClass parent;
};

GType		rb_play_queue_source_get_type 		(void);

RBSource *	rb_play_queue_source_new		(RBShell *shell);

void		rb_play_queue_source_sidebar_song_info	(RBPlayQueueSource *source);
void		rb_play_queue_source_sidebar_delete	(RBPlayQueueSource *source);
void		rb_play_queue_source_clear_queue	(RBPlayQueueSource *source);

G_END_DECLS

#endif /* __RB_PLAY_QUEUE_SOURCE_H */
