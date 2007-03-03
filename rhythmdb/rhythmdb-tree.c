/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of RhythmDB tree-structured database
 *
 *  Copyright (C) 2003, 2004 Colin Walters <walters@verbum.org>
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

#include "config.h"

#ifdef HAVE_GNU_FWRITE_UNLOCKED
#define _GNU_SOURCE
#endif
#include <stdio.h>
#ifdef HAVE_GNU_FWRITE_UNLOCKED
#undef _GNU_SOURCE
#endif
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <glib/gprintf.h>
#include <glib/gatomic.h>
#include <glib/gi18n.h>
#include <gtk/gtkliststore.h>
#include <libxml/entities.h>
#include <libxml/SAX.h>
#include <libxml/parserInternals.h>

#include "rhythmdb-private.h"
#include "rhythmdb-tree.h"
#include "rhythmdb-property-model.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"

typedef struct RhythmDBTreeProperty
{
#ifndef G_DISABLE_ASSERT
	guint magic;
#endif
	struct RhythmDBTreeProperty *parent;
	GHashTable *children;
} RhythmDBTreeProperty;

#define RHYTHMDB_TREE_PROPERTY_FROM_ENTRY(entry) ((RhythmDBTreeProperty *) entry->data)

G_DEFINE_TYPE(RhythmDBTree, rhythmdb_tree, RHYTHMDB_TYPE)

static void rhythmdb_tree_finalize (GObject *object);

static gboolean rhythmdb_tree_load (RhythmDB *rdb, gboolean *die, GError **error);
static void rhythmdb_tree_save (RhythmDB *rdb);
static void rhythmdb_tree_entry_new (RhythmDB *db, RhythmDBEntry *entry);
static void rhythmdb_tree_entry_new_internal (RhythmDB *db, RhythmDBEntry *entry);
static gboolean rhythmdb_tree_entry_set (RhythmDB *db, RhythmDBEntry *entry,
					 guint propid, const GValue *value);

static void rhythmdb_tree_entry_delete (RhythmDB *db, RhythmDBEntry *entry);
static void rhythmdb_tree_entry_delete_by_type (RhythmDB *adb, RhythmDBEntryType type);

static RhythmDBEntry * rhythmdb_tree_entry_lookup_by_location (RhythmDB *db, RBRefString *uri);
static RhythmDBEntry * rhythmdb_tree_entry_lookup_by_id (RhythmDB *db, gint id);
static void rhythmdb_tree_entry_foreach (RhythmDB *adb, GFunc func, gpointer user_data);
static gint64 rhythmdb_tree_entry_count (RhythmDB *adb);
static void rhythmdb_tree_entry_foreach_by_type (RhythmDB *adb, RhythmDBEntryType type, GFunc func, gpointer user_data);
static gint64 rhythmdb_tree_entry_count_by_type (RhythmDB *adb, RhythmDBEntryType type);
static void rhythmdb_tree_do_full_query (RhythmDB *db, GPtrArray *query,
					 RhythmDBQueryResults *results,
					 gboolean *cancel);
static gboolean rhythmdb_tree_evaluate_query (RhythmDB *adb, GPtrArray *query,
				       RhythmDBEntry *aentry);
static void rhythmdb_tree_entry_type_registered (RhythmDB *db,
						 const char *name,
						 RhythmDBEntryType type);

typedef void (*RBTreeEntryItFunc)(RhythmDBTree *db,
				  RhythmDBEntry *entry,
				  gpointer data);

typedef void (*RBTreePropertyItFunc)(RhythmDBTree *db,
				     RhythmDBTreeProperty *property,
				     gpointer data);
static void rhythmdb_hash_tree_foreach (RhythmDB *adb,
					RhythmDBEntryType type,
					RBTreeEntryItFunc entry_func,
					RBTreePropertyItFunc album_func,
					RBTreePropertyItFunc artist_func,
					RBTreePropertyItFunc genres_func,
					gpointer data);

#define RHYTHMDB_TREE_XML_VERSION "1.3"

static void destroy_tree_property (RhythmDBTreeProperty *prop);
static RhythmDBTreeProperty *get_or_create_album (RhythmDBTree *db, RhythmDBTreeProperty *artist,
						  RBRefString *name);
static RhythmDBTreeProperty *get_or_create_artist (RhythmDBTree *db, RhythmDBTreeProperty *genre,
						   RBRefString *name);
static RhythmDBTreeProperty *get_or_create_genre (RhythmDBTree *db, RhythmDBEntryType type,
							 RBRefString *name);

static void remove_entry_from_album (RhythmDBTree *db, RhythmDBEntry *entry);

static GList *split_query_by_disjunctions (RhythmDBTree *db, GPtrArray *query);
static gboolean evaluate_conjunctive_subquery (RhythmDBTree *db, GPtrArray *query,
					       guint base, guint max, RhythmDBEntry *entry);

struct RhythmDBTreePrivate
{
	GHashTable *entries;
	GHashTable *entry_ids;
	GMutex *entries_lock;

	GHashTable *genres;
	GMutex *genres_lock; /* must be held while using the tree */

	GHashTable *unknown_entry_types;
	gboolean finalizing;

	guint idle_load_id;
};

typedef struct
{
	RBRefString *name;
	RBRefString *value;
} RhythmDBUnknownEntryProperty;

typedef struct
{
	RBRefString *typename;
	GList *properties;
} RhythmDBUnknownEntry;

#define RHYTHMDB_TREE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RHYTHMDB_TYPE_TREE, RhythmDBTreePrivate))

enum
{
	PROP_0,
};

const int RHYTHMDB_TREE_PARSER_INITIAL_BUFFER_SIZE = 512;

GQuark
rhythmdb_tree_error_quark (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("rhythmdb_tree_error");

	return quark;
}

static void
rhythmdb_tree_class_init (RhythmDBTreeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RhythmDBClass *rhythmdb_class = RHYTHMDB_CLASS (klass);

	object_class->finalize = rhythmdb_tree_finalize;

	rhythmdb_class->impl_load = rhythmdb_tree_load;
	rhythmdb_class->impl_save = rhythmdb_tree_save;
	rhythmdb_class->impl_entry_new = rhythmdb_tree_entry_new;
	rhythmdb_class->impl_entry_set = rhythmdb_tree_entry_set;
	rhythmdb_class->impl_entry_delete = rhythmdb_tree_entry_delete;
	rhythmdb_class->impl_entry_delete_by_type = rhythmdb_tree_entry_delete_by_type;
	rhythmdb_class->impl_lookup_by_location = rhythmdb_tree_entry_lookup_by_location;
	rhythmdb_class->impl_lookup_by_id = rhythmdb_tree_entry_lookup_by_id;
	rhythmdb_class->impl_entry_foreach = rhythmdb_tree_entry_foreach;
	rhythmdb_class->impl_entry_count = rhythmdb_tree_entry_count;
	rhythmdb_class->impl_entry_foreach_by_type = rhythmdb_tree_entry_foreach_by_type;
	rhythmdb_class->impl_entry_count_by_type = rhythmdb_tree_entry_count_by_type;
	rhythmdb_class->impl_evaluate_query = rhythmdb_tree_evaluate_query;
	rhythmdb_class->impl_do_full_query = rhythmdb_tree_do_full_query;
	rhythmdb_class->impl_entry_type_registered = rhythmdb_tree_entry_type_registered;

	g_type_class_add_private (klass, sizeof (RhythmDBTreePrivate));
}

static void
rhythmdb_tree_init (RhythmDBTree *db)
{
	db->priv = RHYTHMDB_TREE_GET_PRIVATE (db);

	db->priv->entries = g_hash_table_new (rb_refstring_hash, rb_refstring_equal);

	db->priv->entry_ids = g_hash_table_new (g_direct_hash, g_direct_equal);

	db->priv->genres = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						  NULL, (GDestroyNotify)g_hash_table_destroy);
	db->priv->unknown_entry_types = g_hash_table_new (rb_refstring_hash, rb_refstring_equal);

	db->priv->entries_lock = g_mutex_new();
	db->priv->genres_lock = g_mutex_new();
}

/* must be called with the genres lock held */
static void
unparent_entries (gpointer key,
		  RhythmDBEntry *entry,
		  RhythmDBTree *db)
{
	rb_assert_locked (db->priv->genres_lock);

	remove_entry_from_album (db, entry);
}

static void
free_unknown_entries (RBRefString *name,
		      GList *entries,
		      gpointer nah)
{
	GList *e;
	for (e = entries; e != NULL; e = e->next) {
		RhythmDBUnknownEntry *entry;
		GList *p;

		entry = (RhythmDBUnknownEntry *)e->data;
		rb_refstring_unref (entry->typename);
		for (p = entry->properties; p != NULL; p = p->next) {
			RhythmDBUnknownEntryProperty *prop;

			prop = (RhythmDBUnknownEntryProperty *)p->data;
			rb_refstring_unref (prop->name);
			rb_refstring_unref (prop->value);
			g_free (prop);
		}

		g_list_free (entry->properties);
	}
	g_list_free (entries);
}

static void
rhythmdb_tree_finalize (GObject *object)
{
	RhythmDBTree *db;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RHYTHMDB_IS_TREE (object));

	db = RHYTHMDB_TREE (object);

	g_return_if_fail (db->priv != NULL);

	db->priv->finalizing = TRUE;

	g_mutex_lock (db->priv->genres_lock);
	g_hash_table_foreach (db->priv->entries, (GHFunc) unparent_entries, db);
	g_mutex_unlock (db->priv->genres_lock);

	g_hash_table_destroy (db->priv->entries);
	g_hash_table_destroy (db->priv->entry_ids);
	g_mutex_free (db->priv->entries_lock);

	g_hash_table_destroy (db->priv->genres);
	g_mutex_free (db->priv->genres_lock);

	g_hash_table_foreach (db->priv->unknown_entry_types,
			      (GHFunc) free_unknown_entries,
			      NULL);
	g_hash_table_destroy (db->priv->unknown_entry_types);

	G_OBJECT_CLASS (rhythmdb_tree_parent_class)->finalize (object);
}

