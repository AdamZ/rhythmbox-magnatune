/*
 *  arch-tag: Implementation of local file source object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>
#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs.h>
#include <glade/glade.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-window.h>
#include <string.h>

#include "rb-source.h"
#include "rb-library-source.h"

#include "rb-glade-helpers.h"
#include "rb-stock-icons.h"
#include "rb-node-view.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-volume.h"
#include "rb-bonobo-helpers.h"
#include "rb-debug.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"
#include "rb-song-info.h"
#include "rb-library-dnd-types.h"
#include "rb-node-filter.h"
#include "rb-search-entry.h"
#include "rb-preferences.h"

static void rb_library_source_class_init (RBLibrarySourceClass *klass);
static void rb_library_source_init (RBLibrarySource *source);
static void rb_library_source_finalize (GObject *object);
static void rb_library_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_library_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);
static void album_node_selected_cb (RBNodeView *source,
			            RBNode *node,
			            RBLibrarySource *libsource);
static void genre_node_selected_cb (RBNodeView *source,
			             RBNode *node,
			             RBLibrarySource *libsource);
static void artist_node_selected_cb (RBNodeView *source,
			             RBNode *node,
			             RBLibrarySource *libsource);
static void paned_size_allocate_cb (GtkWidget *widget,
				    GtkAllocation *allocation,
		                    RBLibrarySource *source);
static void rb_library_source_drop_cb (GtkWidget        *widget,
				       GdkDragContext   *context,
				       gint              x,
				       gint              y,
				       GtkSelectionData *data,
				       guint             info,
				       guint             time,
				       gpointer          user_data);

static void songs_view_changed_cb (RBNodeView *view, RBLibrarySource *source);

static void library_status_changed_cb (RBLibrary *library, RBLibrarySource *source);

static void rb_library_source_state_pref_changed (GConfClient *client,
						 guint cnxn_id,
						 GConfEntry *entry,
						 RBLibrarySource *source);
static void rb_library_source_ui_pref_changed (GConfClient *client,
					       guint cnxn_id,
					       GConfEntry *entry,
					       RBLibrarySource *source); 
static void rb_library_source_state_prefs_sync (RBLibrarySource *source);
static void rb_library_source_ui_prefs_sync (RBLibrarySource *source);
static void rb_library_source_preferences_sync (RBLibrarySource *source);
/* source methods */
static const char *impl_get_status (RBSource *source);
static const char *impl_get_browser_key (RBSource *source);
static GdkPixbuf *impl_get_pixbuf (RBSource *source);
static RBNodeView *impl_get_node_view (RBSource *source);
static GList *impl_get_extra_views (RBSource *source);
static void impl_delete (RBSource *source);
static void impl_search (RBSource *source, const char *text);
static GtkWidget *impl_get_config_widget (RBSource *source);
static void impl_song_properties (RBSource *source);
static const char * impl_get_artist (RBSource *player);
static const char * impl_get_album (RBSource *player);
static gboolean impl_receive_drag (RBSource *source, GtkSelectionData *data);
static gboolean impl_show_popup (RBSource *source);

static void artists_filter (RBLibrarySource *source,
	                    RBNode *genre);
static void albums_filter (RBLibrarySource *source,
			   RBNode *genre,
	                   RBNode *artist);
static void songs_filter (RBLibrarySource *source,
	                  RBNode *genre,
	                  RBNode *artist,
			  RBNode *album);

void rb_library_source_browser_views_activated_cb (GtkWidget *widget,
						 RBLibrarySource *source);


#define LIBRARY_SOURCE_SONGS_POPUP_PATH "/popups/LibrarySongsList"
#define LIBRARY_SOURCE_POPUP_PATH "/popups/LibrarySourceList"

#define CONF_UI_LIBRARY_DIR CONF_PREFIX "/ui/library"
#define CONF_UI_LIBRARY_BROWSER_VIEWS CONF_PREFIX "/ui/library/browser_views"
#define CONF_STATE_LIBRARY_DIR CONF_PREFIX "/state/library"
#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/library/paned_position"
#define CONF_STATE_SHOW_BROWSER   CONF_PREFIX "/state/library/show_browser"

