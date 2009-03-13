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

#include "config.h"

#include <string.h>
#include <libxml/tree.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-auto-playlist-source.h"
#include "rb-library-browser.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-stock-icons.h"
#include "rb-playlist-xml.h"
#include "rb-source-search-basic.h"

/**
 * SECTION:rb-auto-playlist-source
 * @short_description: automatic playlist source, based on a database query
 *
 * A playlist populated with the results of a database query.
 *
 * The query, limit, and sort settings are saved to the playlists file, so
 * they are persistent.
 *
 * Searching is implemented by appending the query criteria generated from
 * the search text to the query.  Browsing is implemented by using the base
 * query model (or a query model using the query generated from the search text,
 * there is some) as the input to a #RBLibraryBrowser.
 *
 * If the user has not set a sort order as part of the playlist definition,
 * the entry view columns are made clickable to allow the user to sort the
 * results.
 */

static GObject *rb_auto_playlist_source_constructor (GType type, guint n_construct_properties,
						      GObjectConstructParam *construct_properties);
static void rb_auto_playlist_source_dispose (GObject *object);
static void rb_auto_playlist_source_finalize (GObject *object);
static void rb_auto_playlist_source_set_property (GObject *object,
						  guint prop_id,
						  const GValue *value,
						  GParamSpec *pspec);
static void rb_auto_playlist_source_get_property (GObject *object,
						  guint prop_id,
						  GValue *value,
						  GParamSpec *pspec);

/* source methods */
static gboolean impl_show_popup (RBSource *source);
static gboolean impl_receive_drag (RBSource *asource, GtkSelectionData *data);
static void impl_search (RBSource *source, RBSourceSearch *search, const char *cur_text, const char *new_text);
static void impl_reset_filters (RBSource *asource);
static void impl_browser_toggled (RBSource *source, gboolean enabled);
static GList *impl_get_search_actions (RBSource *source);

/* playlist methods */
static void impl_save_contents_to_xml (RBPlaylistSource *source,
				       xmlNodePtr node);

static void rb_auto_playlist_source_songs_sort_order_changed_cb (RBEntryView *view,
								 RBAutoPlaylistSource *source);
static void rb_auto_playlist_source_do_query (RBAutoPlaylistSource *source,
					      gboolean subset);

/* browser stuff */
static GList *impl_get_property_views (RBSource *source);
void rb_auto_playlist_source_browser_views_activated_cb (GtkWidget *widget,
							 RBAutoPlaylistSource *source);
static void rb_auto_playlist_source_browser_changed_cb (RBLibraryBrowser *entry,
							GParamSpec *pspec,
							RBAutoPlaylistSource *source);

static GtkRadioActionEntry rb_auto_playlist_source_radio_actions [] =
{
	{ "AutoPlaylistSearchAll", NULL, N_("All"), NULL, N_("Search all fields"), RHYTHMDB_PROP_SEARCH_MATCH },
	{ "AutoPlaylistSearchArtists", NULL, N_("Artists"), NULL, N_("Search artists"), RHYTHMDB_PROP_ARTIST_FOLDED },
	{ "AutoPlaylistSearchAlbums", NULL, N_("Albums"), NULL, N_("Search albums"), RHYTHMDB_PROP_ALBUM_FOLDED },
	{ "AutoPlaylistSearchTitles", NULL, N_("Titles"), NULL, N_("Search titles"), RHYTHMDB_PROP_TITLE_FOLDED }
};

enum
{
	PROP_0,
	PROP_BASE_QUERY_MODEL
};

#define AUTO_PLAYLIST_SOURCE_POPUP_PATH "/AutoPlaylistSourcePopup"

typedef struct _RBAutoPlaylistSourcePrivate RBAutoPlaylistSourcePrivate;

struct _RBAutoPlaylistSourcePrivate
{
	RhythmDBQueryModel *cached_all_query;
	GPtrArray *query;
	gboolean query_resetting;
	RhythmDBQueryModelLimitType limit_type;
	GValueArray *limit_value;

	gboolean query_active;
	gboolean search_on_completion;

	GtkWidget *paned;
	RBLibraryBrowser *browser;
	gboolean browser_shown;

	RBSourceSearch *default_search;
	RhythmDBQuery *search_query;

	GtkActionGroup *action_group;
};