struct RhythmDBTreeLoadContext
{
	RhythmDBTree *db;
	xmlParserCtxtPtr xmlctx;
	gboolean *die;
	enum {
		RHYTHMDB_TREE_PARSER_STATE_START,
		RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB,
		RHYTHMDB_TREE_PARSER_STATE_ENTRY,
		RHYTHMDB_TREE_PARSER_STATE_ENTRY_PROPERTY,
		RHYTHMDB_TREE_PARSER_STATE_UNKNOWN_ENTRY,
		RHYTHMDB_TREE_PARSER_STATE_UNKNOWN_ENTRY_PROPERTY,
		RHYTHMDB_TREE_PARSER_STATE_END,
	} state;
	guint in_unknown_elt;
	RhythmDBEntry *entry;
	RhythmDBUnknownEntry *unknown_entry;
	GString *buf;
	RhythmDBPropType propid;
	gint batch_count;
	GError **error;

	/* updating */
	gboolean has_date;
	gboolean canonicalise_uris;
	gboolean reload_all_metadata;
};

static void
rhythmdb_tree_parser_start_element (struct RhythmDBTreeLoadContext *ctx,
				    const char *name,
				    const char **attrs)
{
	if (*ctx->die == TRUE) {
		xmlStopParser (ctx->xmlctx);
		return;
	}

	if (ctx->in_unknown_elt) {
		ctx->in_unknown_elt++;
		return;
	}

	switch (ctx->state)
	{
	case RHYTHMDB_TREE_PARSER_STATE_START:
	{
		if (!strcmp (name, "rhythmdb")) {
			ctx->state = RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB;
			for (; *attrs; attrs +=2) {
				if (!strcmp (*attrs, "version")) {
					const char *version = *(attrs+1);

					if (!strcmp (version, "1.0") || !strcmp (version, "1.1")) {
						ctx->canonicalise_uris = TRUE;
						rb_debug ("old version of rhythmdb, performing URI canonicalisation for all entries");
					} else if (!strcmp (version, "1.2")) {
						/* current version*/
						rb_debug ("reloading all file metadata to get MusicBrainz tags");
						ctx->reload_all_metadata = TRUE;
					} else if (!strcmp (version, "1.3")) {
						/* current version*/
					} else {
						g_set_error (ctx->error,
							     RHYTHMDB_TREE_ERROR,
							     RHYTHMDB_TREE_ERROR_DATABASE_TOO_NEW,
							     _("The database was created by a later version of rhythmbox."
							       "  This version of rhythmbox cannot read the database."));
						xmlStopParser (ctx->xmlctx);
					}
				} else {
					g_assert_not_reached ();
				}
			}

		} else {
			ctx->in_unknown_elt++;
		}

		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB:
	{
		if (!strcmp (name, "entry")) {
			RhythmDBEntryType type = RHYTHMDB_ENTRY_TYPE_INVALID;
			const char *typename = NULL;
			for (; *attrs; attrs +=2) {
				if (!strcmp (*attrs, "type")) {
					typename = *(attrs+1);
					type = rhythmdb_entry_type_get_by_name (RHYTHMDB (ctx->db), typename);
					break;
				}
			}

			g_assert (typename);
			if (type != RHYTHMDB_ENTRY_TYPE_INVALID) {
				ctx->state = RHYTHMDB_TREE_PARSER_STATE_ENTRY;
				ctx->entry = rhythmdb_entry_allocate (RHYTHMDB (ctx->db), type);
				ctx->entry->flags |= RHYTHMDB_ENTRY_TREE_LOADING;
				ctx->has_date = FALSE;
			} else {
				rb_debug ("reading unknown entry");
				ctx->state = RHYTHMDB_TREE_PARSER_STATE_UNKNOWN_ENTRY;
				ctx->unknown_entry = g_new0 (RhythmDBUnknownEntry, 1);
				ctx->unknown_entry->typename = rb_refstring_new (typename);
			}
		} else {
			ctx->in_unknown_elt++;
		}
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY:
	{
		int val = rhythmdb_propid_from_nice_elt_name (RHYTHMDB (ctx->db), BAD_CAST name);
		if (val < 0) {
			ctx->in_unknown_elt++;
			break;
		}

		ctx->state = RHYTHMDB_TREE_PARSER_STATE_ENTRY_PROPERTY;
		ctx->propid = val;
		g_string_truncate (ctx->buf, 0);
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_UNKNOWN_ENTRY:
	{
		RhythmDBUnknownEntryProperty *prop;

		prop = g_new0 (RhythmDBUnknownEntryProperty, 1);
		prop->name = rb_refstring_new (name);

		ctx->unknown_entry->properties = g_list_prepend (ctx->unknown_entry->properties, prop);
		ctx->state = RHYTHMDB_TREE_PARSER_STATE_UNKNOWN_ENTRY_PROPERTY;
		g_string_truncate (ctx->buf, 0);
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_UNKNOWN_ENTRY_PROPERTY:
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY_PROPERTY:
	case RHYTHMDB_TREE_PARSER_STATE_END:
	break;
	}
}

static void
rhythmdb_tree_parser_end_element (struct RhythmDBTreeLoadContext *ctx,
				  const char *name)
{
	if (*ctx->die == TRUE) {
		xmlStopParser (ctx->xmlctx);
		return;
	}

	if (ctx->in_unknown_elt) {
		ctx->in_unknown_elt--;
		return;
	}

	switch (ctx->state)
	{
	case RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB:
		ctx->state = RHYTHMDB_TREE_PARSER_STATE_END;
		break;
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY:
	{
		if (!ctx->has_date | ctx->reload_all_metadata) {
			/* there is no date metadata, so this is from an old version
			 * reset the last-modified timestamp, so that the file is re-read
			 */
			rb_debug ("pre-Date entry found, causing re-read");
			ctx->entry->mtime = 0;
		}
		if (ctx->entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED) {
			RhythmDBPodcastFields *podcast = RHYTHMDB_ENTRY_GET_TYPE_DATA (ctx->entry, RhythmDBPodcastFields);
			/* Handle upgrades from 0.9.2.
			 * Previously, last-seen for podcast feeds was the time of the last post,
			 * and post-time was unused.  Now, we want last-seen to be the time we
			 * last updated the feed, and post-time to be the time of the last post.
			 */
			if (podcast->post_time == 0) {
				podcast->post_time = ctx->entry->last_seen;
			}
		}

		if (ctx->entry->location != NULL) {
			RhythmDBEntry *entry;

			g_mutex_lock (ctx->db->priv->entries_lock);
			entry = g_hash_table_lookup (ctx->db->priv->entries, ctx->entry->location);
			if (entry == NULL) {
				rhythmdb_tree_entry_new_internal (RHYTHMDB (ctx->db), ctx->entry);
				rhythmdb_entry_insert (RHYTHMDB (ctx->db), ctx->entry);
				if (++ctx->batch_count == RHYTHMDB_QUERY_MODEL_SUGGESTED_UPDATE_CHUNK) {
					rhythmdb_commit (RHYTHMDB (ctx->db));
					ctx->batch_count = 0;
				}
			} else {
				rb_debug ("found entry with duplicate location %s. merging metadata",
					  rb_refstring_get (ctx->entry->location));
				entry->play_count += ctx->entry->play_count;

				if (entry->rating < 0.01)
					entry->rating = ctx->entry->rating;
				else if (ctx->entry->rating > 0.01)
					entry->rating = (entry->rating + ctx->entry->rating) / 2;

				if (ctx->entry->last_played > entry->last_played)
					entry->last_played = ctx->entry->last_played;

				if (ctx->entry->first_seen < entry->first_seen)
					entry->first_seen = ctx->entry->first_seen;

				if (ctx->entry->last_seen > entry->last_seen)
					entry->last_seen = ctx->entry->last_seen;

				rhythmdb_entry_unref (ctx->entry);
			}
			g_mutex_unlock (ctx->db->priv->entries_lock);
		} else {
			rb_debug ("found entry without location");
			rhythmdb_entry_unref (ctx->entry);
		}
		ctx->state = RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB;
		ctx->entry = NULL;
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_UNKNOWN_ENTRY:
	{
		GList *entry_list;

		rb_debug ("finished reading unknown entry");
		ctx->unknown_entry->properties = g_list_reverse (ctx->unknown_entry->properties);

		entry_list = g_hash_table_lookup (ctx->db->priv->unknown_entry_types, ctx->unknown_entry->typename);
		entry_list = g_list_prepend (entry_list, ctx->unknown_entry);
		g_hash_table_insert (ctx->db->priv->unknown_entry_types, ctx->unknown_entry->typename, entry_list);

		ctx->state = RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB;
		ctx->unknown_entry = NULL;
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY_PROPERTY:
	{
		GValue value = {0,};
		gboolean set = FALSE;
		gboolean skip = FALSE;

		/* special case some properties for upgrade handling etc. */
		switch (ctx->propid) {
		case RHYTHMDB_PROP_DATE:
			ctx->has_date = TRUE;
			break;
		case RHYTHMDB_PROP_LOCATION:
			if (ctx->canonicalise_uris) {
				char *canon = rb_canonicalise_uri (ctx->buf->str);

				g_value_init (&value, G_TYPE_STRING);
				g_value_take_string (&value, canon);
				set = TRUE;
			}
			break;
		case RHYTHMDB_PROP_MOUNTPOINT:
			/* fix old podcast posts */
			if (g_str_has_prefix (ctx->buf->str, "http://"))
				skip = TRUE;
			break;
		default:
			break;
		}

		if (!skip) {
			if (!set) {
				rhythmdb_read_encoded_property (RHYTHMDB (ctx->db), ctx->buf->str, ctx->propid, &value);
			}

			rhythmdb_entry_set_internal (RHYTHMDB (ctx->db), ctx->entry, FALSE, ctx->propid, &value);
			g_value_unset (&value);
		}

		ctx->state = RHYTHMDB_TREE_PARSER_STATE_ENTRY;
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_UNKNOWN_ENTRY_PROPERTY:
	{
		RhythmDBUnknownEntryProperty *prop;

		g_assert (ctx->unknown_entry->properties);
		prop = ctx->unknown_entry->properties->data;
		g_assert (prop->value == NULL);
		prop->value = rb_refstring_new (ctx->buf->str);
		rb_debug ("unknown entry property: %s = %s", rb_refstring_get (prop->name), rb_refstring_get (prop->value));

		ctx->state = RHYTHMDB_TREE_PARSER_STATE_UNKNOWN_ENTRY;
		break;
	}
	case RHYTHMDB_TREE_PARSER_STATE_START:
	case RHYTHMDB_TREE_PARSER_STATE_END:
	break;
	}
}

static void
rhythmdb_tree_parser_characters (struct RhythmDBTreeLoadContext *ctx,
				 const char *data,
				 guint len)
{
	if (*ctx->die == TRUE) {
		xmlStopParser (ctx->xmlctx);
		return;
	}

	switch (ctx->state)
	{
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY_PROPERTY:
	case RHYTHMDB_TREE_PARSER_STATE_UNKNOWN_ENTRY_PROPERTY:
		g_string_append_len (ctx->buf, data, len);
		break;
	case RHYTHMDB_TREE_PARSER_STATE_ENTRY:
	case RHYTHMDB_TREE_PARSER_STATE_UNKNOWN_ENTRY:
	case RHYTHMDB_TREE_PARSER_STATE_RHYTHMDB:
	case RHYTHMDB_TREE_PARSER_STATE_START:
	case RHYTHMDB_TREE_PARSER_STATE_END:
	break;
	}
}

static gboolean
rhythmdb_tree_load (RhythmDB *rdb,
		    gboolean *die,
		    GError **error)
{
	RhythmDBTree *db = RHYTHMDB_TREE (rdb);
	xmlParserCtxtPtr ctxt;
	xmlSAXHandlerPtr sax_handler;
	struct RhythmDBTreeLoadContext *ctx;
	char *name;
	GError *local_error;
	gboolean ret;

	local_error = NULL;

	sax_handler = g_new0 (xmlSAXHandler, 1);
	ctx = g_new0 (struct RhythmDBTreeLoadContext, 1);

	sax_handler->startElement = (startElementSAXFunc) rhythmdb_tree_parser_start_element;
	sax_handler->endElement = (endElementSAXFunc) rhythmdb_tree_parser_end_element;
	sax_handler->characters = (charactersSAXFunc) rhythmdb_tree_parser_characters;

	ctx->state = RHYTHMDB_TREE_PARSER_STATE_START;
	ctx->db = db;
	ctx->die = die;
	ctx->buf = g_string_sized_new (RHYTHMDB_TREE_PARSER_INITIAL_BUFFER_SIZE);
	ctx->error = &local_error;

	g_object_get (G_OBJECT (db), "name", &name, NULL);

	if (g_file_test (name, G_FILE_TEST_EXISTS)) {
		ctxt = xmlCreateFileParserCtxt (name);
		ctx->xmlctx = ctxt;
		xmlFree (ctxt->sax);
		ctxt->userData = ctx;
		ctxt->sax = sax_handler;
		xmlParseDocument (ctxt);
		ctxt->sax = NULL;
		xmlFreeParserCtxt (ctxt);

		if (ctx->batch_count)
			rhythmdb_commit (RHYTHMDB (ctx->db));
	}

	ret = TRUE;
	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		ret = FALSE;
	}

	g_string_free (ctx->buf, TRUE);
	g_free (name);
	g_free (sax_handler);
	g_free (ctx);

	return ret;
}

struct RhythmDBTreeSaveContext
{
	RhythmDBTree *db;
	FILE *handle;
	char *error;
};

#ifdef HAVE_GNU_FWRITE_UNLOCKED
#define RHYTHMDB_FWRITE_REAL fwrite_unlocked
#define RHYTHMDB_FPUTC_REAL fputc_unlocked
#else
#define RHYTHMDB_FWRITE_REAL fwrite
#define RHYTHMDB_FPUTC_REAL fputc
#endif

#define RHYTHMDB_FWRITE(w,x,len,handle,error) do {			\
	if (error == NULL) {						\
		if (RHYTHMDB_FWRITE_REAL (w,x,len,handle) != len) {	\
			error = g_strdup (g_strerror (errno));		\
		}							\
	}								\
} while (0)

#define RHYTHMDB_FPUTC(x,handle,error) do {				\
	if (error == NULL) {						\
		if (RHYTHMDB_FPUTC_REAL (x,handle) == EOF) {		\
			error = g_strdup (g_strerror (errno));		\
		}							\
	}								\
} while (0)

#define RHYTHMDB_FWRITE_STATICSTR(STR, HANDLE, ERROR) RHYTHMDB_FWRITE(STR, 1, sizeof(STR)-1, HANDLE, ERROR)

static void
write_elt_name_open (struct RhythmDBTreeSaveContext *ctx,
		     const xmlChar *elt_name)
{
	RHYTHMDB_FWRITE_STATICSTR ("    <", ctx->handle, ctx->error);
	RHYTHMDB_FWRITE (elt_name, 1, xmlStrlen (elt_name), ctx->handle, ctx->error);
	RHYTHMDB_FPUTC ('>', ctx->handle, ctx->error);
}

static void
write_elt_name_close (struct RhythmDBTreeSaveContext *ctx,
		      const xmlChar *elt_name)
{
	RHYTHMDB_FWRITE_STATICSTR ("</", ctx->handle, ctx->error);
	RHYTHMDB_FWRITE (elt_name, 1, xmlStrlen (elt_name), ctx->handle, ctx->error);
	RHYTHMDB_FWRITE_STATICSTR (">\n", ctx->handle, ctx->error);
}

static void
save_entry_string (struct RhythmDBTreeSaveContext *ctx,
		   const xmlChar *elt_name,
		   const char *str)
{
	xmlChar *encoded;

	g_return_if_fail (str != NULL);
	write_elt_name_open (ctx, elt_name);
	encoded	= xmlEncodeEntitiesReentrant (NULL, BAD_CAST str);
	RHYTHMDB_FWRITE (encoded, 1, xmlStrlen (encoded), ctx->handle, ctx->error);
	g_free (encoded);
	write_elt_name_close (ctx, elt_name);
}

static void
save_entry_int (struct RhythmDBTreeSaveContext *ctx,
		const xmlChar *elt_name,
		int num)
{
	char buf[92];
	if (num == 0)
		return;
	write_elt_name_open (ctx, elt_name);
	g_snprintf (buf, sizeof (buf), "%d", num);
	RHYTHMDB_FWRITE (buf, 1, strlen (buf), ctx->handle, ctx->error);
	write_elt_name_close (ctx, elt_name);
}

static void
save_entry_ulong (struct RhythmDBTreeSaveContext *ctx,
		  const xmlChar *elt_name,
		  gulong num,
		  gboolean save_zeroes)
{
	char buf[92];

	if (num == 0 && !save_zeroes)
		return;
	write_elt_name_open (ctx, elt_name);
	g_snprintf (buf, sizeof (buf), "%lu", num);
	RHYTHMDB_FWRITE (buf, 1, strlen (buf), ctx->handle, ctx->error);
	write_elt_name_close (ctx, elt_name);
}

static void
save_entry_boolean (struct RhythmDBTreeSaveContext *ctx,
		    const xmlChar *elt_name,
		    gboolean val)
{
	save_entry_ulong (ctx, elt_name, val ? 1 : 0, FALSE);
}

static void
save_entry_uint64 (struct RhythmDBTreeSaveContext *ctx,
		   const xmlChar *elt_name,
		   guint64 num)
{
	char buf[92];

	if (num == 0)
		return;

	write_elt_name_open (ctx, elt_name);
	g_snprintf (buf, sizeof (buf), "%" G_GUINT64_FORMAT, num);
	RHYTHMDB_FWRITE (buf, 1, strlen (buf), ctx->handle, ctx->error);
	write_elt_name_close (ctx, elt_name);
}

static void
save_entry_double (struct RhythmDBTreeSaveContext *ctx,
		   const xmlChar *elt_name,
		   double num)
{
	char buf[G_ASCII_DTOSTR_BUF_SIZE+1];

	if (num > -0.001 && num < 0.001)
		return;

	write_elt_name_open (ctx, elt_name);
	g_ascii_dtostr (buf, sizeof (buf), num);
	RHYTHMDB_FWRITE (buf, 1, strlen (buf), ctx->handle, ctx->error);
	write_elt_name_close (ctx, elt_name);
}

/* This code is intended to be highly optimized.  This came at a small
 * readability cost.  Sorry about that.
 */
static void
save_entry (RhythmDBTree *db,
	    RhythmDBEntry *entry,
	    struct RhythmDBTreeSaveContext *ctx)
{
	RhythmDBPropType i;
	RhythmDBPodcastFields *podcast = NULL;
	xmlChar *encoded;

	if (ctx->error)
		return;

	if (entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED ||
	    entry->type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST)
		podcast = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RhythmDBPodcastFields);

	RHYTHMDB_FWRITE_STATICSTR ("  <entry type=\"", ctx->handle, ctx->error);
	encoded	= xmlEncodeEntitiesReentrant (NULL, BAD_CAST entry->type->name);
	RHYTHMDB_FWRITE (encoded, 1, xmlStrlen (encoded), ctx->handle, ctx->error);
	g_free (encoded);

	RHYTHMDB_FWRITE_STATICSTR ("\">\n", ctx->handle, ctx->error);

	/* Skip over the first property - the type */
	for (i = 1; i < RHYTHMDB_NUM_PROPERTIES; i++) {
		const xmlChar *elt_name;

		if (ctx->error)
			return;

		elt_name = rhythmdb_nice_elt_name_from_propid ((RhythmDB *) ctx->db, i);

		switch (i) {
		case RHYTHMDB_PROP_TYPE:
			break;
		case RHYTHMDB_PROP_ENTRY_ID:
			break;
		case RHYTHMDB_PROP_TITLE:
			save_entry_string(ctx, elt_name, rb_refstring_get (entry->title));
			break;
		case RHYTHMDB_PROP_ALBUM:
			save_entry_string(ctx, elt_name, rb_refstring_get (entry->album));
			break;
		case RHYTHMDB_PROP_ARTIST:
			save_entry_string(ctx, elt_name, rb_refstring_get (entry->artist));
			break;
		case RHYTHMDB_PROP_GENRE:
			save_entry_string(ctx, elt_name, rb_refstring_get (entry->genre));
			break;
		case RHYTHMDB_PROP_MUSICBRAINZ_TRACKID:
			save_entry_string(ctx, elt_name, rb_refstring_get (entry->musicbrainz_trackid));
			break;
		case RHYTHMDB_PROP_TRACK_NUMBER:
			save_entry_ulong (ctx, elt_name, entry->tracknum, FALSE);
			break;
		case RHYTHMDB_PROP_DISC_NUMBER:
			save_entry_ulong (ctx, elt_name, entry->discnum, FALSE);
			break;
		case RHYTHMDB_PROP_DATE:
			if (g_date_valid (&entry->date))
				save_entry_ulong (ctx, elt_name, g_date_get_julian (&entry->date), TRUE);
			else
				save_entry_ulong (ctx, elt_name, 0, TRUE);
			break;
		case RHYTHMDB_PROP_DURATION:
			save_entry_ulong (ctx, elt_name, entry->duration, FALSE);
			break;
		case RHYTHMDB_PROP_BITRATE:
			save_entry_int(ctx, elt_name, entry->bitrate);
			break;
		case RHYTHMDB_PROP_TRACK_GAIN:
			save_entry_double(ctx, elt_name, entry->track_gain);
			break;
		case RHYTHMDB_PROP_TRACK_PEAK:
			save_entry_double(ctx, elt_name, entry->track_peak);
			break;
		case RHYTHMDB_PROP_ALBUM_GAIN:
			save_entry_double(ctx, elt_name, entry->album_gain);
			break;
		case RHYTHMDB_PROP_ALBUM_PEAK:
			save_entry_double(ctx, elt_name, entry->album_peak);
			break;
		case RHYTHMDB_PROP_LOCATION:
			save_entry_string(ctx, elt_name, rb_refstring_get (entry->location));
			break;
		case RHYTHMDB_PROP_MOUNTPOINT:
			/* Avoid crashes on exit when upgrading from 0.8
			 * and no mountpoint is available from some entries */
			if (entry->mountpoint) {
				save_entry_string(ctx, elt_name, rb_refstring_get (entry->mountpoint));
			}
			break;
		case RHYTHMDB_PROP_FILE_SIZE:
			save_entry_uint64(ctx, elt_name, entry->file_size);
			break;
		case RHYTHMDB_PROP_MIMETYPE:
			save_entry_string(ctx, elt_name, rb_refstring_get (entry->mimetype));
			break;
		case RHYTHMDB_PROP_MTIME:
			save_entry_ulong (ctx, elt_name, entry->mtime, FALSE);
			break;
		case RHYTHMDB_PROP_FIRST_SEEN:
			save_entry_ulong (ctx, elt_name, entry->first_seen, FALSE);
			break;
		case RHYTHMDB_PROP_LAST_SEEN:
			save_entry_ulong (ctx, elt_name, entry->last_seen, FALSE);
			break;
		case RHYTHMDB_PROP_RATING:
			save_entry_double(ctx, elt_name, entry->rating);
			break;
		case RHYTHMDB_PROP_PLAY_COUNT:
			save_entry_ulong (ctx, elt_name, entry->play_count, FALSE);
			break;
		case RHYTHMDB_PROP_LAST_PLAYED:
			save_entry_ulong (ctx, elt_name, entry->last_played, FALSE);
			break;
		case RHYTHMDB_PROP_HIDDEN:
			{
				gboolean hidden = ((entry->flags & RHYTHMDB_ENTRY_HIDDEN) != 0);
				save_entry_boolean (ctx, elt_name, hidden);
			}
			break;
		case RHYTHMDB_PROP_STATUS:
			if (podcast)
				save_entry_ulong (ctx, elt_name, podcast->status, FALSE);
			break;
		case RHYTHMDB_PROP_DESCRIPTION:
			if (podcast && podcast->description)
				save_entry_string(ctx, elt_name, rb_refstring_get (podcast->description));
			break;
		case RHYTHMDB_PROP_SUBTITLE:
			if (podcast && podcast->subtitle)
				save_entry_string(ctx, elt_name, rb_refstring_get (podcast->subtitle));
			break;
		case RHYTHMDB_PROP_SUMMARY:
			if (podcast && podcast->summary)
				save_entry_string(ctx, elt_name, rb_refstring_get (podcast->summary));
			break;
		case RHYTHMDB_PROP_LANG:
			if (podcast && podcast->lang)
				save_entry_string(ctx, elt_name, rb_refstring_get (podcast->lang));
			break;
		case RHYTHMDB_PROP_COPYRIGHT:
			if (podcast && podcast->copyright)
				save_entry_string(ctx, elt_name, rb_refstring_get (podcast->copyright));
			break;
		case RHYTHMDB_PROP_IMAGE:
			if (podcast && podcast->image)
				save_entry_string(ctx, elt_name, rb_refstring_get (podcast->image));
			break;
		case RHYTHMDB_PROP_POST_TIME:
			if (podcast)
				save_entry_ulong (ctx, elt_name, podcast->post_time, FALSE);
			break;
		case RHYTHMDB_PROP_TITLE_SORT_KEY:
		case RHYTHMDB_PROP_GENRE_SORT_KEY:
		case RHYTHMDB_PROP_ARTIST_SORT_KEY:
		case RHYTHMDB_PROP_ALBUM_SORT_KEY:
		case RHYTHMDB_PROP_TITLE_FOLDED:
		case RHYTHMDB_PROP_GENRE_FOLDED:
		case RHYTHMDB_PROP_ARTIST_FOLDED:
		case RHYTHMDB_PROP_ALBUM_FOLDED:
		case RHYTHMDB_PROP_LAST_PLAYED_STR:
		case RHYTHMDB_PROP_PLAYBACK_ERROR:
		case RHYTHMDB_PROP_FIRST_SEEN_STR:
		case RHYTHMDB_PROP_LAST_SEEN_STR:
		case RHYTHMDB_PROP_SEARCH_MATCH:
		case RHYTHMDB_PROP_YEAR:
		case RHYTHMDB_NUM_PROPERTIES:
			break;
		}
	}

	RHYTHMDB_FWRITE_STATICSTR ("  </entry>\n", ctx->handle, ctx->error);
}

static void
save_entry_type (const char *name,
		 RhythmDBEntryType entry_type,
		 struct RhythmDBTreeSaveContext *ctx)
{
	if (entry_type->save_to_disk == FALSE)
		return;

	rb_debug ("saving entries of type %s", name);
	rhythmdb_hash_tree_foreach (RHYTHMDB (ctx->db), entry_type,
				    (RBTreeEntryItFunc) save_entry,
				    NULL, NULL, NULL, ctx);
}

static void
save_unknown_entry_type (RBRefString *typename,
			 GList *entries,
			 struct RhythmDBTreeSaveContext *ctx)
{
	GList *t;

	for (t = entries; t != NULL; t = t->next) {
		RhythmDBUnknownEntry *entry;
		xmlChar *encoded;
		GList *p;

		if (ctx->error)
			return;

		entry = (RhythmDBUnknownEntry *)t->data;

		RHYTHMDB_FWRITE_STATICSTR ("  <entry type=\"", ctx->handle, ctx->error);
		encoded	= xmlEncodeEntitiesReentrant (NULL, BAD_CAST rb_refstring_get (entry->typename));
		RHYTHMDB_FWRITE (encoded, 1, xmlStrlen (encoded), ctx->handle, ctx->error);
		g_free (encoded);

		RHYTHMDB_FWRITE_STATICSTR ("\">\n", ctx->handle, ctx->error);

		for (p = entry->properties; p != NULL; p = p->next) {
			RhythmDBUnknownEntryProperty *prop;
			prop = (RhythmDBUnknownEntryProperty *) p->data;
			save_entry_string(ctx, (const xmlChar *)rb_refstring_get (prop->name), rb_refstring_get (prop->value));
		}

		RHYTHMDB_FWRITE_STATICSTR ("  </entry>\n", ctx->handle, ctx->error);
	}
}

static void
rhythmdb_tree_save (RhythmDB *rdb)
{
	RhythmDBTree *db = RHYTHMDB_TREE (rdb);
	char *name;
	GString *savepath;
	FILE *f;
	struct RhythmDBTreeSaveContext ctx;

	g_object_get (G_OBJECT (db), "name", &name, NULL);

	savepath = g_string_new (name);
	g_string_append (savepath, ".tmp");

	f = fopen (savepath->str, "w");

	if (!f) {
		g_warning ("Can't save XML: %s", g_strerror (errno));
		goto out;
	}

	ctx.db = db;
	ctx.handle = f;
	ctx.error = NULL;
	RHYTHMDB_FWRITE_STATICSTR ("<?xml version=\"1.0\" standalone=\"yes\"?>\n"
				   "<rhythmdb version=\"" RHYTHMDB_TREE_XML_VERSION "\">\n",
				   ctx.handle, ctx.error);

	rhythmdb_entry_type_foreach (rdb, (GHFunc) save_entry_type, &ctx);
	g_hash_table_foreach (db->priv->unknown_entry_types,
			      (GHFunc) save_unknown_entry_type,
			      &ctx);

	RHYTHMDB_FWRITE_STATICSTR ("</rhythmdb>\n", ctx.handle, ctx.error);

	if (fclose (f) < 0) {
		g_warning ("Couldn't close %s: %s",
			   savepath->str,
			   g_strerror (errno));
		unlink (savepath->str);
		goto out;
	}

	if (ctx.error != NULL) {
		g_warning ("Writing to the database failed: %s", ctx.error);
		g_free (ctx.error);
		unlink (savepath->str);
	} else {
		if (rename (savepath->str, name) < 0) {
			g_warning ("Couldn't rename %s to %s: %s",
				   name, savepath->str,
				   g_strerror (errno));
			unlink (savepath->str);
		}
	}

out:
	g_string_free (savepath, TRUE);
	g_free (name);
	return;
}

#undef RHYTHMDB_FWRITE_ENCODED_STR
#undef RHYTHMDB_FWRITE_STATICSTR
#undef RHYTHMDB_FPUTC
#undef RHYTHMDB_FWRITE

RhythmDB *
rhythmdb_tree_new (const char *name)
{
	RhythmDBTree *db = g_object_new (RHYTHMDB_TYPE_TREE, "name", name, NULL);

	g_return_val_if_fail (db->priv != NULL, NULL);

	return RHYTHMDB (db);
}

/* must be called with the genres_lock held */
static void
set_entry_album (RhythmDBTree *db,
		 RhythmDBEntry *entry,
		 RhythmDBTreeProperty *artist,
		 RBRefString *name)
{
	struct RhythmDBTreeProperty *prop;

	prop = get_or_create_album (db, artist, name);
	g_hash_table_insert (prop->children, entry, NULL);
	entry->data = prop;
}

static void
rhythmdb_tree_entry_new (RhythmDB *rdb,
			 RhythmDBEntry *entry)
{
	g_mutex_lock (RHYTHMDB_TREE(rdb)->priv->entries_lock);
	rhythmdb_tree_entry_new_internal (rdb, entry);
	g_mutex_unlock (RHYTHMDB_TREE(rdb)->priv->entries_lock);
}

/* must be called with the entry lock held */
static void
rhythmdb_tree_entry_new_internal (RhythmDB *rdb, RhythmDBEntry *entry)
{
	RhythmDBTree *db = RHYTHMDB_TREE (rdb);
	RhythmDBTreeProperty *artist;
	RhythmDBTreeProperty *genre;

	rb_assert_locked (db->priv->entries_lock);
	g_assert (entry != NULL);

	g_return_if_fail (entry->location != NULL);

	if (entry->title == NULL) {
		g_warning ("Entry %s has missing title", rb_refstring_get (entry->location));
		entry->title = rb_refstring_new (_("Unknown"));
	}
	if (entry->artist == NULL) {
		g_warning ("Entry %s has missing artist", rb_refstring_get (entry->location));
		entry->artist = rb_refstring_new (_("Unknown"));
	}
	if (entry->album == NULL) {
		g_warning ("Entry %s has missing album", rb_refstring_get (entry->location));
		entry->album = rb_refstring_new (_("Unknown"));
	}
	if (entry->genre == NULL) {
		g_warning ("Entry %s has missing genre", rb_refstring_get (entry->location));
		entry->genre = rb_refstring_new (_("Unknown"));
	}
	if (entry->mimetype == NULL) {
		g_warning ("Entry %s has missing mimetype", rb_refstring_get (entry->location));
		entry->mimetype = rb_refstring_new ("unknown/unknown");
	}

	/* Initialize the tree structure. */
	g_mutex_lock (db->priv->genres_lock);
	genre = get_or_create_genre (db, entry->type, entry->genre);
	artist = get_or_create_artist (db, genre, entry->artist);
	set_entry_album (db, entry, artist, entry->album);
	g_mutex_unlock (db->priv->genres_lock);

	/* this accounts for the initial reference on the entry */
	g_hash_table_insert (db->priv->entries, entry->location, entry);
	g_hash_table_insert (db->priv->entry_ids, GINT_TO_POINTER (entry->id), entry);

	entry->flags &= ~RHYTHMDB_ENTRY_TREE_LOADING;
}

static RhythmDBTreeProperty *
rhythmdb_tree_property_new (RhythmDBTree *db)
{
	RhythmDBTreeProperty *ret = g_new0 (RhythmDBTreeProperty, 1);
#ifndef G_DISABLE_ASSERT
	ret->magic = 0xf00dbeef;
#endif
	return ret;
}

static GHashTable *
get_genres_hash_for_type (RhythmDBTree *db,
			  RhythmDBEntryType type)
{
	GHashTable *table;

	table = g_hash_table_lookup (db->priv->genres, type);
	if (table == NULL) {
		table = g_hash_table_new_full (rb_refstring_hash,
					       rb_refstring_equal,
					       (GDestroyNotify) rb_refstring_unref,
					       NULL);
		if (table == NULL) {
			g_warning ("Out of memory\n");
			return NULL;
		}
		g_hash_table_insert (db->priv->genres,
				     type,
				     table);
	}
	return table;
}

typedef void (*RBHFunc)(RhythmDBTree *db, GHashTable *genres, gpointer data);

typedef struct {
	RhythmDBTree *db;
	RBHFunc func;
	gpointer data;
} GenresIterCtxt;

static void
genres_process_one (gpointer key,
		    gpointer value,
		    gpointer user_data)
{
	GenresIterCtxt *ctxt = (GenresIterCtxt *)user_data;
	ctxt->func (ctxt->db, (GHashTable *)value, ctxt->data);
}

static void
genres_hash_foreach (RhythmDBTree *db, RBHFunc func, gpointer data)
{
	GenresIterCtxt ctxt;

	ctxt.db = db;
	ctxt.func = func;
	ctxt.data = data;
	g_hash_table_foreach (db->priv->genres, genres_process_one, &ctxt);
}

/* must be called with the genres lock held */
static RhythmDBTreeProperty *
get_or_create_genre (RhythmDBTree *db,
		     RhythmDBEntryType type,
		     RBRefString *name)
{
	RhythmDBTreeProperty *genre;
	GHashTable *table;

	rb_assert_locked (db->priv->genres_lock);

	table = get_genres_hash_for_type (db, type);
	genre = g_hash_table_lookup (table, name);

	if (G_UNLIKELY (genre == NULL)) {
		genre = rhythmdb_tree_property_new (db);
		genre->children = g_hash_table_new_full (rb_refstring_hash, rb_refstring_equal,
							 (GDestroyNotify) rb_refstring_unref,
							 NULL);
		rb_refstring_ref (name);
		g_hash_table_insert (table, name, genre);
		genre->parent = NULL;
	}

	return genre;
}

/* must be called with the genres lock held */
static RhythmDBTreeProperty *
get_or_create_artist (RhythmDBTree *db,
		      RhythmDBTreeProperty *genre,
		      RBRefString *name)
{
	RhythmDBTreeProperty *artist;

	rb_assert_locked (db->priv->genres_lock);

	artist = g_hash_table_lookup (genre->children, name);

	if (G_UNLIKELY (artist == NULL)) {
		artist = rhythmdb_tree_property_new (db);
		artist->children = g_hash_table_new_full (rb_refstring_hash, rb_refstring_equal,
							  (GDestroyNotify) rb_refstring_unref,
							  NULL);
		rb_refstring_ref (name);
		g_hash_table_insert (genre->children, name, artist);
		artist->parent = genre;
	}

	return artist;
}

/* must be called with the genres lock held */
static RhythmDBTreeProperty *
get_or_create_album (RhythmDBTree *db,
		     RhythmDBTreeProperty *artist,
		     RBRefString *name)
{
	RhythmDBTreeProperty *album;

	rb_assert_locked (db->priv->genres_lock);

	album = g_hash_table_lookup (artist->children, name);

	if (G_UNLIKELY (album == NULL)) {
		album = rhythmdb_tree_property_new (db);
		album->children = g_hash_table_new (g_direct_hash, g_direct_equal);
		rb_refstring_ref (name);
		g_hash_table_insert (artist->children, name, album);
		album->parent = artist;
	}

	return album;
}

static gboolean
remove_child (RhythmDBTreeProperty *parent,
	      gconstpointer data)
{
	g_assert (g_hash_table_remove (parent->children, data));
	if (g_hash_table_size (parent->children) <= 0) {
		return TRUE;
	}
	return FALSE;
}

/* must be called with the genres lock held */
static void
remove_entry_from_album (RhythmDBTree *db,
			 RhythmDBEntry *entry)
{
	GHashTable *table;

	rb_assert_locked (db->priv->genres_lock);

	rb_refstring_ref (entry->genre);
	rb_refstring_ref (entry->artist);
	rb_refstring_ref (entry->album);

	table = get_genres_hash_for_type (db, entry->type);
	if (remove_child (RHYTHMDB_TREE_PROPERTY_FROM_ENTRY (entry), entry)) {
		if (remove_child (RHYTHMDB_TREE_PROPERTY_FROM_ENTRY (entry)->parent,
				  entry->album)) {

			if (remove_child (RHYTHMDB_TREE_PROPERTY_FROM_ENTRY (entry)->parent->parent,
					  entry->artist)) {
				destroy_tree_property (RHYTHMDB_TREE_PROPERTY_FROM_ENTRY (entry)->parent->parent);
				g_assert (g_hash_table_remove (table, entry->genre));
			}
			destroy_tree_property (RHYTHMDB_TREE_PROPERTY_FROM_ENTRY (entry)->parent);
		}

		destroy_tree_property (RHYTHMDB_TREE_PROPERTY_FROM_ENTRY (entry));
	}

	rb_refstring_unref (entry->genre);
	rb_refstring_unref (entry->artist);
	rb_refstring_unref (entry->album);
}

static gboolean
rhythmdb_tree_entry_set (RhythmDB *adb,
			 RhythmDBEntry *entry,
			 guint propid,
			 const GValue *value)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	RhythmDBEntryType type;

	type = entry->type;

	/* don't process changes to entries we're loading, we'll get them
	 * when the entry is complete.
	 */
	if (entry->flags & RHYTHMDB_ENTRY_TREE_LOADING)
		return FALSE;

	/* Handle special properties */
	switch (propid)
	{
	case RHYTHMDB_PROP_LOCATION:
	{
		RBRefString *s;
		/* We have to use the string in the entry itself as the hash key,
		 * otherwise either we leak it, or the string vanishes when the
		 * GValue is freed; this means we have to do the entry modification
		 * here, rather than letting rhythmdb_entry_set_internal do it.
		 */
		g_mutex_lock (db->priv->entries_lock);
		g_assert (g_hash_table_remove (db->priv->entries, entry->location));

		s = rb_refstring_new (g_value_get_string (value));
		rb_refstring_unref (entry->location);
		entry->location = s;
		g_hash_table_insert (db->priv->entries, entry->location, entry);
		g_mutex_unlock (db->priv->entries_lock);

		return TRUE;
	}
	case RHYTHMDB_PROP_ALBUM:
	{
		const char *albumname = g_value_get_string (value);

		if (strcmp (rb_refstring_get (entry->album), albumname)) {
			RhythmDBTreeProperty *artist;
			RhythmDBTreeProperty *genre;

			rb_refstring_ref (entry->genre);
			rb_refstring_ref (entry->artist);
			rb_refstring_ref (entry->album);

			g_mutex_lock (db->priv->genres_lock);
			remove_entry_from_album (db, entry);
			genre = get_or_create_genre (db, type, entry->genre);
			artist = get_or_create_artist (db, genre, entry->artist);
			set_entry_album (db, entry, artist, rb_refstring_new (albumname));
			g_mutex_unlock (db->priv->genres_lock);

			rb_refstring_unref (entry->genre);
			rb_refstring_unref (entry->artist);
			rb_refstring_unref (entry->album);
		}
		break;
	}
	case RHYTHMDB_PROP_ARTIST:
	{
		const char *artistname = g_value_get_string (value);

		if (strcmp (rb_refstring_get (entry->artist), artistname)) {
			RhythmDBTreeProperty *new_artist;
			RhythmDBTreeProperty *genre;

			rb_refstring_ref (entry->genre);
			rb_refstring_ref (entry->artist);
			rb_refstring_ref (entry->album);

			g_mutex_lock (db->priv->genres_lock);
			remove_entry_from_album (db, entry);
			genre = get_or_create_genre (db, type, entry->genre);
			new_artist = get_or_create_artist (db, genre,
							   rb_refstring_new (artistname));
			set_entry_album (db, entry, new_artist, entry->album);
			g_mutex_unlock (db->priv->genres_lock);

			rb_refstring_unref (entry->genre);
			rb_refstring_unref (entry->artist);
			rb_refstring_unref (entry->album);
		}
		break;
	}
	case RHYTHMDB_PROP_GENRE:
	{
		const char *genrename = g_value_get_string (value);

		if (strcmp (rb_refstring_get (entry->genre), genrename)) {
			RhythmDBTreeProperty *new_genre;
			RhythmDBTreeProperty *new_artist;

			rb_refstring_ref (entry->genre);
			rb_refstring_ref (entry->artist);
			rb_refstring_ref (entry->album);

			g_mutex_lock (db->priv->genres_lock);
			remove_entry_from_album (db, entry);
			new_genre = get_or_create_genre (db, type,
							 rb_refstring_new (genrename));
			new_artist = get_or_create_artist (db, new_genre, entry->artist);
			set_entry_album (db, entry, new_artist, entry->album);
			g_mutex_unlock (db->priv->genres_lock);

			rb_refstring_unref (entry->genre);
			rb_refstring_unref (entry->artist);
			rb_refstring_unref (entry->album);
		}
		break;
	}
	default:
		break;
	}

	return FALSE;
}

static void
rhythmdb_tree_entry_delete (RhythmDB *adb,
			    RhythmDBEntry *entry)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);

	g_mutex_lock (db->priv->genres_lock);
	remove_entry_from_album (db, entry);
	g_mutex_unlock (db->priv->genres_lock);

	g_mutex_lock (db->priv->entries_lock);
	g_assert (g_hash_table_remove (db->priv->entries, entry->location));
	g_assert (g_hash_table_remove (db->priv->entry_ids, GINT_TO_POINTER (entry->id)));
	rhythmdb_entry_unref (entry);
	g_mutex_unlock (db->priv->entries_lock);
}