struct RBLibrarySourcePrivate
{
	RBLibrary *library;

	GtkWidget *browser;
	GtkWidget *vbox;

	GdkPixbuf *pixbuf;

	RBNodeView *genres;
	RBNodeView *albums;
	RBNodeView *artists;
	RBNodeView *songs;

	GtkWidget *paned;

	gboolean lock;

	char *status;
	
	RBNodeFilter *artists_filter;
	RBNodeFilter *songs_filter;
	RBNodeFilter *albums_filter;

	gboolean changing_artist;
	gboolean changing_genre;

	gboolean loading_prefs;

	GtkWidget *config_widget;
	GSList *browser_views_group;

	gboolean filter_changed;
};

enum
{
	PROP_0,
	PROP_PLAYER,
	PROP_LIBRARY
};

static GObjectClass *parent_class = NULL;

/* dnd */
static const GtkTargetEntry target_uri [] =
		{ { RB_LIBRARY_DND_URI_LIST_TYPE, 0, RB_LIBRARY_DND_URI_LIST } };
static const GtkTargetEntry target_id [] =
		{ { RB_LIBRARY_DND_NODE_ID_TYPE,  0, RB_LIBRARY_DND_NODE_ID } };

GType
rb_library_source_get_type (void)
{
	static GType rb_library_source_type = 0;

	if (rb_library_source_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibrarySourceClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_source_class_init,
			NULL,
			NULL,
			sizeof (RBLibrarySource),
			0,
			(GInstanceInitFunc) rb_library_source_init
		};

		rb_library_source_type = g_type_register_static (RB_TYPE_SOURCE,
								 "RBLibrarySource",
								 &our_info, 0);

	}

	return rb_library_source_type;
}

