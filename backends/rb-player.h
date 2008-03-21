/*
 *  arch-tag: Interface to audio backend
 *
 *  Copyright (C) 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#ifndef __RB_PLAYER_H
#define __RB_PLAYER_H

#include <glib-object.h>
#include "rb-metadata.h"

G_BEGIN_DECLS

typedef enum
{
	RB_PLAYER_ERROR_NO_AUDIO,
	RB_PLAYER_ERROR_GENERAL,
	RB_PLAYER_ERROR_INTERNAL
} RBPlayerError;

GType rb_player_error_get_type (void);
#define RB_TYPE_PLAYER_ERROR	(rb_player_error_get_type())
GQuark rb_player_error_quark (void);
#define RB_PLAYER_ERROR rb_player_error_quark ()


#define RB_TYPE_PLAYER         (rb_player_get_type ())
#define RB_PLAYER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PLAYER, RBPlayer))
#define RB_IS_PLAYER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PLAYER))
#define RB_PLAYER_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), RB_TYPE_PLAYER, RBPlayerIface))

typedef struct _RBPlayer RBPlayer;

typedef gboolean (*RBPlayerFeatureFunc) (RBPlayer *player);

typedef struct
{
	GTypeInterface	g_iface;

	/* virtual functions */
	gboolean        (*open)			(RBPlayer *player,
						 const char *uri,
						 gpointer stream_data,
						 GDestroyNotify stream_data_destroy,
						 GError **error);
	gboolean	(*opened)		(RBPlayer *player);
	gboolean        (*close)		(RBPlayer *player,
						 const char *uri,
						 GError **error);

	gboolean	(*play)			(RBPlayer *player,
						 gint crossfade,
						 GError **error);
	void		(*pause)		(RBPlayer *player);
	gboolean	(*playing)		(RBPlayer *player);

	void		(*set_volume)		(RBPlayer *player,
						 float volume);
	float		(*get_volume)		(RBPlayer *player);
	void		(*set_replaygain)	(RBPlayer *player,
						 const char *uri,
						 double track_gain,
						 double track_peak,
						 double album_gain,
						 double album_peak);

	gboolean	(*seekable)		(RBPlayer *player);
	void		(*set_time)		(RBPlayer *player,
						 long time);
	long		(*get_time)		(RBPlayer *player);
	gboolean	(*multiple_open)	(RBPlayer *player);


	/* signals */
	void		(*playing_stream)	(RBPlayer *player,
						 gpointer stream_data);
	void		(*eos)			(RBPlayer *player,
						 gpointer stream_data);
	void		(*info)			(RBPlayer *player,
						 gpointer stream_data,
						 RBMetaDataField field,
						 GValue *value);
	void		(*buffering)		(RBPlayer *player,
						 gpointer stream_data,
						 guint progress);
	void		(*error)           	(RBPlayer *player,
						 gpointer stream_data,
						 GError *error);
	void		(*tick)            	(RBPlayer *player,
						 gpointer stream_data,
						 long elapsed,
						 long duration);
	void		(*event)		(RBPlayer *player,
						 gpointer stream_data,
						 gpointer data);
} RBPlayerIface;

GType		rb_player_get_type   (void);
RBPlayer *	rb_player_new        (gboolean want_crossfade,
				      GError **error);

gboolean        rb_player_open       (RBPlayer *player,
				      const char *uri,
				      gpointer stream_data,
				      GDestroyNotify stream_data_destroy,
				      GError **error);
gboolean	rb_player_opened     (RBPlayer *player);
gboolean        rb_player_close      (RBPlayer *player,
				      const char *uri,
				      GError **error);

gboolean	rb_player_play       (RBPlayer *player, gint crossfade, GError **error);
void		rb_player_pause      (RBPlayer *player);
gboolean	rb_player_playing    (RBPlayer *player);

void		rb_player_set_volume (RBPlayer *player, float volume);
float		rb_player_get_volume (RBPlayer *player);
void		rb_player_set_replaygain (RBPlayer *player,
					  const char *uri,
					  double track_gain, double track_peak,
					  double album_gain, double album_peak);

gboolean	rb_player_seekable   (RBPlayer *player);
void		rb_player_set_time   (RBPlayer *player, long newtime);
long		rb_player_get_time   (RBPlayer *player);

gboolean	rb_player_multiple_open (RBPlayer *player);

/* only to be used by subclasses */
void	_rb_player_emit_eos (RBPlayer *player, gpointer stream_data);
void	_rb_player_emit_info (RBPlayer *player, gpointer stream_data, RBMetaDataField field, GValue *value);
void	_rb_player_emit_buffering (RBPlayer *player, gpointer stream_data, guint progress);
void	_rb_player_emit_error (RBPlayer *player, gpointer stream_data, GError *error);
void	_rb_player_emit_tick (RBPlayer *player, gpointer stream_data, long elapsed, long duration);
void	_rb_player_emit_event (RBPlayer *player, gpointer stream_data, const char *name, gpointer data);
void	_rb_player_emit_playing_stream (RBPlayer *player, gpointer stream_data);

G_END_DECLS

#endif /* __RB_PLAYER_H */