static gpointer playlist_pixbuf = NULL;

G_DEFINE_TYPE (RBAutoPlaylistSource, rb_auto_playlist_source, RB_TYPE_PLAYLIST_SOURCE)
#define GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_AUTO_PLAYLIST_SOURCE, RBAutoPlaylistSourcePrivate))

static void
rb_auto_playlist_source_class_init (RBAutoPlaylistSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBPlaylistSourceClass *playlist_class = RB_PLAYLIST_SOURCE_CLASS (klass);

	object_class->constructor = rb_auto_playlist_source_constructor;
	object_class->dispose = rb_auto_playlist_source_dispose;
	object_class->finalize = rb_auto_playlist_source_finalize;
	object_class->set_property = rb_auto_playlist_source_set_property;
	object_class->get_property = rb_auto_playlist_source_get_property;

	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_browser_toggled = impl_browser_toggled;
	source_class->impl_search = impl_search;
	source_class->impl_reset_filters = impl_reset_filters;
	source_class->impl_get_property_views = impl_get_property_views;
	source_class->impl_get_search_actions = impl_get_search_actions;

	playlist_class->impl_save_contents_to_xml = impl_save_contents_to_xml;

	g_type_class_add_private (klass, sizeof (RBAutoPlaylistSourcePrivate));
}

static void
rb_auto_playlist_source_init (RBAutoPlaylistSource *source)
{
	if (playlist_pixbuf == NULL) {
		gint size;

		gtk_icon_size_lookup (RB_SOURCE_ICON_SIZE, &size, NULL);
		playlist_pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
							    RB_STOCK_AUTO_PLAYLIST,
							    size,
							    0, NULL);
		if (playlist_pixbuf) {
			g_object_add_weak_pointer (playlist_pixbuf,
						   (gpointer *) &playlist_pixbuf);

			rb_source_set_pixbuf (RB_SOURCE (source), playlist_pixbuf);

			/* drop the initial reference to the icon */
			g_object_unref (playlist_pixbuf);
		}
	} else {
		rb_source_set_pixbuf (RB_SOURCE (source), playlist_pixbuf);
	}

}

static void
rb_auto_playlist_source_dispose (GObject *object)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (object);

	if (priv->action_group != NULL) {
		g_object_unref (priv->action_group);
		priv->action_group = NULL;
	}

	if (priv->cached_all_query != NULL) {
		g_object_unref (priv->cached_all_query);
		priv->cached_all_query = NULL;
	}

	if (priv->default_search != NULL) {
		g_object_unref (priv->default_search);
		priv->default_search = NULL;
	}

	G_OBJECT_CLASS (rb_auto_playlist_source_parent_class)->dispose (object);
}

static void
rb_auto_playlist_source_finalize (GObject *object)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (object);

	if (priv->query) {
		rhythmdb_query_free (priv->query);
	}
	
	if (priv->search_query) {
		rhythmdb_query_free (priv->search_query);
	}

	if (priv->limit_value) {
		g_value_array_free (priv->limit_value);
	}

	G_OBJECT_CLASS (rb_auto_playlist_source_parent_class)->finalize (object);
}