static void
rb_library_source_class_init (RBLibrarySourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_source_finalize;

	object_class->set_property = rb_library_source_set_property;
	object_class->get_property = rb_library_source_get_property;

	source_class->impl_get_status = impl_get_status;
	source_class->impl_get_browser_key = impl_get_browser_key;
	source_class->impl_get_pixbuf  = impl_get_pixbuf;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_search = impl_search;
	source_class->impl_get_node_view = impl_get_node_view;
	source_class->impl_get_extra_views = impl_get_extra_views;
	source_class->impl_get_config_widget = impl_get_config_widget;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_delete = impl_delete;
	source_class->impl_have_artist_album = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_get_artist = impl_get_artist;
	source_class->impl_get_album = impl_get_album;
	source_class->impl_have_url = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_show_popup = impl_show_popup;

	g_object_class_install_property (object_class,
					 PROP_LIBRARY,
					 g_param_spec_object ("library",
							      "Library",
							      "Library",
							      RB_TYPE_LIBRARY,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
update_browser_views_visibility (RBLibrarySource *source)
{
	int views;
	GtkWidget *genres = GTK_WIDGET (source->priv->genres);
	GtkWidget *artists = GTK_WIDGET (source->priv->artists);
	GtkWidget *albums = GTK_WIDGET (source->priv->albums);

	views = eel_gconf_get_integer (CONF_UI_LIBRARY_BROWSER_VIEWS);
	
	switch (views)
	{
		case 0:
			gtk_widget_hide (genres);
			gtk_widget_show (artists);
			gtk_widget_show (albums);
			
			rb_node_view_select_node (source->priv->genres,
				  		  rb_library_get_all_artists 
						  (source->priv->library));
		break;
		case 1:
			gtk_widget_show (genres);
			gtk_widget_show (artists);
			gtk_widget_hide (albums);

			rb_node_view_select_node (source->priv->albums,
				  		  rb_library_get_all_songs 
						  (source->priv->library));	
		break;
		case 2:
			gtk_widget_show (genres);
			gtk_widget_show (artists);
			gtk_widget_show (albums);
		break;
	}
}

static void
rb_library_source_ui_prefs_sync (RBLibrarySource *source)
{
	update_browser_views_visibility (source);

	if (source->priv->config_widget)
		rb_library_source_preferences_sync (source);
}

static void
rb_library_source_ui_pref_changed (GConfClient *client,
				   guint cnxn_id,
				   GConfEntry *entry,
				   RBLibrarySource *source)
{
	rb_debug ("ui pref changed");
	rb_library_source_ui_prefs_sync (source);
}

static void
rb_library_source_init (RBLibrarySource *source)
{
	GtkWidget *dummy = gtk_tree_view_new ();
	source->priv = g_new0 (RBLibrarySourcePrivate, 1);

	/* Drag'n'Drop */

	source->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (source), source->priv->vbox);

	source->priv->pixbuf = gtk_widget_render_icon (dummy,
						       RB_STOCK_LIBRARY,
						       GTK_ICON_SIZE_MENU,
						       NULL);
	gtk_widget_destroy (dummy);
}

static void
rb_library_source_finalize (GObject *object)
{
	RBLibrarySource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY_SOURCE (object));

	source = RB_LIBRARY_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	rb_debug ("finalizing library source");

	g_object_unref (G_OBJECT (source->priv->artists_filter));
	g_object_unref (G_OBJECT (source->priv->songs_filter));
	g_object_unref (G_OBJECT (source->priv->albums_filter));

	g_free (source->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_library_source_songs_show_popup_cb (RBNodeView *view,
				       RBLibrarySource *library_source)
{
	GtkWidget *menu;
	GtkWidget *window;

	window = gtk_widget_get_ancestor (GTK_WIDGET (view),
					  BONOBO_TYPE_WINDOW);

	menu = gtk_menu_new ();

	bonobo_window_add_popup (BONOBO_WINDOW (window), GTK_MENU (menu),
			         LIBRARY_SOURCE_SONGS_POPUP_PATH);

	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
			3, gtk_get_current_event_time ());

	gtk_object_sink (GTK_OBJECT (menu));
}

static void
rb_library_source_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		{
			source->priv->library = g_value_get_object (value);

			g_signal_connect (G_OBJECT (source->priv->library),
					  "status-changed",
					  G_CALLBACK (library_status_changed_cb),
					  source);

			source->priv->paned = gtk_vpaned_new ();

			source->priv->browser = gtk_hbox_new (TRUE, 5);

			/* Initialize the filters */
			source->priv->artists_filter = rb_node_filter_new ();
			source->priv->songs_filter = rb_node_filter_new ();
			source->priv->albums_filter = rb_node_filter_new ();

			/* set up genres treeview */
			source->priv->genres = rb_node_view_new 
				(rb_library_get_all_genres (source->priv->library),
				 rb_file ("rb-node-view-genres.xml"),
				 NULL);
			g_signal_connect (G_OBJECT (source->priv->genres),
					  "node_selected",
					  G_CALLBACK (genre_node_selected_cb),
					  source);

			gtk_box_pack_start_defaults (GTK_BOX (source->priv->browser), GTK_WIDGET (source->priv->genres));


			/* set up artist treeview */
			source->priv->artists = rb_node_view_new (rb_library_get_all_artists (source->priv->library),
						                rb_file ("rb-node-view-artists.xml"),
								source->priv->artists_filter);
			g_signal_connect (G_OBJECT (source->priv->artists),
					  "node_selected",
					  G_CALLBACK (artist_node_selected_cb),
					  source);


			gtk_box_pack_start_defaults (GTK_BOX (source->priv->browser), GTK_WIDGET (source->priv->artists));

			/* set up albums treeview */
			source->priv->albums = rb_node_view_new (rb_library_get_all_albums (source->priv->library),
						               rb_file ("rb-node-view-albums.xml"),
							       source->priv->albums_filter);
			g_signal_connect (G_OBJECT (source->priv->albums),
					  "node_selected",
					  G_CALLBACK (album_node_selected_cb),
					  source);

			gtk_box_pack_start_defaults (GTK_BOX (source->priv->browser), GTK_WIDGET (source->priv->albums));
			gtk_paned_pack1 (GTK_PANED (source->priv->paned), source->priv->browser, FALSE, FALSE);

			/* set up songs tree view */
			source->priv->songs = rb_node_view_new (rb_library_get_all_songs (source->priv->library),
						              rb_file ("rb-node-view-songs.xml"),
							      source->priv->songs_filter);

			g_signal_connect (G_OBJECT (source->priv->songs), "show_popup",
					  G_CALLBACK (rb_library_source_songs_show_popup_cb), source);
			g_signal_connect (G_OBJECT (source->priv->songs),
					  "changed",
					  G_CALLBACK (songs_view_changed_cb),
					  source);

			/* Drag'n'Drop for songs view */
			g_signal_connect (G_OBJECT (source->priv->songs), "drag_data_received",
					  G_CALLBACK (rb_library_source_drop_cb), source);
			gtk_drag_dest_set (GTK_WIDGET (source->priv->songs), GTK_DEST_DEFAULT_ALL,
					   target_uri, 1, GDK_ACTION_COPY);
			rb_node_view_enable_drag_source (source->priv->songs, target_uri, 1);

			/* Drag'n'Drop for albums view */
			g_signal_connect (G_OBJECT (source->priv->albums), "drag_data_received",
					  G_CALLBACK (rb_library_source_drop_cb), source);
			gtk_drag_dest_set (GTK_WIDGET (source->priv->albums), GTK_DEST_DEFAULT_ALL,
					   target_uri, 1, GDK_ACTION_COPY);
			rb_node_view_enable_drag_source (source->priv->albums, target_id, 1);

			/* Drag'n'Drop for artists view */
			g_signal_connect (G_OBJECT (source->priv->artists), "drag_data_received",
					  G_CALLBACK (rb_library_source_drop_cb), source);
			gtk_drag_dest_set (GTK_WIDGET (source->priv->artists), GTK_DEST_DEFAULT_ALL,
					   target_uri, 1, GDK_ACTION_COPY);
			rb_node_view_enable_drag_source (source->priv->artists, target_id, 1);

			/* this gets emitted when the paned thingie is moved */
			g_signal_connect (G_OBJECT (source->priv->songs),
					  "size_allocate",
					  G_CALLBACK (paned_size_allocate_cb),
					  source);

			gtk_paned_pack2 (GTK_PANED (source->priv->paned), GTK_WIDGET (source->priv->songs), TRUE, FALSE);

			gtk_box_pack_start_defaults (GTK_BOX (source->priv->vbox), source->priv->paned);

			gtk_widget_show_all (GTK_WIDGET (source));

			rb_library_source_state_prefs_sync (source);
			rb_library_source_ui_prefs_sync (source);
			eel_gconf_notification_add (CONF_STATE_LIBRARY_DIR,
						    (GConfClientNotifyFunc) rb_library_source_state_pref_changed,
						    source);
			eel_gconf_notification_add (CONF_UI_LIBRARY_DIR,
						    (GConfClientNotifyFunc) rb_library_source_ui_pref_changed, source);

			rb_node_view_select_node (source->priv->artists,
					          rb_library_get_all_albums (source->priv->library));
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_library_source_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (object);

	switch (prop_id)
	{
	case PROP_LIBRARY:
		g_value_set_object (value, source->priv->library);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSource *
rb_library_source_new (RBLibrary *library)
{
	RBSource *source;

	source = RB_SOURCE (g_object_new (RB_TYPE_LIBRARY_SOURCE,
					  "name", _("Library"),
					  "library", library,
					  NULL));

	return source;
}

static RBNode *
ensure_node_selection (RBNodeView *source,
		       RBNode *all_node,
		       gboolean *changing_flag)
{
	GList *selection = rb_node_view_get_selection (source);

	if (selection == NULL) {
		*changing_flag = TRUE;
		rb_node_view_select_node (source, all_node);
		*changing_flag = FALSE;
		selection = rb_node_view_get_selection (source);
	}

	return selection->data;
}


static void
genre_node_selected_cb (RBNodeView *view,
			RBNode *node,
			RBLibrarySource *source)
{
	if (source->priv->changing_genre == TRUE)
		return;

	artists_filter (source, node);
	rb_node_view_select_node (source->priv->artists,
				  rb_library_get_all_albums (source->priv->library));

	rb_source_notify_filter_changed (RB_SOURCE (source));
}

static void
artist_node_selected_cb (RBNodeView *view,
			 RBNode *node,
			 RBLibrarySource *source)
{
	RBNode *genre;

	if (source->priv->changing_artist == TRUE)
		return;

	genre = ensure_node_selection (source->priv->genres,
			               rb_library_get_all_artists (source->priv->library),
				       &source->priv->changing_genre);

	albums_filter (source, genre, node);
	rb_node_view_select_node (source->priv->albums,
				  rb_library_get_all_songs (source->priv->library));

	rb_source_notify_filter_changed (RB_SOURCE (source));
}

static void
album_node_selected_cb (RBNodeView *view,
			RBNode *node,
			RBLibrarySource *source)
{
	RBNode *artist;
	RBNode *genre;

	genre = ensure_node_selection (source->priv->genres,
			               rb_library_get_all_artists (source->priv->library),
				       &source->priv->changing_genre);
	artist = ensure_node_selection (source->priv->artists,
			                rb_library_get_all_albums (source->priv->library),
				        &source->priv->changing_artist);

	rb_source_notify_filter_changed (RB_SOURCE (source));

	songs_filter (source, genre, artist, node);
}

static const char *
impl_get_status (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	g_free (source->priv->status);
	source->priv->status = rb_library_compute_status (source->priv->library,
							  rb_library_get_all_songs (source->priv->library),
							  source->priv->songs_filter);
	return source->priv->status;
}

static const char *
impl_get_browser_key (RBSource *source)
{
	return CONF_STATE_SHOW_BROWSER;
}

static GdkPixbuf *
impl_get_pixbuf (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);

	return source->priv->pixbuf;
}

static void
impl_search (RBSource *asource, const char *search_text)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);

		/* resets the filter */
	if (search_text == NULL || strcmp (search_text, "") == 0) {
		rb_node_view_select_node (source->priv->genres,
				          rb_library_get_all_artists (source->priv->library));
	} else {
		rb_node_view_select_none (source->priv->genres);
		rb_node_view_select_none (source->priv->artists);
		rb_node_view_select_none (source->priv->albums);

		artists_filter (source, rb_library_get_all_artists (source->priv->library));
		albums_filter (source, rb_library_get_all_artists (source->priv->library),
			             rb_library_get_all_albums (source->priv->library));

		rb_node_filter_empty (source->priv->songs_filter);
		rb_node_filter_add_expression (source->priv->songs_filter,
					       rb_node_filter_expression_new (source->priv->songs_filter,
									      RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_PROP_NAME,
									      search_text),
					       0);
		rb_node_filter_add_expression (source->priv->songs_filter,
					       rb_node_filter_expression_new (source->priv->songs_filter,
									      RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_PROP_ARTIST,
									      search_text),
					       0);
		rb_node_filter_add_expression (source->priv->songs_filter,
					       rb_node_filter_expression_new (source->priv->songs_filter,
									      RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_PROP_ALBUM,
									      search_text),
					       0);
		rb_node_filter_add_expression (source->priv->songs_filter,
					       rb_node_filter_expression_new (source->priv->songs_filter,
									      RB_NODE_FILTER_EXPRESSION_STRING_PROP_CONTAINS,
									      RB_NODE_PROP_GENRE,
									      search_text),
					       0);

		rb_node_filter_done_changing (source->priv->songs_filter);
	}
}


static RBNodeView *
impl_get_node_view (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);

	return source->priv->songs;
}

