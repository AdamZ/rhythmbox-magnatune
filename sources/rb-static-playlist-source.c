/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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

#include <config.h>
#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libxml/tree.h>
#include <string.h>

#include "rb-static-playlist-source.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-stock-icons.h"
#include "rb-file-helpers.h"
#include "rb-playlist-xml.h"

static GObject *rb_static_playlist_source_constructor (GType type, guint n_construct_properties,
						       GObjectConstructParam *construct_properties);
static void rb_static_playlist_source_finalize (GObject *object);

/* source methods */
static GList * impl_cut (RBSource *source);
static void impl_paste (RBSource *asource, GList *entries);
static void impl_delete (RBSource *source);
static void impl_search (RBSource *asource, const char *search_text);
static void impl_reset_filters (RBSource *asource);
static gboolean impl_receive_drag (RBSource *source, GtkSelectionData *data);

/* playlist methods */
static void impl_save_contents_to_xml (RBPlaylistSource *source,
				       xmlNodePtr node);

static void rb_static_playlist_source_do_query (RBStaticPlaylistSource *source);

static void rb_static_playlist_source_add_list_uri (RBStaticPlaylistSource *source,
						    GList *list);
static void rb_static_playlist_source_row_inserted (GtkTreeModel *model,
						    GtkTreePath *path,
						    GtkTreeIter *iter,
						    RBStaticPlaylistSource *source);
static void rb_static_playlist_source_non_entry_dropped (GtkTreeModel *model,
							 const char *uri,
							 int position,
							 RBStaticPlaylistSource *source);

G_DEFINE_TYPE (RBStaticPlaylistSource, rb_static_playlist_source, RB_TYPE_PLAYLIST_SOURCE)
#define RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), \
								RB_TYPE_STATIC_PLAYLIST_SOURCE, \
								RBStaticPlaylistSourcePrivate))


typedef struct
{
	RhythmDBQueryModel *base_model;
	RhythmDBQueryModel *filter_model;

	char *search_text;
} RBStaticPlaylistSourcePrivate;


	
static void
rb_static_playlist_source_class_init (RBStaticPlaylistSourceClass *klass)
{
	gint size;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBPlaylistSourceClass *playlist_class = RB_PLAYLIST_SOURCE_CLASS (klass);
	
	object_class->constructor = rb_static_playlist_source_constructor;
	object_class->finalize = rb_static_playlist_source_finalize;

	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_cut = impl_cut;
	source_class->impl_paste = impl_paste;
	source_class->impl_delete = impl_delete;
	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_search = impl_search;
	source_class->impl_reset_filters = impl_reset_filters;

	playlist_class->impl_save_contents_to_xml = impl_save_contents_to_xml;
	
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	klass->pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
						  GNOME_MEDIA_PLAYLIST,
						  size,
						  0, NULL);
	
	g_type_class_add_private (klass, sizeof (RBStaticPlaylistSourcePrivate));
}

static void
rb_static_playlist_source_init (RBStaticPlaylistSource *source)
{
	RBStaticPlaylistSourceClass *klass = RB_STATIC_PLAYLIST_SOURCE_GET_CLASS (source);

	rb_source_set_pixbuf (RB_SOURCE (source), klass->pixbuf);
}

static void
rb_static_playlist_source_finalize (GObject *object)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (object);

	g_free (priv->search_text);

	G_OBJECT_CLASS (rb_static_playlist_source_parent_class)->finalize (object);
}

static GObject *
rb_static_playlist_source_constructor (GType type, guint n_construct_properties,
				       GObjectConstructParam *construct_properties)
{
	GObjectClass *parent_class = G_OBJECT_CLASS (rb_static_playlist_source_parent_class);
	RBStaticPlaylistSource *source = RB_STATIC_PLAYLIST_SOURCE (
			parent_class->constructor (type, n_construct_properties, construct_properties));
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);

	priv->base_model = rb_playlist_source_get_query_model (RB_PLAYLIST_SOURCE (psource));
	g_object_set (G_OBJECT (priv->base_model), "show-hidden", TRUE, NULL);
	
	rb_static_playlist_source_do_query (source);

	/* watch these to find out when things are dropped into the entry view */
	g_signal_connect_object (G_OBJECT (priv->base_model), "row-inserted",
			 G_CALLBACK (rb_static_playlist_source_row_inserted),
			 source, 0);
	g_signal_connect_object (G_OBJECT (priv->base_model), "non-entry-dropped",
			 G_CALLBACK (rb_static_playlist_source_non_entry_dropped),
			 source, 0);

	return G_OBJECT (source);
}