static GObject *
rb_auto_playlist_source_constructor (GType type, guint n_construct_properties,
				      GObjectConstructParam *construct_properties)
{
	RBEntryView *songs;
	RBAutoPlaylistSource *source;
	GObjectClass *parent_class = G_OBJECT_CLASS (rb_auto_playlist_source_parent_class);
	RBAutoPlaylistSourcePrivate *priv;
	RBShell *shell;
	RhythmDBEntryType entry_type;

	source = RB_AUTO_PLAYLIST_SOURCE (
			parent_class->constructor (type, n_construct_properties, construct_properties));
	priv = GET_PRIVATE (source);

	priv->paned = gtk_vpaned_new ();

	g_object_get (RB_PLAYLIST_SOURCE (source), "entry-type", &entry_type, NULL);
	priv->browser = rb_library_browser_new (rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source)),
						entry_type);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
	gtk_paned_pack1 (GTK_PANED (priv->paned), GTK_WIDGET (priv->browser), TRUE, FALSE);
	g_signal_connect_object (G_OBJECT (priv->browser), "notify::output-model",
				 G_CALLBACK (rb_auto_playlist_source_browser_changed_cb),
				 source, 0);

	songs = rb_source_get_entry_view (RB_SOURCE (source));
	g_signal_connect_object (G_OBJECT (songs), "sort-order-changed",
				 G_CALLBACK (rb_auto_playlist_source_songs_sort_order_changed_cb),
				 source, 0);

	g_object_get (source, "shell", &shell, NULL);
	priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
							       "AutoPlaylistActions",
							       NULL, 0,
							       shell);
	if (gtk_action_group_get_action (priv->action_group,
					 rb_auto_playlist_source_radio_actions[0].name) == NULL) {
		gtk_action_group_add_radio_actions (priv->action_group,
						    rb_auto_playlist_source_radio_actions,
						    G_N_ELEMENTS (rb_auto_playlist_source_radio_actions),
						    0,
						    NULL,
						    NULL);
		rb_source_search_basic_create_for_actions (priv->action_group,
							   rb_auto_playlist_source_radio_actions,
							   G_N_ELEMENTS (rb_auto_playlist_source_radio_actions));
	}
	priv->default_search = rb_source_search_basic_new (RHYTHMDB_PROP_SEARCH_MATCH);

	g_object_unref (shell);

	/* reparent the entry view */
	g_object_ref (songs);
	gtk_container_remove (GTK_CONTAINER (source), GTK_WIDGET (songs));
	gtk_paned_pack2 (GTK_PANED (priv->paned), GTK_WIDGET (songs), TRUE, FALSE);
	gtk_container_add (GTK_CONTAINER (source), priv->paned);
	g_object_unref (songs);

	gtk_widget_show_all (GTK_WIDGET (source));

	return G_OBJECT (source);
}

/**
 * rb_auto_playlist_source_new:
 * @shell: the #RBShell instance
 * @name: the name of the new playlist
 * @local: if TRUE, the playlist will be considered local
 *
 * Creates a new automatic playlist source, initially with an empty query.
 *
 * Return value: the new source
 */
RBSource *
rb_auto_playlist_source_new (RBShell *shell, const char *name, gboolean local)
{
	if (name == NULL)
		name = "";

	return RB_SOURCE (g_object_new (RB_TYPE_AUTO_PLAYLIST_SOURCE,
					"name", name,
					"shell", shell,
					"is-local", local,
					"entry-type", RHYTHMDB_ENTRY_TYPE_SONG,
					"source-group", RB_SOURCE_GROUP_PLAYLISTS,
					"search-type", RB_SOURCE_SEARCH_INCREMENTAL,
					NULL));
}