static GList *
impl_get_extra_views (RBSource *asource)
{
	GList *ret;
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);

	ret = g_list_append (NULL, source->priv->genres);
	ret = g_list_append (ret, source->priv->artists);
	ret = g_list_append (ret, source->priv->albums);
	return ret;
}
  
static GtkWidget *
impl_get_config_widget (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	GtkWidget *tmp;
	GtkWidget *browser_views_label;
	PangoAttrList *pattrlist;
	PangoAttribute *attr;
	GladeXML *xml;

	if (source->priv->config_widget)
		return source->priv->config_widget;

	xml = rb_glade_xml_new ("library-prefs.glade", "library_vbox", source);
	source->priv->config_widget =
		glade_xml_get_widget (xml, "library_vbox");
	tmp = glade_xml_get_widget (xml, "library_browser_views_radio");
	source->priv->browser_views_group =
		g_slist_reverse (g_slist_copy (gtk_radio_button_get_group 
					       (GTK_RADIO_BUTTON (tmp))));

	pattrlist = pango_attr_list_new ();
	attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
	attr->start_index = 0;
	attr->end_index = G_MAXINT;
	pango_attr_list_insert (pattrlist, attr);
	browser_views_label = glade_xml_get_widget (xml, "browser_views_label");
	gtk_label_set_attributes (GTK_LABEL (browser_views_label), pattrlist);
	pango_attr_list_unref (pattrlist);

	g_object_unref (G_OBJECT (xml));
	
	rb_library_source_preferences_sync (source);
	return source->priv->config_widget;
}