RBSource *
rb_static_playlist_source_new (RBShell *shell, const char *name, gboolean local, RhythmDBEntryType entry_type)
{
	if (name == NULL)
		name = "";

	return RB_SOURCE (g_object_new (RB_TYPE_STATIC_PLAYLIST_SOURCE,
					"name", name,
					"shell", shell,
					"is-local", local,
					"entry-type", entry_type,
					NULL));
}

void
rb_static_playlist_source_load_from_xml (RBStaticPlaylistSource *source, xmlNodePtr node)
{
	xmlNodePtr child;

	for (child = node->children; child; child = child->next) {
		xmlChar *location;

		if (xmlNodeIsText (child))
			continue;
	
		if (xmlStrcmp (child->name, RB_PLAYLIST_LOCATION))
			continue;
	
		location = xmlNodeGetContent (child);
		rb_static_playlist_source_add_location (source,
						        (char *) location, -1);
	}
}


RBSource *	
rb_static_playlist_source_new_from_xml (RBShell *shell, xmlNodePtr node)
{
	RBSource *psource = rb_static_playlist_source_new (shell,
							   NULL,
							   TRUE,
							   RHYTHMDB_ENTRY_TYPE_SONG);
	RBStaticPlaylistSource *source = RB_STATIC_PLAYLIST_SOURCE (psource);

	rb_static_playlist_source_load_from_xml (source, node);

	return RB_SOURCE (source);
}

static GList *
impl_cut (RBSource *asource)
{
	RBStaticPlaylistSource *source = RB_STATIC_PLAYLIST_SOURCE (asource);
	RBEntryView *songs = rb_source_get_entry_view (asource);
	GList *sel = rb_entry_view_get_selected_entries (songs);
	GList *tem;

	for (tem = sel; tem; tem = tem->next)
		rb_static_playlist_source_remove_entry (source, (RhythmDBEntry *) tem->data);

	return sel;
}

static void
impl_paste (RBSource *asource, GList *entries)
{
	RBStaticPlaylistSource *source = RB_STATIC_PLAYLIST_SOURCE (asource);

	for (; entries; entries = g_list_next (entries))
		rb_static_playlist_source_add_entry (source, entries->data, -1);
}

static void
impl_delete (RBSource *asource)
{
	RBEntryView *songs = rb_source_get_entry_view (asource);
	RBStaticPlaylistSource *source = RB_STATIC_PLAYLIST_SOURCE (asource);
	GList *sel, *tem;

	sel = rb_entry_view_get_selected_entries (songs);
	for (tem = sel; tem != NULL; tem = tem->next) {
		rb_static_playlist_source_remove_entry (source, (RhythmDBEntry *) tem->data);
	}
	g_list_free (sel);
}

static void
impl_reset_filters (RBSource *source)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
	gboolean changed = FALSE;

	if (priv->search_text != NULL) {
		changed = TRUE;
		g_free (priv->search_text);
		priv->search_text = NULL;
	}

	if (changed) {
		rb_static_playlist_source_do_query (RB_STATIC_PLAYLIST_SOURCE (source));
		rb_source_notify_filter_changed (source);
	}
}

static void
impl_search (RBSource *source, const char *search_text)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);

	if (search_text == NULL && priv->search_text == NULL)
		return;
	if (search_text != NULL && priv->search_text != NULL &&
	    !strcmp (search_text, priv->search_text))
		return;

	if (search_text[0] == '\0')
		search_text = NULL;

	rb_debug ("doing search for \"%s\"", search_text ? search_text : "(NULL)");

	g_free (priv->search_text);
	priv->search_text = g_strdup (search_text);
	rb_static_playlist_source_do_query (RB_STATIC_PLAYLIST_SOURCE (source));

	rb_source_notify_filter_changed (source);
}