static void
rb_auto_playlist_source_set_property (GObject *object,
				      guint prop_id,
				      const GValue *value,
				      GParamSpec *pspec)
{
	/*RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);*/

	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_auto_playlist_source_get_property (GObject *object,
				      guint prop_id,
				      GValue *value,
				      GParamSpec *pspec)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_BASE_QUERY_MODEL:
		g_value_set_object (value, priv->cached_all_query);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_auto_playlist_source_new_from_xml:
 * @shell: the #RBShell instance
 * @node: libxml node containing the playlist
 *
 * Creates a new auto playlist source by parsing an XML-encoded query.
 *
 * Return value: the new source
 */
RBSource *
rb_auto_playlist_source_new_from_xml (RBShell *shell, xmlNodePtr node)
{
	RBAutoPlaylistSource *source = RB_AUTO_PLAYLIST_SOURCE (rb_auto_playlist_source_new (shell, NULL, TRUE));
	xmlNodePtr child;
	xmlChar *tmp;
	GPtrArray *query;
	RhythmDBQueryModelLimitType limit_type = RHYTHMDB_QUERY_MODEL_LIMIT_NONE;
	GValueArray *limit_value = NULL;
	gchar *sort_key = NULL;
	gint sort_direction = 0;
	GValue val = {0,};

	child = node->children;
	while (xmlNodeIsText (child))
		child = child->next;

	query = rhythmdb_query_deserialize (rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source)),
					    child);

	limit_value = g_value_array_new (0);
	tmp = xmlGetProp (node, RB_PLAYLIST_LIMIT_COUNT);
	if (!tmp) /* Backwards compatibility */
		tmp = xmlGetProp (node, RB_PLAYLIST_LIMIT);
	if (tmp) {
		gulong l = strtoul ((char *)tmp, NULL, 0);
		if (l > 0) {
			limit_type = RHYTHMDB_QUERY_MODEL_LIMIT_COUNT;

			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, l);
			g_value_array_append (limit_value, &val);
			g_free (tmp);
			g_value_unset (&val);
		}
	}

	if (limit_type == RHYTHMDB_QUERY_MODEL_LIMIT_NONE) {
		tmp = xmlGetProp (node, RB_PLAYLIST_LIMIT_SIZE);
		if (tmp) {
			guint64 l = g_ascii_strtoull ((char *)tmp, NULL, 0);
			if (l > 0) {
				limit_type = RHYTHMDB_QUERY_MODEL_LIMIT_SIZE;

				g_value_init (&val, G_TYPE_UINT64);
				g_value_set_uint64 (&val, l);
				g_value_array_append (limit_value, &val);
				g_free (tmp);
				g_value_unset (&val);
			}
		}
	}

	if (limit_type == RHYTHMDB_QUERY_MODEL_LIMIT_NONE) {
		tmp = xmlGetProp (node, RB_PLAYLIST_LIMIT_TIME);
		if (tmp) {
			gulong l = strtoul ((char *)tmp, NULL, 0);
			if (l > 0) {
				limit_type = RHYTHMDB_QUERY_MODEL_LIMIT_TIME;

				g_value_init (&val, G_TYPE_ULONG);
				g_value_set_ulong (&val, l);
				g_value_array_append (limit_value, &val);
				g_free (tmp);
				g_value_unset (&val);
			}
		}
	}

	sort_key = (gchar*) xmlGetProp (node, RB_PLAYLIST_SORT_KEY);
	if (sort_key && *sort_key) {
		tmp = xmlGetProp (node, RB_PLAYLIST_SORT_DIRECTION);
		if (tmp) {
			sort_direction = atoi ((char*) tmp);
			g_free (tmp);
		}
	} else {
		g_free (sort_key);
		sort_key = NULL;
		sort_direction = 0;
	}

	rb_auto_playlist_source_set_query (source, query,
					   limit_type,
					   limit_value,
					   sort_key,
					   sort_direction);
	g_free (sort_key);
	g_value_array_free (limit_value);
	rhythmdb_query_free (query);

	return RB_SOURCE (source);
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (source, AUTO_PLAYLIST_SOURCE_POPUP_PATH);
	return TRUE;
}

static void
impl_reset_filters (RBSource *source)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);
	gboolean changed = FALSE;

	if (rb_library_browser_reset (priv->browser))
		changed = TRUE;

	if (priv->search_query != NULL) {
		changed = TRUE;
		rhythmdb_query_free (priv->search_query);
		priv->search_query = NULL;
	}

	if (changed)
		rb_auto_playlist_source_do_query (RB_AUTO_PLAYLIST_SOURCE (source), FALSE);
}

static void
impl_search (RBSource *asource, RBSourceSearch *search, const char *cur_text, const char *new_text)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (asource);
	RhythmDB *db;
	gboolean subset;

	if (search == NULL) {
		search = priv->default_search;
	}
	
	/* replace our search query */
	if (priv->search_query != NULL) {
		rhythmdb_query_free (priv->search_query);
		priv->search_query = NULL;
	}
	db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (asource));
	priv->search_query = rb_source_search_create_query (search, db, new_text);

	/* we can only do subset searches once the original query is complete */
	subset = rb_source_search_is_subset (search, cur_text, new_text);
	if (priv->query_active && subset) {
		rb_debug ("deferring search for \"%s\" until query completion", new_text ? new_text : "<NULL>");
		priv->search_on_completion = TRUE;
	} else {
		rb_debug ("doing search for \"%s\"", new_text ? new_text : "<NULL>");
		rb_auto_playlist_source_do_query (RB_AUTO_PLAYLIST_SOURCE (asource), subset);
	}
}

static GList *
impl_get_property_views (RBSource *source)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);
	GList *ret;

	ret =  rb_library_browser_get_property_views (priv->browser);
	return ret;
}