static const char *
impl_get_artist (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	RBNode *node;

	node = rb_node_view_get_playing_node (source->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_PROP_ARTIST);
	else
		return NULL;
}

static const char *
impl_get_album (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	RBNode *node;

	node = rb_node_view_get_playing_node (source->priv->songs);

	if (node != NULL)
		return rb_node_get_property_string (node, RB_NODE_PROP_ALBUM);
	else
		return NULL;
}


static void
rb_library_source_preferences_sync (RBLibrarySource *source)
{
	GSList *list;

	rb_debug ("syncing pref dialog state");

	list = g_slist_nth (source->priv->browser_views_group,
			    eel_gconf_get_integer (CONF_UI_LIBRARY_BROWSER_VIEWS));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (list->data), TRUE);
}

void
rb_library_source_browser_views_activated_cb (GtkWidget *widget,
					    RBLibrarySource *source)
{
	int index;

	if (source->priv->loading_prefs == TRUE)
		return;

	index = g_slist_index (source->priv->browser_views_group, widget);

	eel_gconf_set_integer (CONF_UI_LIBRARY_BROWSER_VIEWS, index);
}

static void
impl_delete (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	GList *l;

	for (l = rb_node_view_get_selection (source->priv->songs); l != NULL; l = g_list_next (l))
		rb_library_remove_node (source->priv->library, l->data);
}