typedef struct {
	RhythmDB *db;
	RhythmDBEntryType type;
} RbEntryRemovalCtxt;

/* must be called with the entries and genres locks held */
static gboolean
remove_one_song (gpointer key,
		 RhythmDBEntry *entry,
		 RbEntryRemovalCtxt *ctxt)
{
	rb_assert_locked (RHYTHMDB_TREE (ctxt->db)->priv->entries_lock);
	rb_assert_locked (RHYTHMDB_TREE (ctxt->db)->priv->genres_lock);

	g_return_val_if_fail (entry != NULL, FALSE);

	if (entry->type == ctxt->type) {
		rhythmdb_emit_entry_deleted (ctxt->db, entry);
		remove_entry_from_album (RHYTHMDB_TREE (ctxt->db), entry);
		g_hash_table_remove (RHYTHMDB_TREE (ctxt->db)->priv->entry_ids, GINT_TO_POINTER (entry->id));
		rhythmdb_entry_unref (entry);
		return TRUE;
	}
	return FALSE;
}

static void
rhythmdb_tree_entry_delete_by_type (RhythmDB *adb,
				    RhythmDBEntryType type)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	RbEntryRemovalCtxt ctxt;

	ctxt.db = adb;
	ctxt.type = type;
	g_mutex_lock (db->priv->entries_lock);
	g_mutex_lock (db->priv->genres_lock);
	g_hash_table_foreach_remove (db->priv->entries,
				     (GHRFunc) remove_one_song, &ctxt);
	g_mutex_unlock (db->priv->genres_lock);
	g_mutex_unlock (db->priv->entries_lock);
}