static RhythmDBPropType
rb_auto_playlist_source_drag_atom_to_prop (GdkAtom smasher)
{
	if (smasher == gdk_atom_intern ("text/x-rhythmbox-album", TRUE))
		return RHYTHMDB_PROP_ALBUM;
	else if (smasher == gdk_atom_intern ("text/x-rhythmbox-artist", TRUE))
		return RHYTHMDB_PROP_ARTIST;
	else if (smasher == gdk_atom_intern ("text/x-rhythmbox-genre", TRUE))
		return RHYTHMDB_PROP_GENRE;
	else {
		g_assert_not_reached ();
		return 0;
	}
}

static void
impl_browser_toggled (RBSource *source, gboolean enabled)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);

	priv->browser_shown = enabled;

	if (enabled)
		gtk_widget_show (GTK_WIDGET (priv->browser));
	else
		gtk_widget_hide (GTK_WIDGET (priv->browser));
}

static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *data)
{
	RBAutoPlaylistSource *source = RB_AUTO_PLAYLIST_SOURCE (asource);

	GPtrArray *subquery = NULL;
	gchar **names;
	guint propid;
	int i;
	RhythmDB *db;

	/* ignore URI and entry ID lists */
	if (data->type == gdk_atom_intern ("text/uri-list", TRUE) ||
	    data->type == gdk_atom_intern ("application/x-rhythmbox-entry", TRUE))
		return TRUE;

	names = g_strsplit ((char *)data->data, "\r\n", 0);
	propid = rb_auto_playlist_source_drag_atom_to_prop (data->type);

	g_object_get (asource, "db", &db, NULL);

	for (i = 0; names[i]; i++) {
		if (subquery == NULL) {
			subquery = rhythmdb_query_parse (db,
							 RHYTHMDB_QUERY_PROP_EQUALS,
							 propid,
							 names[i],
							 RHYTHMDB_QUERY_END);
		} else {
			rhythmdb_query_append (db,
					       subquery,
					       RHYTHMDB_QUERY_DISJUNCTION,
					       RHYTHMDB_QUERY_PROP_EQUALS,
					       propid,
					       names[i],
					       RHYTHMDB_QUERY_END);
		}
	}

	g_strfreev (names);

	if (subquery != NULL) {
		RhythmDBEntryType qtype;
		GPtrArray *query;

		g_object_get (source, "entry-type", &qtype, NULL);
		if (qtype == RHYTHMDB_ENTRY_TYPE_INVALID)
			qtype = RHYTHMDB_ENTRY_TYPE_SONG;

		query = rhythmdb_query_parse (db,
					      RHYTHMDB_QUERY_PROP_EQUALS,
					      RHYTHMDB_PROP_TYPE,
					      qtype,
					      RHYTHMDB_QUERY_SUBQUERY,
					      subquery,
					      RHYTHMDB_QUERY_END);
		rb_auto_playlist_source_set_query (RB_AUTO_PLAYLIST_SOURCE (source), query,
						   RHYTHMDB_QUERY_MODEL_LIMIT_NONE, NULL,
						   NULL, 0);

		rhythmdb_query_free (subquery);
		rhythmdb_query_free (query);
                g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, qtype);
	}

	g_object_unref (db);

	return TRUE;
}

static void
_save_write_ulong (xmlNodePtr node, GValueArray *limit_value, const xmlChar *key)
{
	gulong l;
	gchar *str;

	l = g_value_get_ulong (g_value_array_get_nth (limit_value, 0));
	str = g_strdup_printf ("%u", (guint)l);
	xmlSetProp (node, key, BAD_CAST str);
	g_free (str);
}

static void
_save_write_uint64 (xmlNodePtr node, GValueArray *limit_value, const xmlChar *key)
{
	guint64 l;
	gchar *str;

	l = g_value_get_uint64 (g_value_array_get_nth (limit_value, 0));
	str = g_strdup_printf ("%" G_GUINT64_FORMAT, l);
	xmlSetProp (node, key, BAD_CAST str);
	g_free (str);
}