static void
impl_song_properties (RBSource *asource)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	GtkWidget *song_info = NULL;

	g_return_if_fail (source->priv->songs != NULL);

	song_info = rb_song_info_new (source->priv->songs);
	if (song_info)
		gtk_widget_show_all (song_info);
	else
		rb_debug ("failed to create dialog, or no selection!");
}

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBLibrarySource *source)
{
	/* save state */
	rb_debug ("paned size allocate");
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION,
			       gtk_paned_get_position (GTK_PANED (source->priv->paned)));
}

static void
rb_library_source_state_prefs_sync (RBLibrarySource *source)
{
	rb_debug ("syncing state");
	gtk_paned_set_position (GTK_PANED (source->priv->paned),
				eel_gconf_get_integer (CONF_STATE_PANED_POSITION));
	
	if (eel_gconf_get_boolean (CONF_STATE_SHOW_BROWSER))
		gtk_widget_show (source->priv->browser);
	else
		gtk_widget_hide (source->priv->browser);
}

static void
rb_library_source_state_pref_changed (GConfClient *client,
				     guint cnxn_id,
				     GConfEntry *entry,
				     RBLibrarySource *source)
{
	rb_debug ("state prefs changed");
	rb_library_source_state_prefs_sync (source);
}

static void
library_status_changed_cb (RBLibrary *library, RBLibrarySource *source)
{
	rb_source_notify_status_changed (RB_SOURCE (source));
}

