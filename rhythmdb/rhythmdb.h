/*
 *  arch-tag: Header for RhythmDB - Rhythmbox backend queryable database
 *
 *  Copyright (C) 2003 Colin Walters <walters@rhythmbox.org>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef RHYTHMDB_H
#define RHYTHMDB_H

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtktreemodel.h>
#include <stdarg.h>

G_BEGIN_DECLS

#define RHYTHMDB_TYPE      (rhythmdb_get_type ())
#define RHYTHMDB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE, RhythmDB))
#define RHYTHMDB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_TYPE, RhythmDBClass))
#define RHYTHMDB_IS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE))
#define RHYTHMDB_IS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE))
#define RHYTHMDB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE, RhythmDBClass))

typedef enum
{
	RHYTHMDB_ENTRY_TYPE_SONG,
	RHYTHMDB_ENTRY_TYPE_IRADIO_STATION
} RhythmDBEntryType;

typedef enum
{
	RHYTHMDB_QUERY_END,
	RHYTHMDB_QUERY_DISJUNCTION,
	RHYTHMDB_QUERY_PROP_EQUALS,
	RHYTHMDB_QUERY_PROP_LIKE,
	RHYTHMDB_QUERY_PROP_GREATER,
	RHYTHMDB_QUERY_PROP_LESS,
} RhythmDBQueryType;

#define RHYTHMDB_NUM_PROPERTIES 19
#define RHYTHMDB_NUM_SAVED_PROPERTIES 14

typedef enum
{
	RHYTHMDB_PROP_TYPE,
	RHYTHMDB_PROP_NAME,
	RHYTHMDB_PROP_GENRE,
	RHYTHMDB_PROP_ARTIST,
	RHYTHMDB_PROP_ALBUM,
	RHYTHMDB_PROP_TRACK_NUMBER,
	RHYTHMDB_PROP_DURATION,
	RHYTHMDB_PROP_FILE_SIZE,
	RHYTHMDB_PROP_LOCATION,
	RHYTHMDB_PROP_MTIME,
	RHYTHMDB_PROP_RATING,
	RHYTHMDB_PROP_PLAY_COUNT,
	RHYTHMDB_PROP_LAST_PLAYED,
	RHYTHMDB_PROP_QUALITY,
} RhythmDBPropType;

typedef enum
{
	RHYTHMDB_PROP_NAME_SORT_KEY = 14,
	RHYTHMDB_PROP_GENRE_SORT_KEY,
	RHYTHMDB_PROP_ARTIST_SORT_KEY,
	RHYTHMDB_PROP_ALBUM_SORT_KEY,
	RHYTHMDB_PROP_LAST_PLAYED_STR,
} RhythmDBUnsavedPropType;

GType rhythmdb_query_get_type (void);
GType rhythmdb_prop_get_type (void);
GType rhythmdb_unsaved_prop_get_type (void);

#define RHYTHMDB_TYPE_QUERY (rhythmdb_query_get_type ())
#define RHYTHMDB_TYPE_PROP (rhythmdb_prop_get_type ())
#define RHYTHMDB_TYPE_UNSAVED_PROP (rhythmdb_unsaved_prop_get_type ())

typedef struct {
	guint type;
	guint propid;
	GValue *val;
} RhythmDBQueryData;

typedef void RhythmDBEntry;

typedef struct RhythmDBPrivate RhythmDBPrivate;

typedef struct
{
	GObject parent;

	RhythmDBPrivate *priv;
} RhythmDB;

typedef struct
{
	GObjectClass parent;

	/* signals */
	void	(*entry_added)		(RhythmDBEntry *entry);

	void	(*genre_added)		(RhythmDBEntry *entry, const char *genre);
	void	(*artist_added)		(RhythmDBEntry *entry, const char *artist);
	void	(*album_added)		(RhythmDBEntry *entry, const char *album);
	void	(*genre_deleted)	(RhythmDBEntry *entry, const char *genre);
	void	(*artist_deleted)	(RhythmDBEntry *entry, const char *artist);
	void	(*album_deleted)	(RhythmDBEntry *entry, const char *album);

	/* virtual methods */

	void		(*impl_load)		(RhythmDB *db, GMutex *mutex, gboolean *dead);
	void		(*impl_save)		(RhythmDB *db);
	
	RhythmDBEntry *	(*impl_entry_new)	(RhythmDB *db, RhythmDBEntryType type,
						 const char *uri);

	void		(*impl_entry_set)	(RhythmDB *db, RhythmDBEntry *entry,
						 guint propid, GValue *value);

	void		(*impl_entry_get)	(RhythmDB *db, RhythmDBEntry *entry,
						 guint propid, GValue *value);

	void		(*impl_entry_delete)	(RhythmDB *db, RhythmDBEntry *entry);
	RhythmDBEntry *	(*impl_lookup_by_location)(RhythmDB *db, const char *uri);

	void		(*impl_do_full_query)	(RhythmDB *db, GPtrArray *query,
						 GtkTreeModel **main_model,
						 GtkTreeModel **genre_model,
						 GtkTreeModel **artist_model,
						 GtkTreeModel **album_model);

} RhythmDBClass;