static void
destroy_tree_property (RhythmDBTreeProperty *prop)
{
#ifndef G_DISABLE_ASSERT
	prop->magic = 0xf33df33d;
#endif
	g_hash_table_destroy (prop->children);
	g_free (prop);
}

typedef void (*RhythmDBTreeTraversalFunc) (RhythmDBTree *db, RhythmDBEntry *entry, gpointer data);
typedef void (*RhythmDBTreeAlbumTraversalFunc) (RhythmDBTree *db, RhythmDBTreeProperty *album, gpointer data);

struct RhythmDBTreeTraversalData
{
	RhythmDBTree *db;
	GPtrArray *query;
	RhythmDBTreeTraversalFunc func;
	gpointer data;
	gboolean *cancel;
};

static gboolean
rhythmdb_tree_evaluate_query (RhythmDB *adb,
			      GPtrArray *query,
			      RhythmDBEntry *entry)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	guint i;
	guint last_disjunction;

	for (i = 0, last_disjunction = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);

		if (data->type == RHYTHMDB_QUERY_DISJUNCTION) {
			if (evaluate_conjunctive_subquery (db, query, last_disjunction, i, entry))
				return TRUE;

			last_disjunction = i + 1;
		}
	}
	if (evaluate_conjunctive_subquery (db, query, last_disjunction, query->len, entry))
		return TRUE;
	return FALSE;
}