static void
impl_save_contents_to_xml (RBPlaylistSource *psource,
			   xmlNodePtr node)
{
	GPtrArray *query;
	RhythmDBQueryModelLimitType limit_type;
	GValueArray *limit_value = NULL;
	char *sort_key;
	gint sort_direction;
	RBAutoPlaylistSource *source = RB_AUTO_PLAYLIST_SOURCE (psource);

	xmlSetProp (node, RB_PLAYLIST_TYPE, RB_PLAYLIST_AUTOMATIC);

	sort_key = NULL;
	rb_auto_playlist_source_get_query (source,
					   &query,
					   &limit_type,
					   &limit_value,
					   &sort_key,
					   &sort_direction);

	switch (limit_type) {
	case RHYTHMDB_QUERY_MODEL_LIMIT_NONE:
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_COUNT:
		_save_write_ulong (node, limit_value, RB_PLAYLIST_LIMIT_COUNT);
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_SIZE:
		_save_write_uint64 (node, limit_value, RB_PLAYLIST_LIMIT_SIZE);
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_TIME:
		_save_write_ulong (node, limit_value, RB_PLAYLIST_LIMIT_TIME);
		break;

	default:
		g_assert_not_reached ();
	}

	if (sort_key && *sort_key) {
		char *temp_str;

		xmlSetProp (node, RB_PLAYLIST_SORT_KEY, BAD_CAST sort_key);
		temp_str = g_strdup_printf ("%d", sort_direction);
		xmlSetProp (node, RB_PLAYLIST_SORT_DIRECTION, BAD_CAST temp_str);

		g_free (temp_str);
	}

	rhythmdb_query_serialize (rb_playlist_source_get_db (psource), query, node);
	rhythmdb_query_free (query);

	if (limit_value != NULL) {
		g_value_array_free (limit_value);
	}
	g_free (sort_key);
}

static void
rb_auto_playlist_source_query_complete_cb (RhythmDBQueryModel *model,
					   RBAutoPlaylistSource *source)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);

	priv->query_active = FALSE;
	if (priv->search_on_completion) {
		priv->search_on_completion = FALSE;
		rb_debug ("performing deferred search");
		/* this is only done for subset searches */
		rb_auto_playlist_source_do_query (source, TRUE);
	}
}

static void
rb_auto_playlist_source_do_query (RBAutoPlaylistSource *source, gboolean subset)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);
	RhythmDB *db;
	RhythmDBQueryModel *query_model;
	GPtrArray *query;

	/* this doesn't add a ref */
	db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));

	g_assert (priv->cached_all_query);

	if (priv->search_query == NULL) {
		rb_library_browser_set_model (priv->browser,
					      priv->cached_all_query,
					      FALSE);
		return;
	}

	query = rhythmdb_query_copy (priv->query);
	rhythmdb_query_append (db, query,
			       RHYTHMDB_QUERY_SUBQUERY, priv->search_query,
			       RHYTHMDB_QUERY_END);

	g_object_get (priv->browser, "input-model", &query_model, NULL);

	if (subset && query_model != priv->cached_all_query) {
		/* just apply the new query to the existing query model */
		g_object_set (query_model, "query", query, NULL);
		rhythmdb_query_model_reapply_query (query_model, FALSE);
		g_object_unref (query_model);
	} else {
		/* otherwise, we need a new query model */
		g_object_unref (query_model);

		query_model = g_object_new (RHYTHMDB_TYPE_QUERY_MODEL,
					    "db", db,
					    "limit-type", priv->limit_type,
					    "limit-value", priv->limit_value,
					    NULL);
		rhythmdb_query_model_chain (query_model, priv->cached_all_query, FALSE);
		rb_library_browser_set_model (priv->browser, query_model, TRUE);

		priv->query_active = TRUE;
		priv->search_on_completion = FALSE;
		g_signal_connect_object (G_OBJECT (query_model),
					 "complete", G_CALLBACK (rb_auto_playlist_source_query_complete_cb),
					 source, 0);
		rhythmdb_do_full_query_async_parsed (db,
						     RHYTHMDB_QUERY_RESULTS (query_model),
						     query);
		g_object_unref (query_model);
	}

	rhythmdb_query_free (query);
}

/**
 * rb_auto_playlist_source_set_query:
 * @source: the #RBAutoPlaylistSource
 * @query: the new database query
 * @limit_type: the playlist limit type
 * @limit_value: the playlist limit value
 * @sort_key: the sorting key
 * @sort_direction: the sorting direction (as a #GtkSortType)
 *
 * Sets the database query used to populate the playlist, and also the limit on
 * playlist size, and the sorting type used.
 */
