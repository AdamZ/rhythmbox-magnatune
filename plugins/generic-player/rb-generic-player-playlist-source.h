/*
 *  Copyright (C) 2007 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef __RB_GENERIC_PLAYER_PLAYLIST_SOURCE_H
#define __RB_GENERIC_PLAYER_PLAYLIST_SOURCE_H

#include "rb-static-playlist-source.h"
#include "rb-generic-player-source.h"

G_BEGIN_DECLS

#define RB_TYPE_GENERIC_PLAYER_PLAYLIST_SOURCE         (rb_generic_player_playlist_source_get_type ())
#define RB_GENERIC_PLAYER_PLAYLIST_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_GENERIC_PLAYER_PLAYLIST_SOURCE, RBGenericPlayerPlaylistSource))
#define RB_GENERIC_PLAYER_PLAYLIST_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_GENERIC_PLAYER_PLAYLIST_SOURCE, RBGenericPlayerPlaylistSourceClass))
#define RB_IS_GENERIC_PLAYER_PLAYLIST_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_GENERIC_PLAYER_PLAYLIST_SOURCE))
#define RB_IS_GENERIC_PLAYER_PLAYLIST_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_GENERIC_PLAYER_PLAYLIST_SOURCE))
#define RB_GENERIC_PLAYER_PLAYLIST_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_GENERIC_PLAYER_PLAYLIST_SOURCE, RBGenericPlayerPlaylistSourceClass))

typedef struct
{
	RBStaticPlaylistSource parent;
} RBGenericPlayerPlaylistSource;

typedef struct
{
	RBStaticPlaylistSourceClass parent;
} RBGenericPlayerPlaylistSourceClass;

GType		rb_generic_player_playlist_source_get_type	(void);
GType		rb_generic_player_playlist_source_register_type	(GTypeModule *module);

RBSource *	rb_generic_player_playlist_source_new (RBShell *shell,
						       RBGenericPlayerSource *source,
						       const char *playlist_file,
						       RhythmDBEntryType entry_type);
void		rb_generic_player_playlist_delete_from_player (RBGenericPlayerPlaylistSource *source);

G_END_DECLS

#endif	/* __RB_GENERIC_PLAYER_PLAYLIST_SOURCE_H */
