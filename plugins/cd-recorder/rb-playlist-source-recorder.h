/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@redhat.com>
 *  Copyright (C) 2004 William Jon McCann <mccann@jhu.edu>
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

#ifndef __RB_PLAYLIST_SOURCE_RECORDER_H
#define __RB_PLAYLIST_SOURCE_RECORDER_H

#include "rb-shell.h"
#include "rb-plugin.h"

G_BEGIN_DECLS

#define RB_TYPE_PLAYLIST_SOURCE_RECORDER         (rb_playlist_source_recorder_get_type ())
#define RB_PLAYLIST_SOURCE_RECORDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PLAYLIST_SOURCE_RECORDER, RBPlaylistSourceRecorder))
#define RB_PLAYLIST_SOURCE_RECORDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PLAYLIST_SOURCE_RECORDER, RBPlaylistSourceRecorderClass))
#define RB_IS_PLAYLIST_SOURCE_RECORDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PLAYLIST_SOURCE_RECORDER))
#define RB_IS_PLAYLIST_SOURCE_RECORDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PLAYLIST_SOURCE_RECORDER))
#define RB_PLAYLIST_SOURCE_RECORDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PLAYLIST_SOURCE_RECORDER, RBPlaylistSourceRecorderClass))

typedef struct RBPlaylistSourceRecorderPrivate RBPlaylistSourceRecorderPrivate;

typedef struct
{
        GtkDialog                        parent;
        RBPlaylistSourceRecorderPrivate *priv;
} RBPlaylistSourceRecorder;

typedef struct
{
        GtkDialogClass parent_class;
} RBPlaylistSourceRecorderClass;

typedef gboolean (*RBPlaylistSourceIterFunc) (GtkTreeModel *model,
                                              GtkTreeIter  *iter,
                                              char        **uri,
                                              char        **artist,
                                              char        **title,
                                              gulong       *duration);

GType       rb_playlist_source_recorder_get_type (void);

GtkWidget * rb_playlist_source_recorder_new            (GtkWidget                *parent,
                                                        RBShell                  *shell,
							RBPlugin                 *plugin,
                                                        const char               *name);

void        rb_playlist_source_recorder_set_name       (RBPlaylistSourceRecorder *recorder,
                                                        const char               *name,
                                                        GError                  **error);
gboolean    rb_playlist_source_recorder_add_from_model (RBPlaylistSourceRecorder *recorder,
                                                        GtkTreeModel             *model,
                                                        RBPlaylistSourceIterFunc  func,
                                                        GError                  **error);
void        rb_playlist_source_recorder_add_uri        (RBPlaylistSourceRecorder *recorder,
                                                        const char               *uri,
                                                        GError                  **error);
void        rb_playlist_source_recorder_start          (RBPlaylistSourceRecorder *recorder,
                                                        GError                  **error);
void        rb_playlist_source_recorder_stop           (RBPlaylistSourceRecorder *recorder,
                                                        GError                  **error);

G_END_DECLS

#endif /* __RB_PLAYLIST_SOURCE_RECORDER_H */