static void
songs_view_changed_cb (RBNodeView *view, RBLibrarySource *source)
{
	rb_debug ("got node view change");
	if (source->priv->filter_changed)
		rb_source_notify_status_changed (RB_SOURCE (source));
	source->priv->filter_changed = FALSE;
}

void
rb_library_source_add_location (RBLibrarySource *source, GtkWindow *win)
{
	GladeXML *xml = rb_glade_xml_new ("uri.glade",
					  "open_uri_dialog_content",
					  source);
	GtkWidget *content, *uri_widget;
	GtkWidget *dialog = gtk_dialog_new_with_buttons (_("Add Location"),
							 win,
							 GTK_DIALOG_MODAL,
							 GTK_STOCK_CANCEL,
							 GTK_RESPONSE_CANCEL,
							 GTK_STOCK_OPEN,
							 GTK_RESPONSE_OK,
							 NULL);

	g_return_if_fail (dialog != NULL);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog),
					 GTK_RESPONSE_OK);

	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	content = glade_xml_get_widget (xml, "open_uri_dialog_content");

	g_return_if_fail (content != NULL);

	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    content, FALSE, FALSE, 0);

	uri_widget = glade_xml_get_widget (xml, "uri");

	g_return_if_fail (uri_widget != NULL);

	switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
	case GTK_RESPONSE_OK:
	{
		char *uri = gtk_editable_get_chars (GTK_EDITABLE (uri_widget), 0, -1);
		if (uri != NULL) {
			GnomeVFSURI *vfsuri = gnome_vfs_uri_new (uri);
			if (vfsuri != NULL) {
				rb_library_add_uri (source->priv->library, uri);
				gnome_vfs_uri_unref (vfsuri);
			} else
				rb_debug ("invalid uri: \"%s\"", uri);

		}
		break;
	}
	default:
		rb_debug ("Location add cancelled");
	}
	gtk_widget_destroy (GTK_WIDGET (dialog));

}

gboolean
rb_library_source_eval_filter (RBLibrarySource *source, RBNode *node)
{
	if (rb_node_filter_evaluate (source->priv->songs_filter, node) == FALSE)
		return FALSE;

	return TRUE;
}

static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *data)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (asource);
	GList *list, *uri_list, *i;

	rb_debug ("parsing uri list");
	list = gnome_vfs_uri_list_parse (data->data);

	if (list == NULL)
		return FALSE;

	uri_list = NULL;

	for (i = list; i != NULL; i = g_list_next (i))
		uri_list = g_list_append (uri_list, gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));

	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL)
		return FALSE;
	
	rb_debug ("adding uris");

	for (i = uri_list; i != NULL; i = i->next) {
		char *uri = i->data;

		if (uri != NULL)
			rb_library_add_uri (source->priv->library, uri);

		g_free (uri);
	}

	g_list_free (uri_list);
	return TRUE;
}

static gboolean
impl_show_popup (RBSource *source)
{
	rb_bonobo_show_popup (GTK_WIDGET (source), LIBRARY_SOURCE_POPUP_PATH);
	return TRUE;
}

static void
rb_library_source_drop_cb (GtkWidget *widget,
			 GdkDragContext *context,
			 gint x,
			 gint y,
			 GtkSelectionData *data,
			 guint info,
			 guint time,
			 gpointer user_data)
{
	RBLibrarySource *source = RB_LIBRARY_SOURCE (user_data);
	GtkTargetList *tlist;
	gboolean ret;

	rb_debug ("checking target");

	tlist = gtk_target_list_new (target_uri, 1);
	ret = (gtk_drag_dest_find_target (widget, context, tlist) != GDK_NONE);
	gtk_target_list_unref (tlist);

	if (ret == FALSE)
		return;

	if (!impl_receive_drag (RB_SOURCE (source), data)) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	gtk_drag_finish (context, TRUE, FALSE, time);
}