#define RHYTHMDB_PROPERTY_COMPARE(OP) \
			switch (rhythmdb_get_property_type (db, data->propid)) { \
			case G_TYPE_STRING: \
				if (strcmp (rhythmdb_entry_get_string (entry, data->propid), \
					    g_value_get_string (data->val)) OP 0) \
					return FALSE; \
				break; \
			case G_TYPE_ULONG: \
				if (rhythmdb_entry_get_ulong (entry, data->propid) OP \
				    g_value_get_ulong (data->val)) \
					return FALSE; \
				break; \
			case G_TYPE_BOOLEAN: \
				if (rhythmdb_entry_get_boolean (entry, data->propid) OP \
				    g_value_get_boolean (data->val)) \
					return FALSE; \
				break; \
			case G_TYPE_UINT64: \
				if (rhythmdb_entry_get_uint64 (entry, data->propid) OP \
				    g_value_get_uint64 (data->val)) \
					return FALSE; \
				break; \
			case G_TYPE_DOUBLE: \
				if (rhythmdb_entry_get_double (entry, data->propid) OP \
				    g_value_get_double (data->val)) \
					return FALSE; \
				break; \
                        case G_TYPE_POINTER: \
                                if (rhythmdb_entry_get_pointer (entry, data->propid) OP \
                                    g_value_get_pointer (data->val)) \
                                        return FALSE; \
                                break; \
			default: \
                                g_warning ("Unexpected type: %s", g_type_name (rhythmdb_get_property_type (db, data->propid))); \
				g_assert_not_reached (); \
			}