static void
rb_static_playlist_source_do_query (RBStaticPlaylistSource *source)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);
	RhythmDB *db = rb_playlist_source_get_db (psource);
	GPtrArray *query = NULL;
	
	if (priv->filter_model)
		g_object_unref (priv->filter_model);
	priv->filter_model = rhythmdb_query_model_new_empty (db);
	g_object_set (G_OBJECT (priv->filter_model), "base-model", priv->base_model, NULL);

	query = g_ptr_array_new();

	if (priv->search_text) {
		rhythmdb_query_append (db,
				       query,
				       RHYTHMDB_QUERY_PROP_LIKE, RHYTHMDB_PROP_SEARCH_MATCH, priv->search_text,
				       RHYTHMDB_QUERY_END);
	}

	g_object_set (G_OBJECT (priv->filter_model), "query", query, NULL);
	rhythmdb_query_free (query);
	rhythmdb_query_model_reapply_query (priv->filter_model, TRUE);
	rb_playlist_source_set_query_model (psource, priv->filter_model);
}


static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *data)
{
	GList *list;
	RBStaticPlaylistSource *source = RB_STATIC_PLAYLIST_SOURCE (asource);

        if (data->type == gdk_atom_intern ("text/uri-list", TRUE)) {
                list = gnome_vfs_uri_list_parse ((char *) data->data);

                if (list != NULL)
                        rb_static_playlist_source_add_list_uri (source, list);
                else
                        return FALSE;
	}

        return TRUE;
}

static void 
impl_save_contents_to_xml (RBPlaylistSource *source,
			   xmlNodePtr node)
{
	RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
	GtkTreeIter iter;

	xmlSetProp (node, RB_PLAYLIST_TYPE, RB_PLAYLIST_STATIC);
	
	if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->base_model), &iter))
		return;

	do { 
		xmlNodePtr child_node = xmlNewChild (node, NULL, RB_PLAYLIST_LOCATION, NULL);
		RhythmDBEntry *entry;
		xmlChar *encoded;

		gtk_tree_model_get (GTK_TREE_MODEL (priv->base_model), &iter, 0, &entry, -1);

		encoded = xmlEncodeEntitiesReentrant (NULL, BAD_CAST entry->location);

		xmlNodeSetContent (child_node, encoded);
		g_free (encoded);
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (priv->base_model), &iter));
}

static void
rb_static_playlist_source_add_list_uri (RBStaticPlaylistSource *source,
					GList *list)
{
	GList *i, *uri_list = NULL;
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);
	RhythmDBEntry *entry;

	g_return_if_fail (list != NULL);

	for (i = list; i != NULL; i = g_list_next (i)) {
		char *uri = gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0);
		uri_list = g_list_prepend (uri_list, rb_canonicalise_uri (uri));
		g_free (uri);
	}
	uri_list = g_list_reverse (uri_list);

	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL)
		return;

	for (i = uri_list; i != NULL; i = i->next) {
		char *uri = i->data;
		if (uri != NULL) {
			entry = rhythmdb_entry_lookup_by_location (rb_playlist_source_get_db (psource), uri);
			if (entry == NULL)
				rhythmdb_add_uri (rb_playlist_source_get_db (psource), uri);

			rb_static_playlist_source_add_location (source, uri, -1);
		}

		g_free (uri);
	}
	g_list_free (uri_list);
}