GType		rhythmdb_get_type	(void);

RhythmDB *	rhythmdb_new		(const char *name);

void		rhythmdb_shutdown	(RhythmDB *db);

/**
 * This function must be called WITHOUT the RhythmDB lock held!
 */
void		rhythmdb_load		(RhythmDB *db);

void		rhythmdb_load_join	(RhythmDB *db);

void		rhythmdb_save		(RhythmDB *db);

void		rhythmdb_read_lock	(RhythmDB *db);
void		rhythmdb_write_lock	(RhythmDB *db);
void		rhythmdb_read_unlock	(RhythmDB *db);
void		rhythmdb_write_unlock	(RhythmDB *db);

RhythmDBEntry *	rhythmdb_entry_new	(RhythmDB *db, RhythmDBEntryType type, const char *uri);

void		rhythmdb_entry_set	(RhythmDB *db, RhythmDBEntry *entry,
					 guint propid, GValue *value);

void		rhythmdb_entry_get	(RhythmDB *db, RhythmDBEntry *entry,
					 guint propid, GValue *value);

void		rhythmdb_entry_delete	(RhythmDB *db, RhythmDBEntry *entry);

const char *	rhythmdb_entry_get_string	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);
gboolean	rhythmdb_entry_get_boolean	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);
long		rhythmdb_entry_get_long		(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);
int		rhythmdb_entry_get_int		(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);
double		rhythmdb_entry_get_double	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);
float		rhythmdb_entry_get_float	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);
gpointer	rhythmdb_entry_get_pointer	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 guint property_id);

RhythmDBEntry *	rhythmdb_entry_lookup_by_location (RhythmDB *db, const char *uri);

/**
 * Returns a freshly allocated GtkTreeModel which represents the query.
 * The extended arguments alternate between RhythmDBQueryType args
 * and their values.  Here's an example:
 *
 * rhythmdb_do_entry_query (db, RHYTHMDB_QUERY_PROP_EQUALS, "genre", "Classical",
 *                          RHYTHMDB_QUERY_PROP_GREATER, "rating", 5,
 *                          RHYTHMDB_QUERY_END);
 *
 * Actually, you wouldn't literally specify "Classical" as above.  More on that
 * issue below.
 *
 */
/* GtkTreeModel *	rhythmdb_do_entry_query			(RhythmDB *db, ...); */

/* This is a specialized query to return a flat list of metadata,
 * e.g. genre/artist/album.  The varargs are the same as for the
 * rhythmdb_do_entry_query call.  You should think of it, conceptually,
 * as constructing a list of all entries which match the query, and then
 * returning the values of the attribute as a set.
 */
/* GtkTreeModel *	rhythmdb_do_property_query		(RhythmDB *db, guint property_id, ...); */

void		rhythmdb_do_full_query			(RhythmDB *db,
							 GtkTreeModel **main_model,
							 GtkTreeModel **genre_model,
							 GtkTreeModel **artist_model,
							 GtkTreeModel **album_model, ...);

GType		rhythmdb_get_property_type		(RhythmDB *db, guint property_id);

void		rhythmdb_emit_genre_deleted		(RhythmDB *db, const char *genre);
void		rhythmdb_emit_artist_deleted		(RhythmDB *db, const char *artist);
void		rhythmdb_emit_album_deleted		(RhythmDB *db, const char *album);

void		rhythmdb_emit_genre_added		(RhythmDB *db, const char *genre);
void		rhythmdb_emit_artist_added		(RhythmDB *db, const char *artist);
void		rhythmdb_emit_album_added		(RhythmDB *db, const char *album);

G_END_DECLS

#endif /* __RHYTHMBDB_H */