static gboolean
search_match_properties (RhythmDB *db,
			 RhythmDBEntry *entry,
			 gchar **words)
{
	const RhythmDBPropType props[] = {
		RHYTHMDB_PROP_TITLE_FOLDED,
		RHYTHMDB_PROP_ALBUM_FOLDED,
		RHYTHMDB_PROP_ARTIST_FOLDED,
		RHYTHMDB_PROP_GENRE_FOLDED
	};
	gboolean islike = TRUE;
	gchar **current;
	int i;

	for (current = words; *current != NULL; current++) {
		gboolean word_found = FALSE;

		for (i = 0; i < G_N_ELEMENTS (props); i++) {
			const char *entry_string = rhythmdb_entry_get_string (entry, props[i]);
			if (entry_string && (strstr (entry_string, *current) != NULL)) {
				/* the word was found, go to the next one */
				word_found = TRUE;
				break;
			}
		}
		if (!word_found) {
			/* the word wasn't in any of the properties*/
			islike = FALSE;
			break;
		}
	}

	return islike;
}

static gboolean
evaluate_conjunctive_subquery (RhythmDBTree *dbtree,
			       GPtrArray *query,
			       guint base,
			       guint max,
			       RhythmDBEntry *entry)

{
	RhythmDB *db = (RhythmDB *) dbtree;
	guint i;
/* Optimization possibility - we may get here without actually having
 * anything in the query.  It would be faster to instead just merge
 * the child hash table into the query result hash.
 */
	for (i = base; i < max; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);

		switch (data->type) {
		case RHYTHMDB_QUERY_SUBQUERY:
		{
			gboolean matched = FALSE;
			GList *conjunctions = split_query_by_disjunctions (dbtree, data->subquery);
			GList *tem;

			if (conjunctions == NULL)
				matched = TRUE;

			for (tem = conjunctions; tem; tem = tem->next) {
				GPtrArray *subquery = tem->data;
				if (!matched && evaluate_conjunctive_subquery (dbtree, subquery,
									       0, subquery->len,
									       entry)) {
					matched = TRUE;
				}
				g_ptr_array_free (tem->data, TRUE);
			}
			g_list_free (conjunctions);
			if (!matched)
				return FALSE;
		}
		break;
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN:
		case RHYTHMDB_QUERY_PROP_CURRENT_TIME_NOT_WITHIN:
		{
			gulong relative_time;
			GTimeVal current_time;

			g_assert (rhythmdb_get_property_type (db, data->propid) == G_TYPE_ULONG);

			relative_time = g_value_get_ulong (data->val);
			g_get_current_time  (&current_time);

			if (data->type == RHYTHMDB_QUERY_PROP_CURRENT_TIME_WITHIN) {
				if (!(rhythmdb_entry_get_ulong (entry, data->propid) >= (current_time.tv_sec - relative_time)))
					return FALSE;
			} else {
				if (!(rhythmdb_entry_get_ulong (entry, data->propid) < (current_time.tv_sec - relative_time)))
					return FALSE;
			}
			break;
		}
		case RHYTHMDB_QUERY_PROP_PREFIX:
		case RHYTHMDB_QUERY_PROP_SUFFIX:
		{
			const char *value_s;
			const char *entry_s;

			g_assert (rhythmdb_get_property_type (db, data->propid) == G_TYPE_STRING);

			value_s = g_value_get_string (data->val);
			entry_s = rhythmdb_entry_get_string (entry, data->propid);

			if (data->type == RHYTHMDB_QUERY_PROP_PREFIX && !g_str_has_prefix (entry_s, value_s))
				return FALSE;
			if (data->type == RHYTHMDB_QUERY_PROP_SUFFIX && !g_str_has_suffix (entry_s, value_s))
				return FALSE;

			break;
		}
		case RHYTHMDB_QUERY_PROP_LIKE:
		case RHYTHMDB_QUERY_PROP_NOT_LIKE:
		{
			if (rhythmdb_get_property_type (db, data->propid) == G_TYPE_STRING) {
				gboolean islike;

				if (data->propid == RHYTHMDB_PROP_SEARCH_MATCH) {
					/* this is a special property, that should match several things */
					islike = search_match_properties (db, entry, g_value_get_boxed (data->val));

				} else {
					const gchar *value_string = g_value_get_string (data->val);
					const char *entry_string = rhythmdb_entry_get_string (entry, data->propid);

					/* check in case the property is NULL, the value should never be NULL */
					if (entry_string == NULL)
						return FALSE;

					islike = (strstr (entry_string, value_string) != NULL);
				}

				if ((data->type == RHYTHMDB_QUERY_PROP_LIKE) ^ islike)
					return FALSE;
				else
					continue;
				break;
			}
			/* Fall through */
		}
		case RHYTHMDB_QUERY_PROP_EQUALS:
			RHYTHMDB_PROPERTY_COMPARE (!=)
			break;
		case RHYTHMDB_QUERY_PROP_GREATER:
			RHYTHMDB_PROPERTY_COMPARE (<)
			break;
		case RHYTHMDB_QUERY_PROP_LESS:
			RHYTHMDB_PROPERTY_COMPARE (>)
			break;
		case RHYTHMDB_QUERY_END:
		case RHYTHMDB_QUERY_DISJUNCTION:
		case RHYTHMDB_QUERY_PROP_YEAR_EQUALS:
		case RHYTHMDB_QUERY_PROP_YEAR_LESS:
		case RHYTHMDB_QUERY_PROP_YEAR_GREATER:
			g_assert_not_reached ();
			break;
		}
	}
	return TRUE;
}