static void
rb_static_playlist_source_add_location_internal (RBStaticPlaylistSource *source,
						 const char *location,
						 gint index)
{
	RhythmDB *db;
	RhythmDBEntry *entry;
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);
	if (rb_playlist_source_location_in_map (psource, location))
		return;

	db = rb_playlist_source_get_db (psource);
	entry = rhythmdb_entry_lookup_by_location (db, location);
	if (entry) {
		RBStaticPlaylistSourcePrivate *priv = RB_STATIC_PLAYLIST_SOURCE_GET_PRIVATE (source);
		RhythmDBEntryType entry_type;

		g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
		if (entry_type != -1 &&
		    rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TYPE) != entry_type) {
			rb_debug ("attempting to add an entry of the wrong type to playlist");
			return;
		}

		rhythmdb_entry_ref (db, entry);
		rhythmdb_query_model_add_entry (priv->base_model, entry, index);
		rhythmdb_entry_unref (db, entry);
	}

	rb_playlist_source_add_to_map (psource, location);

	rb_playlist_source_mark_dirty (psource);
}

static void
rb_static_playlist_source_add_location_swapped (const char *uri, 
						RBStaticPlaylistSource *source)
{
	rb_static_playlist_source_add_location_internal (source, uri, -1);
}


void
rb_static_playlist_source_add_location (RBStaticPlaylistSource *source,
					const char *location,
					gint index)
{
	RhythmDB *db;
	RhythmDBEntry *entry;

	db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));
	entry = rhythmdb_entry_lookup_by_location (db, location);

	/* if there is an entry, it won't be a directory */
	if (entry == NULL && rb_uri_is_directory (location))
		rb_uri_handle_recursively (location,
					   (GFunc) rb_static_playlist_source_add_location_swapped,
					   NULL,
					   source);
	else
		rb_static_playlist_source_add_location_internal (source, location, index);

}

void
rb_static_playlist_source_add_locations (RBStaticPlaylistSource *source,
					 GList *locations)
{
	GList *l;

	for (l = locations; l; l = l->next) {
		const gchar *uri = (const gchar *)l->data;
		rb_static_playlist_source_add_location (source, uri, -1);
	}
}

void
rb_static_playlist_source_remove_location (RBStaticPlaylistSource *source,
					   const char *location)
{
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);
	RhythmDB *db;
	RhythmDBEntry *entry;

	g_return_if_fail (rb_playlist_source_location_in_map (psource, location));

	db = rb_playlist_source_get_db (psource);
	entry = rhythmdb_entry_lookup_by_location (db, location);

	if (entry != NULL) {
		RhythmDBQueryModel *model = rb_playlist_source_get_query_model (psource);

		/* if this fails, the model and the playlist are out of sync */
		g_assert (rhythmdb_query_model_remove_entry (model, entry));
		rb_playlist_source_mark_dirty (psource);
	}
}

void
rb_static_playlist_source_add_entry (RBStaticPlaylistSource *source,
				     RhythmDBEntry *entry,
				     gint index)
{
	rb_static_playlist_source_add_location_internal (source, entry->location, index);
}

void
rb_static_playlist_source_remove_entry (RBStaticPlaylistSource *source,
					RhythmDBEntry *entry)
{
	rb_static_playlist_source_remove_location (source, entry->location);
}

void
rb_static_playlist_source_move_entry (RBStaticPlaylistSource *source,
				      RhythmDBEntry *entry,
				      gint index)
{
	RBPlaylistSource *psource = RB_PLAYLIST_SOURCE (source);
	RhythmDBQueryModel *model = rb_playlist_source_get_query_model (psource);
	rhythmdb_query_model_move_entry (model, entry, index);

	rb_playlist_source_mark_dirty (psource);
}


static void
rb_static_playlist_source_non_entry_dropped (GtkTreeModel *model,
					     const char *uri,
					     int position,
					     RBStaticPlaylistSource *source)
{
	g_assert (g_utf8_strlen (uri, -1) > 0);

	rhythmdb_add_uri (rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source)), uri);
	rb_static_playlist_source_add_location (source, uri, position);
}

static void
rb_static_playlist_source_row_inserted (GtkTreeModel *model,
					GtkTreePath *path,
					GtkTreeIter *iter,
					RBStaticPlaylistSource *source)
{
	RhythmDBEntry *entry;

	gtk_tree_model_get (model, iter, 0, &entry, -1);

	rb_static_playlist_source_add_entry (source, entry, -1);
}