void
rb_auto_playlist_source_set_query (RBAutoPlaylistSource *source,
				   GPtrArray *query,
				   RhythmDBQueryModelLimitType limit_type,
				   GValueArray *limit_value,
				   const char *sort_key,
				   gint sort_direction)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);
	RhythmDB *db = rb_playlist_source_get_db (RB_PLAYLIST_SOURCE (source));
	RBEntryView *songs = rb_source_get_entry_view (RB_SOURCE (source));

	priv->query_resetting = TRUE;
	if (priv->query) {
		rhythmdb_query_free (priv->query);
	}

	if (priv->cached_all_query) {
		g_object_unref (G_OBJECT (priv->cached_all_query));
	}

	if (priv->limit_value) {
		g_value_array_free (priv->limit_value);
	}

	/* playlists that aren't limited, with a particular sort order, are user-orderable */
	rb_entry_view_set_columns_clickable (songs, (limit_type == RHYTHMDB_QUERY_MODEL_LIMIT_NONE));
	rb_entry_view_set_sorting_order (songs, sort_key, sort_direction);

	priv->query = rhythmdb_query_copy (query);
	priv->limit_type = limit_type;
	priv->limit_value = limit_value ? g_value_array_copy (limit_value) : NULL;

	priv->cached_all_query = g_object_new (RHYTHMDB_TYPE_QUERY_MODEL,
					       "db", db,
					       "limit-type", priv->limit_type,
					       "limit-value", priv->limit_value,
					       NULL);
	rb_library_browser_set_model (priv->browser, priv->cached_all_query, TRUE);
	rhythmdb_do_full_query_async_parsed (db,
					     RHYTHMDB_QUERY_RESULTS (priv->cached_all_query),
					     priv->query);

	priv->query_resetting = FALSE;
}

/**
 * rb_auto_playlist_source_get_query:
 * @source: the #RBAutoPlaylistSource
 * @query: returns the database query for the playlist
 * @limit_type: returns the playlist limit type
 * @limit_value: returns the playlist limit value
 * @sort_key: returns the playlist sorting key
 * @sort_direction: returns the playlist sorting direction (as a GtkSortOrder)
 *
 * Extracts the current query, playlist limit, and sorting settings for the playlist.
 */
void
rb_auto_playlist_source_get_query (RBAutoPlaylistSource *source,
				   GPtrArray **query,
				   RhythmDBQueryModelLimitType *limit_type,
				   GValueArray **limit_value,
				   char **sort_key,
				   gint *sort_direction)
{
	RBAutoPlaylistSourcePrivate *priv;
	RBEntryView *songs;

 	g_return_if_fail (RB_IS_AUTO_PLAYLIST_SOURCE (source));

	priv = GET_PRIVATE (source);
	songs = rb_source_get_entry_view (RB_SOURCE (source));

	*query = rhythmdb_query_copy (priv->query);
	*limit_type = priv->limit_type;
	*limit_value = (priv->limit_value) ? g_value_array_copy (priv->limit_value) : NULL;

	rb_entry_view_get_sorting_order (songs, sort_key, sort_direction);
}

static void
rb_auto_playlist_source_songs_sort_order_changed_cb (RBEntryView *view, RBAutoPlaylistSource *source)
{
	RBAutoPlaylistSourcePrivate *priv = GET_PRIVATE (source);

	/* don't process this if we are in the middle of setting a query */
	if (priv->query_resetting)
		return;
	rb_debug ("sort order changed");

	rb_entry_view_resort_model (view);
}

static void
rb_auto_playlist_source_browser_changed_cb (RBLibraryBrowser *browser,
					    GParamSpec *pspec,
					    RBAutoPlaylistSource *source)
{
	RBEntryView *songs = rb_source_get_entry_view (RB_SOURCE (source));
	RhythmDBQueryModel *query_model;

	g_object_get (browser, "output-model", &query_model, NULL);
	rb_entry_view_set_model (songs, query_model);
	rb_playlist_source_set_query_model (RB_PLAYLIST_SOURCE (source), query_model);
	g_object_unref (query_model);

	rb_source_notify_filter_changed (RB_SOURCE (source));
}

static GList *
impl_get_search_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("AutoPlaylistSearchTitles"));
	actions = g_list_prepend (actions, g_strdup ("AutoPlaylistSearchAlbums"));
	actions = g_list_prepend (actions, g_strdup ("AutoPlaylistSearchArtists"));
	actions = g_list_prepend (actions, g_strdup ("AutoPlaylistSearchAll"));

	return actions;
}