static void
do_conjunction (RhythmDBEntry *entry,
		gpointer unused,
		struct RhythmDBTreeTraversalData *data)
{
	if (G_UNLIKELY (*data->cancel))
		return;
	/* Finally, we actually evaluate the query! */
	if (evaluate_conjunctive_subquery (data->db, data->query, 0, data->query->len,
					   entry)) {
		data->func (data->db, entry, data->data);
	}
}

static void
conjunctive_query_songs (const char *name,
			 RhythmDBTreeProperty *album,
			 struct RhythmDBTreeTraversalData *data)
{
	if (G_UNLIKELY (*data->cancel))
		return;
	g_hash_table_foreach (album->children, (GHFunc) do_conjunction, data);
}

static GPtrArray *
clone_remove_ptr_array_index (GPtrArray *arr,
			      guint index)
{
	GPtrArray *ret = g_ptr_array_new ();
	guint i;
	for (i = 0; i < arr->len; i++)
		if (i != index)
			g_ptr_array_add (ret, g_ptr_array_index (arr, i));

	return ret;
}

static void
conjunctive_query_albums (const char *name,
			  RhythmDBTreeProperty *artist,
			  struct RhythmDBTreeTraversalData *data)
{
	guint i;
	int album_query_idx = -1;

	if (G_UNLIKELY (*data->cancel))
		return;

	for (i = 0; i < data->query->len; i++) {
		RhythmDBQueryData *qdata = g_ptr_array_index (data->query, i);
		if (qdata->type == RHYTHMDB_QUERY_PROP_EQUALS
		    && qdata->propid == RHYTHMDB_PROP_ALBUM) {
			if (album_query_idx > 0)
				return;
			album_query_idx = i;

		}
	}

	if (album_query_idx >= 0) {
		RhythmDBTreeProperty *album;
		RhythmDBQueryData *qdata = g_ptr_array_index (data->query, album_query_idx);
		RBRefString *albumname = rb_refstring_new (g_value_get_string (qdata->val));
		GPtrArray *oldquery = data->query;

		data->query = clone_remove_ptr_array_index (data->query, album_query_idx);

		album = g_hash_table_lookup (artist->children, albumname);

		if (album != NULL) {
			conjunctive_query_songs (rb_refstring_get (albumname), album, data);
		}
		g_ptr_array_free (data->query, TRUE);
		data->query = oldquery;
		return;
	}

	g_hash_table_foreach (artist->children, (GHFunc) conjunctive_query_songs, data);
}

static void
conjunctive_query_artists (const char *name,
			   RhythmDBTreeProperty *genre,
			   struct RhythmDBTreeTraversalData *data)
{
	guint i;
	int artist_query_idx = -1;

	if (G_UNLIKELY (*data->cancel))
		return;

	for (i = 0; i < data->query->len; i++) {
		RhythmDBQueryData *qdata = g_ptr_array_index (data->query, i);
		if (qdata->type == RHYTHMDB_QUERY_PROP_EQUALS
		    && qdata->propid == RHYTHMDB_PROP_ARTIST) {
			if (artist_query_idx > 0)
				return;
			artist_query_idx = i;

		}
	}

	if (artist_query_idx >= 0) {
		RhythmDBTreeProperty *artist;
		RhythmDBQueryData *qdata = g_ptr_array_index (data->query, artist_query_idx);
		RBRefString *artistname = rb_refstring_new (g_value_get_string (qdata->val));
		GPtrArray *oldquery = data->query;

		data->query = clone_remove_ptr_array_index (data->query, artist_query_idx);

		artist = g_hash_table_lookup (genre->children, artistname);
		if (artist != NULL) {
			conjunctive_query_albums (rb_refstring_get (artistname), artist, data);
		}
		g_ptr_array_free (data->query, TRUE);
		data->query = oldquery;
		return;
	}

	g_hash_table_foreach (genre->children, (GHFunc) conjunctive_query_albums, data);
}

static void
conjunctive_query_genre (RhythmDBTree *db,
			 GHashTable *genres,
			 struct RhythmDBTreeTraversalData *data)
{
	int genre_query_idx = -1;
	guint i;

	if (G_UNLIKELY (*data->cancel))
		return;

	for (i = 0; i < data->query->len; i++) {
		RhythmDBQueryData *qdata = g_ptr_array_index (data->query, i);
		if (qdata->type == RHYTHMDB_QUERY_PROP_EQUALS
		    && qdata->propid == RHYTHMDB_PROP_GENRE) {
			/* A song can't currently have two genres.  So
			 * if we get a conjunctive query for that, we
			 * know the result must be the empty set. */
			if (genre_query_idx > 0)
				return;
			genre_query_idx = i;

		}
	}

	if (genre_query_idx >= 0) {
		RhythmDBTreeProperty *genre;
		RhythmDBQueryData *qdata = g_ptr_array_index (data->query, genre_query_idx);
		RBRefString *genrename = rb_refstring_new (g_value_get_string (qdata->val));
		GPtrArray *oldquery = data->query;

		data->query = clone_remove_ptr_array_index (data->query, genre_query_idx);

		genre = g_hash_table_lookup (genres, genrename);
		if (genre != NULL) {
			conjunctive_query_artists (rb_refstring_get (genrename), genre, data);
		}
		g_ptr_array_free (data->query, TRUE);
		data->query = oldquery;
		return;
	}

	g_hash_table_foreach (genres, (GHFunc) conjunctive_query_artists, data);
}

static void
conjunctive_query (RhythmDBTree *db,
		   GPtrArray *query,
		   RhythmDBTreeTraversalFunc func,
		   gpointer data,
		   gboolean *cancel)
{
	int type_query_idx = -1;
	guint i;
	struct RhythmDBTreeTraversalData *traversal_data;

	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *qdata = g_ptr_array_index (query, i);
		if (qdata->type == RHYTHMDB_QUERY_PROP_EQUALS
		    && qdata->propid == RHYTHMDB_PROP_TYPE) {
			/* A song can't have two types. */
			if (type_query_idx > 0)
				return;
			type_query_idx = i;
		}
	}

	traversal_data = g_new (struct RhythmDBTreeTraversalData, 1);
	traversal_data->db = db;
	traversal_data->query = query;
	traversal_data->func = func;
	traversal_data->data = data;
	traversal_data->cancel = cancel;

	g_mutex_lock (db->priv->genres_lock);
	if (type_query_idx >= 0) {
		GHashTable *genres;
		RhythmDBEntryType etype;
		RhythmDBQueryData *qdata = g_ptr_array_index (query, type_query_idx);

		g_ptr_array_remove_index_fast (query, type_query_idx);

		etype = g_value_get_pointer (qdata->val);
		genres = get_genres_hash_for_type (db, etype);
		if (genres != NULL) {
			conjunctive_query_genre (db, genres, traversal_data);
		} else {
			g_assert_not_reached ();
		}
	} else {
		/* FIXME */
		/* No type was given; punt and query everything */
		genres_hash_foreach (db, (RBHFunc)conjunctive_query_genre,
				     traversal_data);
	}
	g_mutex_unlock (db->priv->genres_lock);

	g_free (traversal_data);
}

static GList *
split_query_by_disjunctions (RhythmDBTree *db,
			     GPtrArray *query)
{
	GList *conjunctions = NULL;
	guint i, j;
	guint last_disjunction = 0;
	GPtrArray *subquery = g_ptr_array_new ();

	for (i = 0; i < query->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (query, i);
		if (data->type == RHYTHMDB_QUERY_DISJUNCTION) {

			/* Copy the subquery */
			for (j = last_disjunction; j < i; j++) {
				g_ptr_array_add (subquery, g_ptr_array_index (query, j));
			}

			conjunctions = g_list_prepend (conjunctions, subquery);
			last_disjunction = i+1;
			g_assert (subquery->len > 0);
			subquery = g_ptr_array_new ();
		}
	}

	/* Copy the last subquery, except for the QUERY_END */
	for (i = last_disjunction; i < query->len; i++) {
		g_ptr_array_add (subquery, g_ptr_array_index (query, i));
	}

	if (subquery->len > 0)
		conjunctions = g_list_prepend (conjunctions, subquery);
	else
		g_ptr_array_free (subquery, TRUE);

	return conjunctions;
}

struct RhythmDBTreeQueryGatheringData
{
	RhythmDBTree *db;
	GPtrArray *queue;
	GHashTable *entries;
	RhythmDBQueryResults *results;
};

static void
do_query_recurse (RhythmDBTree *db,
		  GPtrArray *query,
		  RhythmDBTreeTraversalFunc func,
		  struct RhythmDBTreeQueryGatheringData *data,
		  gboolean *cancel)
{
	GList *conjunctions, *tem;

	if (query == NULL)
		return;

	conjunctions = split_query_by_disjunctions (db, query);
	rb_debug ("doing recursive query, %d conjunctions", g_list_length (conjunctions));

	if (conjunctions == NULL)
		return;

	/* If there is a disjunction involved, we must uniquify the entry hits. */
	if (conjunctions->next != NULL)
		data->entries = g_hash_table_new (g_direct_hash, g_direct_equal);
	else
		data->entries = NULL;

	for (tem = conjunctions; tem; tem = tem->next) {
		if (G_UNLIKELY (*cancel))
			break;
		conjunctive_query (db, tem->data, func, data, cancel);
		g_ptr_array_free (tem->data, TRUE);
	}

	if (data->entries != NULL)
		g_hash_table_destroy (data->entries);

	g_list_free (conjunctions);
}