static void
artists_filter (RBLibrarySource *source, RBNode *genre)
{
	rb_node_filter_empty (source->priv->artists_filter);
	rb_node_filter_add_expression (source->priv->artists_filter,
				       rb_node_filter_expression_new (source->priv->artists_filter,
								      RB_NODE_FILTER_EXPRESSION_EQUALS,
								      rb_library_get_all_albums (source->priv->library)),
				       0);
	rb_node_filter_add_expression (source->priv->artists_filter,
				       rb_node_filter_expression_new (source->priv->artists_filter,
								      RB_NODE_FILTER_EXPRESSION_HAS_PARENT,
								      genre),
				       0);
	rb_node_filter_done_changing (source->priv->artists_filter);
}

static void
albums_filter (RBLibrarySource *source, RBNode *genre, RBNode *artist)
{
	rb_node_filter_empty (source->priv->albums_filter);
	rb_node_filter_add_expression (source->priv->albums_filter,
				       rb_node_filter_expression_new (source->priv->albums_filter,
								      RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,
								      genre, rb_library_get_all_artists (source->priv->library)),
				       0);
	rb_node_filter_add_expression (source->priv->albums_filter,
				       rb_node_filter_expression_new (source->priv->albums_filter,
								      RB_NODE_FILTER_EXPRESSION_EQUALS,
								      rb_library_get_all_songs (source->priv->library)),
				       0);
	rb_node_filter_add_expression (source->priv->albums_filter,
				       rb_node_filter_expression_new (source->priv->albums_filter,
								      RB_NODE_FILTER_EXPRESSION_CHILD_PROP_EQUALS,
								      RB_NODE_PROP_REAL_GENRE, genre),
				       0);
	rb_node_filter_add_expression (source->priv->albums_filter,
				       rb_node_filter_expression_new (source->priv->albums_filter,
								      RB_NODE_FILTER_EXPRESSION_EQUALS,
								      rb_library_get_all_songs (source->priv->library)),
				       1);
	rb_node_filter_add_expression (source->priv->albums_filter,
				       rb_node_filter_expression_new (source->priv->albums_filter,
								      RB_NODE_FILTER_EXPRESSION_HAS_PARENT,
								      artist),
				       1);
	rb_node_filter_done_changing (source->priv->albums_filter);
}

static void
songs_filter (RBLibrarySource *source, RBNode *genre, RBNode *artist, RBNode *album)
{
	rb_node_filter_empty (source->priv->songs_filter);
	rb_node_filter_add_expression (source->priv->songs_filter,
				       rb_node_filter_expression_new (source->priv->songs_filter,
								      RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,
								      genre, rb_library_get_all_artists (source->priv->library)),
				       0);
	rb_node_filter_add_expression (source->priv->songs_filter,
				       rb_node_filter_expression_new (source->priv->songs_filter,
								      RB_NODE_FILTER_EXPRESSION_NODE_PROP_EQUALS,
								      RB_NODE_PROP_REAL_GENRE, genre),
				       0);
	rb_node_filter_add_expression (source->priv->songs_filter,
				       rb_node_filter_expression_new (source->priv->songs_filter,
								      RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,
								      artist, rb_library_get_all_albums (source->priv->library)),
				       1);
	rb_node_filter_add_expression (source->priv->songs_filter,
				       rb_node_filter_expression_new (source->priv->songs_filter,
								      RB_NODE_FILTER_EXPRESSION_NODE_PROP_EQUALS,
								      RB_NODE_PROP_REAL_ARTIST, artist),
				       1);
	rb_node_filter_add_expression (source->priv->songs_filter,
				       rb_node_filter_expression_new (source->priv->songs_filter,
								      RB_NODE_FILTER_EXPRESSION_NODE_EQUALS,
								      album, rb_library_get_all_songs (source->priv->library)),
				       2);
	rb_node_filter_add_expression (source->priv->songs_filter,
				       rb_node_filter_expression_new (source->priv->songs_filter,
								      RB_NODE_FILTER_EXPRESSION_NODE_PROP_EQUALS,
								      RB_NODE_PROP_REAL_ALBUM, album),
				       2);
	rb_node_filter_done_changing (source->priv->songs_filter);
	source->priv->filter_changed = TRUE;
}