static void
handle_entry_match (RhythmDB *db,
		    RhythmDBEntry *entry,
		    struct RhythmDBTreeQueryGatheringData *data)
{

	if (data->entries
	    && g_hash_table_lookup (data->entries, entry))
		return;

	g_ptr_array_add (data->queue, entry);
	if (data->queue->len > RHYTHMDB_QUERY_MODEL_SUGGESTED_UPDATE_CHUNK) {
		rhythmdb_query_results_add_results (data->results, data->queue);
		data->queue = g_ptr_array_new ();
	}
}

static void
rhythmdb_tree_do_full_query (RhythmDB *adb,
			     GPtrArray *query,
			     RhythmDBQueryResults *results,
			     gboolean *cancel)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	struct RhythmDBTreeQueryGatheringData *data = g_new0 (struct RhythmDBTreeQueryGatheringData, 1);

	data->results = results;
	data->queue = g_ptr_array_new ();

	do_query_recurse (db, query, (RhythmDBTreeTraversalFunc) handle_entry_match, data, cancel);

	rhythmdb_query_results_add_results (data->results, data->queue);

	g_free (data);
}

static RhythmDBEntry *
rhythmdb_tree_entry_lookup_by_location (RhythmDB *adb,
					RBRefString *uri)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	RhythmDBEntry *entry;

	g_mutex_lock (db->priv->entries_lock);
	entry = g_hash_table_lookup (db->priv->entries, uri);
	g_mutex_unlock (db->priv->entries_lock);

	return entry;
}

static RhythmDBEntry *
rhythmdb_tree_entry_lookup_by_id (RhythmDB *adb,
				  gint id)
{
	RhythmDBTree *db = RHYTHMDB_TREE (adb);
	RhythmDBEntry *entry;

	g_mutex_lock (db->priv->entries_lock);
	entry = g_hash_table_lookup (db->priv->entry_ids, GINT_TO_POINTER (id));
	g_mutex_unlock (db->priv->entries_lock);

	return entry;
}

struct RhythmDBEntryForeachCtxt
{
	RhythmDBTree *db;
	GFunc func;
	gpointer user_data;
};

static void
rhythmdb_tree_entry_foreach_func (gpointer key, RhythmDBEntry *val, GPtrArray *list)
{
	rhythmdb_entry_ref (val);
	g_ptr_array_add (list, val);
}

static void
rhythmdb_tree_entry_foreach (RhythmDB *rdb, GFunc foreach_func, gpointer user_data)
{
	RhythmDBTree *db = RHYTHMDB_TREE (rdb);
	GPtrArray *list;
	guint size, i;

	g_mutex_lock (db->priv->entries_lock);
	size = g_hash_table_size (db->priv->entries);
	list = g_ptr_array_sized_new (size);
	g_hash_table_foreach (db->priv->entries, (GHFunc)rhythmdb_tree_entry_foreach_func, list);
	g_mutex_unlock (db->priv->entries_lock);

	for (i = 0; i < size; i++) {
		RhythmDBEntry *entry = (RhythmDBEntry*)g_ptr_array_index (list, i);
		(*foreach_func) (entry, user_data);
		rhythmdb_entry_unref (entry);
	}

	g_ptr_array_free (list, TRUE);
}

static gint64
rhythmdb_tree_entry_count (RhythmDB *rdb)
{
	RhythmDBTree *db = RHYTHMDB_TREE (rdb);
	return g_hash_table_size (db->priv->entries);
}

static void
rhythmdb_tree_entry_foreach_by_type (RhythmDB *db,
				     RhythmDBEntryType type,
				     GFunc foreach_func,
				     gpointer data)
{
	rhythmdb_hash_tree_foreach (db, type,
				    (RBTreeEntryItFunc) foreach_func,
				    NULL, NULL, NULL, data);
}

static void
count_entries (RhythmDB *db, RhythmDBTreeProperty *album, gint64 *count)
{
	*count += g_hash_table_size (album->children);
}

static gint64
rhythmdb_tree_entry_count_by_type (RhythmDB *db,
				   RhythmDBEntryType type)
{
	gint64 count = 0;
	rhythmdb_hash_tree_foreach (db, type,
				    NULL, (RBTreePropertyItFunc) count_entries, NULL, NULL,
				    &count);
	return count;
}


struct HashTreeIteratorCtxt {
	RhythmDBTree *db;
	RBTreeEntryItFunc entry_func;
	RBTreePropertyItFunc album_func;
	RBTreePropertyItFunc artist_func;
	RBTreePropertyItFunc genres_func;
	gpointer data;
};

static void
hash_tree_entries_foreach (gpointer key,
			   gpointer value,
			   gpointer data)
{
	RhythmDBEntry *entry = (RhythmDBEntry *) key;
	struct HashTreeIteratorCtxt *ctxt = (struct HashTreeIteratorCtxt*)data;

	g_assert (ctxt->entry_func);

	ctxt->entry_func (ctxt->db, entry, ctxt->data);
}

static void
hash_tree_albums_foreach (gpointer key,
			  gpointer value,
			  gpointer data)
{
	RhythmDBTreeProperty *album = (RhythmDBTreeProperty *)value;
	struct HashTreeIteratorCtxt *ctxt = (struct HashTreeIteratorCtxt*)data;

	if (ctxt->album_func) {
		ctxt->album_func (ctxt->db, album, ctxt->data);
	}
	if (ctxt->entry_func != NULL) {
		g_hash_table_foreach (album->children,
				      hash_tree_entries_foreach,
				      ctxt);
	}
}

static void
hash_tree_artists_foreach (gpointer key,
			   gpointer value,
			   gpointer data)
{
	RhythmDBTreeProperty *artist = (RhythmDBTreeProperty *)value;
	struct HashTreeIteratorCtxt *ctxt = (struct HashTreeIteratorCtxt*)data;

	if (ctxt->artist_func) {
		ctxt->artist_func (ctxt->db, artist, ctxt->data);
	}
	if ((ctxt->album_func != NULL) || (ctxt->entry_func != NULL)) {
		g_hash_table_foreach (artist->children,
				      hash_tree_albums_foreach,
				      ctxt);
	}
}

static void
hash_tree_genres_foreach (gpointer key,
			  gpointer value,
			  gpointer data)
{
	RhythmDBTreeProperty *genre = (RhythmDBTreeProperty *)value;
	struct HashTreeIteratorCtxt *ctxt = (struct HashTreeIteratorCtxt*)data;

	if (ctxt->genres_func) {
		ctxt->genres_func (ctxt->db, genre, ctxt->data);
	}

	if ((ctxt->album_func != NULL)
	    || (ctxt->artist_func != NULL)
	    || (ctxt->entry_func != NULL)) {
		g_hash_table_foreach (genre->children,
				      hash_tree_artists_foreach,
				      ctxt);
	}
}

static void
rhythmdb_hash_tree_foreach (RhythmDB *adb,
			    RhythmDBEntryType type,
			    RBTreeEntryItFunc entry_func,
			    RBTreePropertyItFunc album_func,
			    RBTreePropertyItFunc artist_func,
			    RBTreePropertyItFunc genres_func,
			    gpointer data)
{
	struct HashTreeIteratorCtxt ctxt;
	GHashTable *table;

	ctxt.db = RHYTHMDB_TREE (adb);
	ctxt.album_func = album_func;
	ctxt.artist_func = artist_func;
	ctxt.genres_func = genres_func;
	ctxt.entry_func = entry_func;
	ctxt.data = data;

	g_mutex_lock (ctxt.db->priv->genres_lock);
	table = get_genres_hash_for_type (RHYTHMDB_TREE (adb), type);
	if (table == NULL) {
		return;
	}
	if ((ctxt.album_func != NULL)
	    || (ctxt.artist_func != NULL)
	    || (ctxt.genres_func != NULL)
	    || (ctxt.entry_func != NULL)) {
		g_hash_table_foreach (table, hash_tree_genres_foreach, &ctxt);
	}
	g_mutex_unlock (ctxt.db->priv->genres_lock);
}

static void
rhythmdb_tree_entry_type_registered (RhythmDB *db,
				     const char *name,
				     RhythmDBEntryType entry_type)
{
	GList *entries = NULL;
	GList *e;
	gint count = 0;
	RhythmDBTree *rdb;
	RBRefString *rs_name;

	if (name == NULL)
		return;

	rdb = RHYTHMDB_TREE (db);
	rs_name = rb_refstring_find (name);
	if (rs_name)
		entries = g_hash_table_lookup (rdb->priv->unknown_entry_types, rs_name);
	if (entries == NULL) {
		rb_refstring_unref (rs_name);
		rb_debug ("no entries of newly registered type %s loaded from db", name);
		return;
	}

	for (e = entries; e != NULL; e = e->next) {
		RhythmDBUnknownEntry *data;
		RhythmDBEntry *entry;
		GList *p;

		data = (RhythmDBUnknownEntry *)e->data;
		entry = rhythmdb_entry_allocate (db, entry_type);
		entry->flags |= RHYTHMDB_ENTRY_TREE_LOADING;
		for (p = data->properties; p != NULL; p = p->next) {
			RhythmDBUnknownEntryProperty *prop;
			RhythmDBPropType propid;
			GValue value = {0,};

			prop = (RhythmDBUnknownEntryProperty *) p->data;
			propid = rhythmdb_propid_from_nice_elt_name (db, (const xmlChar *) rb_refstring_get (prop->name));

			rhythmdb_read_encoded_property (db, rb_refstring_get (prop->value), propid, &value);
			rhythmdb_entry_set_internal (db, entry, FALSE, propid, &value);
			g_value_unset (&value);
		}
		rhythmdb_tree_entry_new_internal (db, entry);
		rhythmdb_entry_insert (db, entry);
		count++;
	}
	rb_debug ("handled %d entries of newly registered type %s", count, name);
	rhythmdb_commit (db);

	g_hash_table_remove (rdb->priv->unknown_entry_types, rs_name);
	free_unknown_entries (rs_name, entries, NULL);
	rb_refstring_unref (rs_name);
}
