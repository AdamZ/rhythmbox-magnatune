/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho <renato.filho@indt.org.br>
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

/**
 * SECTION:rb-podcast-source
 * @short_description: source displaying podcast feeds and episodes
 *
 * The podcast source displays podcast episodes in its entry view
 * and podcast feeds in a property view.  It uses a few custom columns
 * to display podcast-specific information: episode download status
 * and an indication of feed parsing errors.
 */

#include "config.h"

#include <string.h>
#define __USE_XOPEN
#include <time.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-podcast-source.h"

#include "rhythmdb.h"
#include "rhythmdb-query-model.h"
#include "rb-statusbar.h"
#include "rb-builder-helpers.h"
#include "rb-stock-icons.h"
#include "rb-entry-view.h"
#include "rb-property-view.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-preferences.h"
#include "rb-dialog.h"
#include "rb-uri-dialog.h"
#include "rb-podcast-properties-dialog.h"
#include "rb-feed-podcast-properties-dialog.h"
#include "rb-playlist-manager.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-podcast-manager.h"
#include "rb-static-playlist-source.h"
#include "rb-cut-and-paste-code.h"
#include "rb-source-search-basic.h"
#include "rb-cell-renderer-pixbuf.h"

static void rb_podcast_source_class_init 		(RBPodcastSourceClass *klass);

static void rb_podcast_source_init 			(RBPodcastSource *source);

static void rb_podcast_source_constructed 		(GObject *object);

static void rb_podcast_source_dispose 			(GObject *object);

static void rb_podcast_source_finalize 			(GObject *object);

static void rb_podcast_source_set_property 		(GObject *object,
			                  		 guint prop_id,
			                  		 const GValue *value,
			                  		 GParamSpec *pspec);

static void rb_podcast_source_get_property 		(GObject *object,
			                  		 guint prop_id,
			                  		 GValue *value,
			                  		 GParamSpec *pspec);

static void rb_podcast_source_songs_show_popup_cb 	(RBEntryView *view,
							 gboolean over_entry,
						  	 RBPodcastSource *source);

static void rb_podcast_source_feeds_show_popup_cb 	(RBPropertyView *view,
						  	 RBPodcastSource *source);

static void paned_size_allocate_cb 			(GtkWidget *widget,
				    			 GtkAllocation *allocation,
		                    			 RBPodcastSource *source);

static void rb_podcast_source_state_pref_changed 	(GConfClient *client,
						 	 guint cnxn_id,
						 	 GConfEntry *entry,
					 		 RBPodcastSource *source);

static void rb_podcast_source_show_browser 		(RBPodcastSource *source,
					   		 gboolean show);

static void rb_podcast_source_state_prefs_sync 		(RBPodcastSource *source);

static void feed_select_change_cb 			(RBPropertyView *propview,
							 GList *feeds,
			 			       	 RBPodcastSource *podcast_source);

static void rb_podcast_source_posts_view_sort_order_changed_cb (RBEntryView *view,
								RBPodcastSource *source);

static void rb_podcast_source_download_status_changed_cb(RBPodcastManager *download,
							 RhythmDBEntry *entry,
							 gulong status,
							 RBPodcastSource *source);

static void rb_podcast_source_btn_file_change_cb 	(GtkFileChooserButton *widget,
							 const char *key);

static void posts_view_drag_data_received_cb 		(GtkWidget *widget,
					      		 GdkDragContext *dc,
  				              		 gint x,
							 gint y,
				              		 GtkSelectionData *selection_data,
				              		 guint info,
							 guint time,
				              		 RBPodcastSource *source);

static void rb_podcast_source_cmd_download_post		(GtkAction *action,
							 RBPodcastSource *source);
static void rb_podcast_source_cmd_cancel_download	(GtkAction *action,
							 RBPodcastSource *source);
static void rb_podcast_source_cmd_delete_feed		(GtkAction *action,
							 RBPodcastSource *source);
static void rb_podcast_source_cmd_update_feed		(GtkAction *action,
							 RBPodcastSource *source);
static void rb_podcast_source_cmd_update_all		(GtkAction *action,
							 RBPodcastSource *source);
static void rb_podcast_source_cmd_properties_feed	(GtkAction *action,
							 RBPodcastSource *source);

static gint rb_podcast_source_post_date_cell_sort_func 	(RhythmDBEntry *a,
							 RhythmDBEntry *b,
		                                   	 RhythmDBQueryModel *model);

static gint rb_podcast_source_post_status_cell_sort_func(RhythmDBEntry *a,
							 RhythmDBEntry *b,
		                                   	 RhythmDBQueryModel *model);

static gint rb_podcast_source_post_feed_cell_sort_func 	(RhythmDBEntry *a,
							 RhythmDBEntry *b,
		                                   	 RhythmDBQueryModel *model);

static void rb_podcast_source_post_status_cell_data_func(GtkTreeViewColumn *column,
							 GtkCellRenderer *renderer,
						     	 GtkTreeModel *tree_model,
							 GtkTreeIter *iter,
						     	 RBPodcastSource *source);

static void rb_podcast_source_post_date_cell_data_func 	(GtkTreeViewColumn *column,
							 GtkCellRenderer *renderer,
		 			     	   	 GtkTreeModel *tree_model,
							 GtkTreeIter *iter,
				     	           	 RBPodcastSource *source);

static void rb_podcast_source_post_feed_cell_data_func  (GtkTreeViewColumn *column,
							 GtkCellRenderer *renderer,
						     	 GtkTreeModel *tree_model,
							 GtkTreeIter *iter,
						     	 RBPodcastSource *source);

static gboolean rb_podcast_source_feed_title_search_func (GtkTreeModel *model,
							  gint column,
							  const gchar *key,
							  GtkTreeIter *iter,
							  RBPodcastSource *source);
static void rb_podcast_source_feed_title_cell_data_func (GtkTreeViewColumn *column,
							 GtkCellRenderer *renderer,
						     	 GtkTreeModel *tree_model,
							 GtkTreeIter *iter,
						     	 RBPodcastSource *source);
static void rb_podcast_source_feed_error_cell_data_func (GtkTreeViewColumn *column,
							 GtkCellRenderer *renderer,
						     	 GtkTreeModel *tree_model,
							 GtkTreeIter *iter,
						     	 RBPodcastSource *source);

static void rb_podcast_source_start_download_cb 	(RBPodcastManager *pd,
							 RhythmDBEntry *entry,
							 RBPodcastSource *source);

static void rb_podcast_source_finish_download_cb 	(RBPodcastManager *pd,
							 RhythmDBEntry *entry,
							 RBPodcastSource *source);

static void rb_podcast_source_feed_updates_available_cb (RBPodcastManager *pd,
							 RhythmDBEntry *entry,
							 RBPodcastSource *source);

static gboolean rb_podcast_source_download_process_error_cb (RBPodcastManager *pd,
							 const char *error,
							 gboolean existing,
					  		 RBPodcastSource *source);

static void rb_podcast_source_cb_interval_changed_cb 	(GtkComboBox *box, gpointer cb_data);

static gboolean rb_podcast_source_load_finish_cb  	(gpointer cb_data);
static RBShell *rb_podcast_source_get_shell		(RBPodcastSource *source);
static void rb_podcast_source_entry_activated_cb (RBEntryView *view,
						  RhythmDBEntry *entry,
						  RBPodcastSource *source);
static void rb_podcast_source_cmd_new_podcast	 (GtkAction *action,
						  RBPodcastSource *source);
static void rb_podcast_source_entry_changed_cb	(RhythmDB *db,
						 RhythmDBEntry *entry,
						 GSList *changes,
						 RBPodcastSource *source);
static void rb_podcast_source_pixbuf_clicked_cb	(RBCellRendererPixbuf *renderer,
						 const char *path,
						 RBPodcastSource *source);

/* source methods */
static char *impl_get_browser_key	 		(RBSource *source);
static RBEntryView *impl_get_entry_view 		(RBSource *source);
static void impl_search 				(RBSource *source,
							 RBSourceSearch *search,
							 const char *current,
							 const char *next);
static void impl_delete 				(RBSource *source);
static void impl_song_properties 			(RBSource *source);
static RBSourceEOFType impl_handle_eos 			(RBSource *asource);
static gboolean impl_show_popup 			(RBSource *source);
static void rb_podcast_source_do_query			(RBPodcastSource *source);
static GtkWidget *impl_get_config_widget 		(RBSource *source,
							 RBShellPreferences *prefs);
static gboolean impl_receive_drag 			(RBSource *source,
							 GtkSelectionData *data);
static gboolean impl_can_add_to_queue			(RBSource *source);
static void impl_add_to_queue				(RBSource *source, RBSource *queue);
static GList *impl_get_ui_actions			(RBSource *source);
static GList *impl_get_search_actions			(RBSource *source);
static void impl_get_status				(RBSource *source,
							 char **text,
							 char **progress_text,
							 float *progress);
static guint impl_want_uri				(RBSource *source, const char *uri);
static gboolean impl_add_uri				(RBSource *source, const char *uri, const char *title, const char *genre);

#define CMD_PATH_SHOW_BROWSER "/commands/ShowBrowser"
#define CMD_PATH_CURRENT_STATION "/commands/CurrentStation"
#define CMD_PATH_SONG_INFO    "/commands/SongInfo"

#define CONF_UI_PODCAST_DIR 			CONF_PREFIX "/ui/podcast"
#define CONF_UI_PODCAST_COLUMNS_SETUP 		CONF_PREFIX "/ui/podcast/columns_setup"
#define CONF_STATE_PODCAST_PREFIX		CONF_PREFIX "/state/podcast"
#define CONF_STATE_PANED_POSITION 		CONF_STATE_PODCAST_PREFIX "/paned_position"
#define CONF_STATE_PODCAST_SORTING_POSTS	CONF_STATE_PODCAST_PREFIX "/sorting_posts"
#define CONF_STATE_PODCAST_SORTING_FEEDS	CONF_STATE_PODCAST_PREFIX "/sorting_feeds"
#define CONF_STATE_SHOW_BROWSER   		CONF_STATE_PODCAST_PREFIX "/show_browser"
#define CONF_STATE_PODCAST_DOWNLOAD_DIR		CONF_STATE_PODCAST_PREFIX "/download_prefix"
#define CONF_STATE_PODCAST_DOWNLOAD_INTERVAL	CONF_STATE_PODCAST_PREFIX "/download_interval"
#define CONF_STATE_PODCAST_DOWNLOAD_NEXT_TIME	CONF_STATE_PODCAST_PREFIX "/download_next_time"

struct RBPodcastSourcePrivate
{
	RhythmDB *db;

	guint prefs_notify_id;

	GtkWidget *vbox;
	GtkWidget *config_widget;
	GtkWidget *paned;

	RhythmDBPropertyModel *feed_model;
	RBPropertyView *feeds;
	RBEntryView *posts;
	GtkActionGroup *action_group;

	GList *selected_feeds;
	RhythmDBQuery *search_query;
	RhythmDBPropType search_prop;
	RBSourceSearch *default_search;

	RBPodcastManager *podcast_mgr;

	GdkPixbuf *error_pixbuf;
};

#define RB_PODCAST_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PODCAST_SOURCE, RBPodcastSourcePrivate))

static GtkActionEntry rb_podcast_source_actions [] =
{
	{ "MusicNewPodcast", RB_STOCK_PODCAST_NEW, N_("_New Podcast Feed..."), "<control>P",
	  N_("Subscribe to a new Podcast Feed"),
	  G_CALLBACK (rb_podcast_source_cmd_new_podcast) },

	{ "PodcastSrcDownloadPost", NULL, N_("Download _Episode"), NULL,
	  N_("Download Podcast Episode"),
	  G_CALLBACK (rb_podcast_source_cmd_download_post) },
	{ "PodcastSrcCancelDownload", GTK_STOCK_CANCEL, N_("_Cancel Download"), NULL,
	  N_("Cancel Episode Download"),
	  G_CALLBACK (rb_podcast_source_cmd_cancel_download) },
	{ "PodcastFeedProperties", GTK_STOCK_PROPERTIES, N_("_Properties"), NULL,
	  N_("Episode Properties"),
	  G_CALLBACK (rb_podcast_source_cmd_properties_feed) },
	{ "PodcastFeedUpdate", GTK_STOCK_REFRESH, N_("_Update Podcast Feed"), NULL,
	  N_("Update Feed"),
	  G_CALLBACK (rb_podcast_source_cmd_update_feed) },
	{ "PodcastFeedDelete", GTK_STOCK_DELETE, N_("_Delete Podcast Feed"), NULL,
	  N_("Delete Feed"),
	  G_CALLBACK (rb_podcast_source_cmd_delete_feed) },
	{ "PodcastUpdateAllFeeds", GTK_STOCK_REFRESH, N_("_Update All Feeds"), NULL,
	  N_("Update all feeds"),
	  G_CALLBACK (rb_podcast_source_cmd_update_all) },
};

static GtkRadioActionEntry rb_podcast_source_radio_actions [] =
{
	{ "PodcastSearchAll", NULL, N_("All"), NULL, N_("Search all fields"), RHYTHMDB_PROP_SEARCH_MATCH },
	{ "PodcastSearchFeeds", NULL, N_("Feeds"), NULL, N_("Search podcast feeds"), RHYTHMDB_PROP_ALBUM_FOLDED },
	{ "PodcastSearchEpisodes", NULL, N_("Episodes"), NULL, N_("Search podcast episodes"), RHYTHMDB_PROP_TITLE_FOLDED }
};

static const GtkTargetEntry posts_view_drag_types[] = {
	{  "text/uri-list", 0, 0 },
	{  "_NETSCAPE_URL", 0, 1 },
	{  "application/rss+xml", 0, 2 },
};

enum
{
	PROP_0,
	PROP_PODCAST_MANAGER
};

G_DEFINE_TYPE (RBPodcastSource, rb_podcast_source, RB_TYPE_SOURCE)

static void
rb_podcast_source_class_init (RBPodcastSourceClass *klass)
{

	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_podcast_source_dispose;
	object_class->finalize = rb_podcast_source_finalize;
	object_class->constructed = rb_podcast_source_constructed;

	object_class->set_property = rb_podcast_source_set_property;
	object_class->get_property = rb_podcast_source_get_property;

	source_class->impl_add_to_queue = impl_add_to_queue;
	source_class->impl_can_add_to_queue = impl_can_add_to_queue;
	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_delete = impl_delete;
	source_class->impl_get_browser_key  = impl_get_browser_key;
	source_class->impl_get_config_widget = impl_get_config_widget;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_get_search_actions = impl_get_search_actions;
	source_class->impl_get_ui_actions = impl_get_ui_actions;
	source_class->impl_handle_eos = impl_handle_eos;
	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_search = impl_search;
	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_get_status = impl_get_status;
	source_class->impl_want_uri = impl_want_uri;
	source_class->impl_add_uri = impl_add_uri;

	g_object_class_install_property (object_class,
					 PROP_PODCAST_MANAGER,
					 g_param_spec_object ("podcast-manager",
					                      "RBPodcastManager",
					                      "RBPodcastManager object",
					                      RB_TYPE_PODCAST_MANAGER,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBPodcastSourcePrivate));
}

static void
rb_podcast_source_init (RBPodcastSource *source)
{
	GtkIconTheme *icon_theme;
	GdkPixbuf *pixbuf;
	gint       size;

	source->priv = RB_PODCAST_SOURCE_GET_PRIVATE (source);

	source->priv->search_prop = RHYTHMDB_PROP_SEARCH_MATCH;
	source->priv->selected_feeds = NULL;
	source->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (source), source->priv->vbox);

	gtk_icon_size_lookup (RB_SOURCE_ICON_SIZE, &size, NULL);
	pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
					   RB_STOCK_PODCAST,
					   size,
					   0, NULL);

	if (pixbuf != NULL) {
		rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
		g_object_unref (pixbuf);
	}

	icon_theme = gtk_icon_theme_get_default ();
	source->priv->error_pixbuf = gtk_icon_theme_load_icon (icon_theme,
							       "dialog-error",
							       16,
							       0,
							       NULL);
}

static void
rb_podcast_source_dispose (GObject *object)
{
	RBPodcastSource *source;

	rb_debug ("dispose podcast_source");
	source = RB_PODCAST_SOURCE (object);

	if (source->priv->db != NULL) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}

	if (source->priv->search_query != NULL) {
		rhythmdb_query_free (source->priv->search_query);
		source->priv->search_query = NULL;
	}

	if (source->priv->action_group != NULL) {
		g_object_unref (source->priv->action_group);
		source->priv->action_group = NULL;
	}

	if (source->priv->podcast_mgr != NULL) {
		g_object_unref (source->priv->podcast_mgr);
		source->priv->podcast_mgr = NULL;
	}

	if (source->priv->error_pixbuf != NULL) {
		g_object_unref (source->priv->error_pixbuf);
		source->priv->error_pixbuf = NULL;
	}

	if (source->priv->prefs_notify_id != 0) {
		eel_gconf_notification_remove (source->priv->prefs_notify_id);
		source->priv->prefs_notify_id = 0;
	}

	G_OBJECT_CLASS (rb_podcast_source_parent_class)->dispose (object);
}

static void
rb_podcast_source_finalize (GObject *object)
{
	RBPodcastSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_PODCAST_SOURCE (object));

	source = RB_PODCAST_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	rb_debug ("finalizing podcast source");

	if (source->priv->selected_feeds) {
		g_list_foreach (source->priv->selected_feeds, (GFunc) g_free, NULL);
	        g_list_free (source->priv->selected_feeds);
	}

	G_OBJECT_CLASS (rb_podcast_source_parent_class)->finalize (object);
}

static void
rb_podcast_source_constructed (GObject *object)
{
	RBPodcastSource *source;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	RBShell *shell;
	RhythmDBQueryModel *query_model;
	GPtrArray *query;
	GtkAction *action;

	RB_CHAIN_GOBJECT_METHOD (rb_podcast_source_parent_class, constructed, object);
	source = RB_PODCAST_SOURCE (object);

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &source->priv->db, NULL);

	source->priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
								       "PodcastActions",
								       rb_podcast_source_actions,
								       G_N_ELEMENTS (rb_podcast_source_actions),
								       source);

	action = gtk_action_group_get_action (source->priv->action_group,
					      "MusicNewPodcast");
	/* Translators: this is the toolbar button label
	   for New Podcast Feed action. */
	g_object_set (G_OBJECT (action), "short-label", C_("Podcast", "New"), NULL);

	action = gtk_action_group_get_action (source->priv->action_group,
					      "PodcastUpdateAllFeeds");
	/* Translators: this is the toolbar button label
	   for Update All Feeds action. */
	g_object_set (G_OBJECT (action), "short-label", _("Update"), NULL);

	gtk_action_group_add_radio_actions (source->priv->action_group,
					    rb_podcast_source_radio_actions,
					    G_N_ELEMENTS (rb_podcast_source_radio_actions),
					    0,
					    NULL,
					    NULL);
	rb_source_search_basic_create_for_actions (source->priv->action_group,
						   rb_podcast_source_radio_actions,
						   G_N_ELEMENTS (rb_podcast_source_radio_actions));

	source->priv->default_search = rb_source_search_basic_new (RHYTHMDB_PROP_SEARCH_MATCH);

	source->priv->paned = gtk_vpaned_new ();

	g_idle_add ((GSourceFunc) rb_podcast_source_load_finish_cb, source);

	source->priv->podcast_mgr = rb_podcast_manager_new (source->priv->db);

	g_object_unref (shell);

	/* set up posts view */
	source->priv->posts = rb_entry_view_new (source->priv->db,
						 rb_shell_get_player (shell),
						 CONF_STATE_PODCAST_SORTING_POSTS,
						 TRUE, FALSE);

	g_signal_connect_object (source->priv->posts,
				 "entry-activated",
				 G_CALLBACK (rb_podcast_source_entry_activated_cb),
				 source, 0);

	/* Podcast date column */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new();

	gtk_tree_view_column_pack_start (column, renderer, TRUE);

	gtk_tree_view_column_set_clickable (column, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	{
		const char *sample_strings[3];
		sample_strings[0] = _("Date");
		sample_strings[1] = rb_entry_view_get_time_date_column_sample ();
		sample_strings[2] = NULL;
		rb_entry_view_set_fixed_column_width (source->priv->posts, column, renderer, sample_strings);
	}

	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 (GtkTreeCellDataFunc) rb_podcast_source_post_date_cell_data_func,
						 source, NULL);

	rb_entry_view_append_column_custom (source->priv->posts, column,
					    _("Date"), "Date",
					    (GCompareDataFunc) rb_podcast_source_post_date_cell_sort_func, 0, NULL);

	rb_entry_view_append_column (source->priv->posts, RB_ENTRY_VIEW_COL_TITLE, TRUE);

	/* COLUMN FEED */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new();

	gtk_tree_view_column_pack_start (column, renderer, TRUE);

	gtk_tree_view_column_set_clickable (column, TRUE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_expand (column, TRUE);

	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 (GtkTreeCellDataFunc) rb_podcast_source_post_feed_cell_data_func,
						 source, NULL);

	rb_entry_view_append_column_custom (source->priv->posts, column,
					    _("Feed"), "Feed",
					    (GCompareDataFunc) rb_podcast_source_post_feed_cell_sort_func, 0, NULL);

	rb_entry_view_append_column (source->priv->posts, RB_ENTRY_VIEW_COL_DURATION, FALSE);
	rb_entry_view_append_column (source->priv->posts, RB_ENTRY_VIEW_COL_RATING, FALSE);
	rb_entry_view_append_column (source->priv->posts, RB_ENTRY_VIEW_COL_PLAY_COUNT, FALSE);
	rb_entry_view_append_column (source->priv->posts, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);

	/* Status column */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_progress_new();

	gtk_tree_view_column_pack_start (column, renderer, TRUE);

	gtk_tree_view_column_set_clickable (column, TRUE);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);

	{
		static const char *status_strings[6];
		status_strings[0] = _("Status");
		status_strings[1] = _("Downloaded");
		status_strings[2] = _("Waiting");
		status_strings[3] = _("Failed");
		status_strings[4] = "100 %";
		status_strings[5] = NULL;

		rb_entry_view_set_fixed_column_width (source->priv->posts,
						      column,
						      renderer,
						      status_strings);
	}

	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 (GtkTreeCellDataFunc) rb_podcast_source_post_status_cell_data_func,
						 source, NULL);

	rb_entry_view_append_column_custom (source->priv->posts, column,
					    _("Status"), "Status",
					    (GCompareDataFunc) rb_podcast_source_post_status_cell_sort_func, 0, NULL);

	g_signal_connect_object (source->priv->posts,
				 "sort-order-changed",
				 G_CALLBACK (rb_podcast_source_posts_view_sort_order_changed_cb),
				 source, 0);

	g_signal_connect (source->priv->podcast_mgr,
			  "status_changed",
			  G_CALLBACK (rb_podcast_source_download_status_changed_cb),
			  source);

	g_signal_connect_object (source->priv->podcast_mgr,
			  	 "process_error",
			 	 G_CALLBACK (rb_podcast_source_download_process_error_cb),
			  	 source, 0);

	g_signal_connect_object (source->priv->posts,
				 "size_allocate",
				 G_CALLBACK (paned_size_allocate_cb),
				 source, 0);

	g_signal_connect_object (source->priv->posts,
				 "show_popup",
				 G_CALLBACK (rb_podcast_source_songs_show_popup_cb),
				 source, 0);

	/* configure feed view */
	source->priv->feeds = rb_property_view_new (source->priv->db,
						    RHYTHMDB_PROP_LOCATION,
						    _("Feed"));
	rb_property_view_set_selection_mode (RB_PROPERTY_VIEW (source->priv->feeds),
					     GTK_SELECTION_MULTIPLE);

	query_model = rhythmdb_query_model_new_empty (source->priv->db);
	source->priv->feed_model = rb_property_view_get_model (RB_PROPERTY_VIEW (source->priv->feeds));
	g_object_set (source->priv->feed_model, "query-model", query_model, NULL);

	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      RHYTHMDB_ENTRY_TYPE_PODCAST_FEED,
				      RHYTHMDB_QUERY_END);
	rhythmdb_do_full_query_parsed (source->priv->db,
				       RHYTHMDB_QUERY_RESULTS (query_model),
				       query);

	rhythmdb_query_free (query);
	g_object_unref (query_model);

	/* error indicator column */
	column = gtk_tree_view_column_new ();
	renderer = rb_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 (GtkTreeCellDataFunc) rb_podcast_source_feed_error_cell_data_func,
						 source, NULL);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width (column, gdk_pixbuf_get_width (source->priv->error_pixbuf) + 5);
	gtk_tree_view_column_set_reorderable (column, FALSE);
	gtk_tree_view_column_set_visible (column, TRUE);
	rb_property_view_append_column_custom (source->priv->feeds, column);
	g_signal_connect_object (renderer,
				 "pixbuf-clicked",
				 G_CALLBACK (rb_podcast_source_pixbuf_clicked_cb),
				 source, 0);

	/* redraw error indicator when errors are set or cleared */
	g_signal_connect_object (source->priv->db,
				 "entry_changed",
				 G_CALLBACK (rb_podcast_source_entry_changed_cb),
				 source, 0);

	/* title column */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();

	gtk_tree_view_column_pack_start (column, renderer, TRUE);

	gtk_tree_view_column_set_cell_data_func (column,
						 renderer,
						 (GtkTreeCellDataFunc) rb_podcast_source_feed_title_cell_data_func,
						 source, NULL);

	gtk_tree_view_column_set_title (column, _("Feed"));
	gtk_tree_view_column_set_reorderable (column, FALSE);
	gtk_tree_view_column_set_visible (column, TRUE);
	rb_property_view_append_column_custom (source->priv->feeds, column);

	g_signal_connect_object (source->priv->feeds, "show_popup",
				 G_CALLBACK (rb_podcast_source_feeds_show_popup_cb),
				 source, 0);

	g_signal_connect_object (source->priv->feeds,
				 "properties-selected",
				 G_CALLBACK (feed_select_change_cb),
				 source, 0);

	rb_property_view_set_search_func (source->priv->feeds,
					  (GtkTreeViewSearchEqualFunc) rb_podcast_source_feed_title_search_func,
					  source,
					  NULL);

	/* set up drag and drop */
	g_signal_connect_object (source->priv->feeds,
				 "drag_data_received",
				 G_CALLBACK (posts_view_drag_data_received_cb),
				 source, 0);

	gtk_drag_dest_set (GTK_WIDGET (source->priv->feeds),
			   GTK_DEST_DEFAULT_ALL,
			   posts_view_drag_types, 2,
			   GDK_ACTION_COPY | GDK_ACTION_MOVE);

	g_signal_connect_object (G_OBJECT (source->priv->posts),
				 "drag_data_received",
				 G_CALLBACK (posts_view_drag_data_received_cb),
				 source, 0);

	gtk_drag_dest_set (GTK_WIDGET (source->priv->posts),
			   GTK_DEST_DEFAULT_ALL,
			   posts_view_drag_types, 2,
			   GDK_ACTION_COPY | GDK_ACTION_MOVE);

	/* set up properties page */
	gtk_paned_pack1 (GTK_PANED (source->priv->paned),
			 GTK_WIDGET (source->priv->feeds), FALSE, FALSE);
	gtk_paned_pack2 (GTK_PANED (source->priv->paned),
			 GTK_WIDGET (source->priv->posts), TRUE, FALSE);

	gtk_box_pack_start (GTK_BOX (source->priv->vbox), source->priv->paned, TRUE, TRUE, 0);

	source->priv->prefs_notify_id = eel_gconf_notification_add (CONF_STATE_PODCAST_PREFIX,
								    (GConfClientNotifyFunc) rb_podcast_source_state_pref_changed,
								    source);

	gtk_widget_show_all (GTK_WIDGET (source));
	rb_podcast_source_state_prefs_sync (source);

	rb_podcast_source_do_query (source);
}

static void
rb_podcast_source_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (object);

	switch (prop_id) {
	case PROP_PODCAST_MANAGER:
		source->priv->podcast_mgr = g_value_get_object (value);
		if (source->priv->podcast_mgr)
			g_object_ref (source->priv->podcast_mgr);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_podcast_source_get_property (GObject *object,
				guint prop_id,
				GValue *value,
				GParamSpec *pspec)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (object);

	switch (prop_id) {
	case PROP_PODCAST_MANAGER:
		g_value_set_object (value, source->priv->podcast_mgr);
	        break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_podcast_source_new:
 * @shell: the #RBShell instance
 *
 * Creates the #RBPodcastSource instance
 *
 * Return value: the #RBPodcastSource
 */
RBSource *
rb_podcast_source_new (RBShell *shell)
{
	RBSource *source;
	source = RB_SOURCE (g_object_new (RB_TYPE_PODCAST_SOURCE,
					  "name", _("Podcasts"),
					  "shell", shell,
					  "entry-type", RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
					  "source-group", RB_SOURCE_GROUP_LIBRARY,
					  "search-type", RB_SOURCE_SEARCH_INCREMENTAL,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, source,
						 RHYTHMDB_ENTRY_TYPE_PODCAST_FEED);
	rb_shell_register_entry_type_for_source (shell, source,
						 RHYTHMDB_ENTRY_TYPE_PODCAST_POST);

	return source;
}

static void
impl_search (RBSource *asource, RBSourceSearch *search, const char *cur_text, const char *new_text)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);

	if (search == NULL) {
		search = source->priv->default_search;
	}

	if (source->priv->search_query != NULL) {
		rhythmdb_query_free (source->priv->search_query);
		source->priv->search_query = NULL;
	}
	source->priv->search_query = rb_source_search_create_query (search, source->priv->db, new_text);
	rb_podcast_source_do_query (source);

	rb_source_notify_filter_changed (RB_SOURCE (source));
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);

	return source->priv->posts;
}

static RBSourceEOFType
impl_handle_eos (RBSource *asource)
{
	return RB_SOURCE_EOF_STOP;
}

static char *
impl_get_browser_key (RBSource *asource)
{
	return g_strdup (CONF_STATE_SHOW_BROWSER);
}

static void
impl_delete (RBSource *asource)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);
	GList *entries;
	GList *l;
	gint ret;
	GtkWidget *dialog;
	GtkWidget *button;
	GtkWindow *window;
	RBShell *shell;

	rb_debug ("Delete episode action");

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "window", &window, NULL);
	g_object_unref (shell);

	dialog = gtk_message_dialog_new (window,
			                 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_NONE,
					 _("Delete the podcast episode and downloaded file?"));

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
	                                          _("If you choose to delete the episode and file, "
						    "they will be permanently lost.  Please note that "
						    "you can delete the episode but keep the downloaded "
						    "file by choosing to delete the episode only."));

	gtk_window_set_title (GTK_WINDOW (dialog), "");

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
	                        _("Delete _Episode Only"),
	                        GTK_RESPONSE_NO,
	                        GTK_STOCK_CANCEL,
	                        GTK_RESPONSE_CANCEL,
	                        NULL);
	button = gtk_dialog_add_button (GTK_DIALOG (dialog),
	                                _("_Delete Episode And File"),
			                GTK_RESPONSE_YES);

	gtk_window_set_focus (GTK_WINDOW (dialog), button);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);

	ret = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (ret == GTK_RESPONSE_CANCEL || ret == GTK_RESPONSE_DELETE_EVENT)
		return;

	rb_podcast_manager_set_remove_files (source->priv->podcast_mgr,
					     (ret == GTK_RESPONSE_YES));

	entries = rb_entry_view_get_selected_entries (source->priv->posts);
	for (l = entries; l != NULL; l = g_list_next (l)) {
		rhythmdb_entry_delete (source->priv->db, l->data);
	}

	g_list_foreach (entries, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (entries);

	rhythmdb_commit (source->priv->db);
}

static void
impl_song_properties (RBSource *asource)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);
	GtkWidget *dialog = rb_podcast_properties_dialog_new (source->priv->posts);
	rb_debug ("in song properties");
	if (dialog)
		gtk_widget_show_all (dialog);
	else
		rb_debug ("no selection!");
}

static void
paned_size_allocate_cb (GtkWidget *widget,
			GtkAllocation *allocation,
		        RBPodcastSource *source)
{
	rb_debug ("paned size allocate");
	eel_gconf_set_integer (CONF_STATE_PANED_POSITION,
			       gtk_paned_get_position (GTK_PANED (source->priv->paned)));
}

static void
rb_podcast_source_state_prefs_sync (RBPodcastSource *source)
{
	rb_debug ("syncing state");
	gtk_paned_set_position (GTK_PANED (source->priv->paned),
				eel_gconf_get_integer (CONF_STATE_PANED_POSITION));
	rb_podcast_source_show_browser (source,
					eel_gconf_get_boolean (CONF_STATE_SHOW_BROWSER));
}

static void
rb_podcast_source_state_pref_changed (GConfClient *client,
				      guint cnxn_id,
				      GConfEntry *entry,
				      RBPodcastSource *source)
{
	rb_debug ("state prefs changed");
	rb_podcast_source_state_prefs_sync (source);
}

static void
rb_podcast_source_posts_view_sort_order_changed_cb (RBEntryView *view,
						    RBPodcastSource *source)
{
	rb_debug ("sort order changed");
	rb_entry_view_resort_model (view);
}

static void
rb_podcast_source_download_status_changed_cb (RBPodcastManager *download,
					      RhythmDBEntry *entry,
					      gulong status,
					      RBPodcastSource *source)
{
	gtk_widget_queue_draw (GTK_WIDGET (source->priv->posts));
	return;

}

static void
rb_podcast_source_songs_show_popup_cb (RBEntryView *view,
				       gboolean over_entry,
				       RBPodcastSource *source)
{
	if (G_OBJECT (source) == NULL) {
		return;
	} else if (!over_entry) {
		_rb_source_show_popup (RB_SOURCE (source), "/PodcastSourcePopup");
	} else {
		GtkAction* action;
		GList *lst;
		gboolean downloadable = FALSE;
		gboolean cancellable = FALSE;

		lst = rb_entry_view_get_selected_entries (view);

		while (lst) {
			RhythmDBEntry *entry = (RhythmDBEntry*) lst->data;
			gulong status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);

			if ((status > 0 && status < RHYTHMDB_PODCAST_STATUS_COMPLETE) ||
			    status == RHYTHMDB_PODCAST_STATUS_WAITING)
				cancellable = TRUE;
			else if (status == RHYTHMDB_PODCAST_STATUS_PAUSED ||
				 status == RHYTHMDB_PODCAST_STATUS_ERROR)
				 downloadable = TRUE;

			lst = lst->next;
		}

		g_list_foreach (lst, (GFunc)rhythmdb_entry_unref, NULL);
		g_list_free (lst);

		action = gtk_action_group_get_action (source->priv->action_group, "PodcastSrcDownloadPost");
		gtk_action_set_sensitive (action, downloadable);

		action = gtk_action_group_get_action (source->priv->action_group, "PodcastSrcCancelDownload");
		gtk_action_set_sensitive (action, cancellable);

		_rb_source_show_popup (RB_SOURCE (source), "/PodcastViewPopup");
	}
}

static void
rb_podcast_source_feeds_show_popup_cb (RBPropertyView *view,
				       RBPodcastSource *source)
{
	if (G_OBJECT (source) == NULL) {
		return;
	} else {
		GtkAction *act_update;
		GtkAction *act_properties;
		GtkAction *act_delete;
		GList *lst;

		lst = source->priv->selected_feeds;

		act_update = gtk_action_group_get_action (source->priv->action_group, "PodcastFeedUpdate");
		act_properties = gtk_action_group_get_action (source->priv->action_group, "PodcastFeedProperties");
		act_delete = gtk_action_group_get_action (source->priv->action_group, "PodcastFeedDelete");

		if (lst) {
			gtk_action_set_visible (act_update, TRUE);
			gtk_action_set_visible (act_properties, TRUE);
			gtk_action_set_visible (act_delete, TRUE);
		} else {
			gtk_action_set_visible (act_update, FALSE);
			gtk_action_set_visible (act_properties, FALSE);
			gtk_action_set_visible (act_delete, FALSE);
		}

		_rb_source_show_popup (RB_SOURCE (source), "/PodcastFeedViewPopup");
	}
}

static void
feed_select_change_cb (RBPropertyView *propview,
		       GList *feeds,
		       RBPodcastSource *source)
{
	if (rb_string_list_equal (feeds, source->priv->selected_feeds))
		return;

	if (source->priv->selected_feeds) {
		g_list_foreach (source->priv->selected_feeds, (GFunc) g_free, NULL);
	        g_list_free (source->priv->selected_feeds);
	}

	source->priv->selected_feeds = rb_string_list_copy (feeds);

	rb_podcast_source_do_query (source);
	rb_source_notify_filter_changed (RB_SOURCE (source));
}

static void
rb_podcast_source_show_browser (RBPodcastSource *source,
				gboolean show)
{
	g_object_set (source->priv->feeds, "visible", show, NULL);
}

static GPtrArray *
construct_query_from_selection (RBPodcastSource *source)
{
	GPtrArray *query;
	RhythmDBEntryType entry_type;

	g_object_get (source, "entry-type", &entry_type, NULL);

	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      entry_type,
				      RHYTHMDB_QUERY_END);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	if (source->priv->search_query) {
		rhythmdb_query_append (source->priv->db,
				       query,
				       RHYTHMDB_QUERY_SUBQUERY,
				       source->priv->search_query,
				       RHYTHMDB_QUERY_END);
	}

	if (source->priv->selected_feeds) {
		GPtrArray *subquery = g_ptr_array_new ();
		GList *l;

		for (l = source->priv->selected_feeds; l != NULL; l = g_list_next (l)) {
			const char *location;

			location = (char *) l->data;
			rb_debug ("subquery SUBTITLE equals %s", location);

			rhythmdb_query_append (source->priv->db,
					       subquery,
					       RHYTHMDB_QUERY_PROP_EQUALS,
					       RHYTHMDB_PROP_SUBTITLE,
					       location,
					       RHYTHMDB_QUERY_END);
			if (g_list_next (l))
				rhythmdb_query_append (source->priv->db, subquery,
						       RHYTHMDB_QUERY_DISJUNCTION);
		}

		rhythmdb_query_append (source->priv->db, query,
				       RHYTHMDB_QUERY_SUBQUERY, subquery,
				       RHYTHMDB_QUERY_END);

		rhythmdb_query_free (subquery);
	}

	return query;
}

static void
rb_podcast_source_do_query (RBPodcastSource *source)
{
	RhythmDBQueryModel *query_model;
	GPtrArray *query;

	/* set up new query model */
	query_model = rhythmdb_query_model_new_empty (source->priv->db);

	rb_entry_view_set_model (source->priv->posts, query_model);
	g_object_set (source, "query-model", query_model, NULL);

	/* build and run the query */
	query = construct_query_from_selection (source);
	rhythmdb_do_full_query_async_parsed (source->priv->db,
					     RHYTHMDB_QUERY_RESULTS (query_model),
					     query);

	rhythmdb_query_free (query);

	g_object_unref (query_model);
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/PodcastSourcePopup");
	return TRUE;
}

static GtkWidget *
impl_get_config_widget (RBSource *asource, RBShellPreferences *prefs)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);
	GtkBuilder *builder;
	GtkWidget *cb_update_interval;
	GtkWidget *btn_file;
	char *download_dir;

	if (source->priv->config_widget)
		return source->priv->config_widget;

	builder = rb_builder_load ("podcast-prefs.ui", source);
	source->priv->config_widget = GTK_WIDGET (gtk_builder_get_object (builder, "podcast_vbox"));

	btn_file = GTK_WIDGET (gtk_builder_get_object (builder, "location_chooser"));
	gtk_file_chooser_add_shortcut_folder (GTK_FILE_CHOOSER (btn_file),
					      rb_music_dir (),
					      NULL);
	download_dir = rb_podcast_manager_get_podcast_dir (source->priv->podcast_mgr);
	gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (btn_file),
						 download_dir);
	g_free (download_dir);

	g_signal_connect (btn_file,
			  "selection-changed",
			  G_CALLBACK (rb_podcast_source_btn_file_change_cb),
			  CONF_STATE_PODCAST_DOWNLOAD_DIR);

	cb_update_interval = GTK_WIDGET (gtk_builder_get_object (builder, "cb_update_interval"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (cb_update_interval),
				  eel_gconf_get_integer (CONF_STATE_PODCAST_DOWNLOAD_INTERVAL));
	g_signal_connect (cb_update_interval,
			  "changed",
			  G_CALLBACK (rb_podcast_source_cb_interval_changed_cb),
			  source);

	return source->priv->config_widget;
}

static void
rb_podcast_source_btn_file_change_cb (GtkFileChooserButton *widget, const char *key)
{
	char *uri;
	
	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (widget));
	eel_gconf_set_string (key, uri);
	g_free (uri);
}

static void
posts_view_drag_data_received_cb (GtkWidget *widget,
				  GdkDragContext *dc,
				  gint x,
				  gint y,
				  GtkSelectionData *selection_data,
				  guint info,
				  guint time,
				  RBPodcastSource *source)
{
	impl_receive_drag (RB_SOURCE (source), selection_data);
}

/**
 * rb_podcast_source_add_feed:
 * @source: the #RBPodcastSource
 * @uri: the new feed to add
 *
 * Adds a new podcast feed.
 * Simply calls #rb_podcast_manager_subscribe_feed.
 */
void
rb_podcast_source_add_feed (RBPodcastSource *source, const char *uri)
{
	rb_podcast_manager_subscribe_feed (source->priv->podcast_mgr, uri, FALSE);
}

static void
rb_podcast_source_cmd_download_post (GtkAction *action,
				     RBPodcastSource *source)
{
	GList *lst;
	GValue val = {0, };
	RBEntryView *posts;

	rb_debug ("Add to download action");
	posts = source->priv->posts;

	lst = rb_entry_view_get_selected_entries (posts);
	g_value_init (&val, G_TYPE_ULONG);

	while (lst != NULL) {
		RhythmDBEntry *entry  = (RhythmDBEntry *) lst->data;
		gulong status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);

		if (status == RHYTHMDB_PODCAST_STATUS_PAUSED ||
		    status == RHYTHMDB_PODCAST_STATUS_ERROR) {
			g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_WAITING);
			rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_STATUS, &val);
			rb_podcast_manager_download_entry (source->priv->podcast_mgr, entry);
		}

		lst = lst->next;
	}
	g_value_unset (&val);
	rhythmdb_commit (source->priv->db);

	g_list_foreach (lst, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (lst);
}

static void
rb_podcast_source_cmd_cancel_download (GtkAction *action,
				       RBPodcastSource *source)
{
	GList *lst;
	GValue val = {0, };
	RBEntryView *posts;

	posts = source->priv->posts;

	lst = rb_entry_view_get_selected_entries (posts);
	g_value_init (&val, G_TYPE_ULONG);

	while (lst != NULL) {
		RhythmDBEntry *entry  = (RhythmDBEntry *) lst->data;
		gulong status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);

		if ((status > 0 && status < RHYTHMDB_PODCAST_STATUS_COMPLETE) ||
		    status == RHYTHMDB_PODCAST_STATUS_WAITING) {
			g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_PAUSED);
			rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_STATUS, &val);
			rb_podcast_manager_cancel_download (source->priv->podcast_mgr, entry);
		}

		lst = lst->next;
	}

	g_value_unset (&val);
	rhythmdb_commit (source->priv->db);

	g_list_foreach (lst, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (lst);
}

static void
rb_podcast_source_cmd_delete_feed (GtkAction *action,
			     	   RBPodcastSource *source)
{
	GList *feeds, *l;
	gint ret;
	GtkWidget *dialog;
	GtkWidget *button;
	GtkWindow *window;
	RBShell *shell;

	rb_debug ("Delete feed action");

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "window", &window, NULL);
	g_object_unref (shell);

	dialog = gtk_message_dialog_new (window,
			                 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_NONE,
					 _("Delete the podcast feed and downloaded files?"));

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
	                                          _("If you choose to delete the feed and files, "
						    "they will be permanently lost.  Please note that "
						    "you can delete the feed but keep the downloaded "
						    "files by choosing to delete the feed only."));

	gtk_window_set_title (GTK_WINDOW (dialog), "");

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
	                        _("Delete _Feed Only"),
	                        GTK_RESPONSE_NO,
	                        GTK_STOCK_CANCEL,
	                        GTK_RESPONSE_CANCEL,
	                        NULL);

	button = gtk_dialog_add_button (GTK_DIALOG (dialog),
	                                _("_Delete Feed And Files"),
			                GTK_RESPONSE_YES);

	gtk_window_set_focus (GTK_WINDOW (dialog), button);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);

	ret = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (ret == GTK_RESPONSE_CANCEL || ret == GTK_RESPONSE_DELETE_EVENT)
		return;

	feeds = rb_string_list_copy (source->priv->selected_feeds);
	for (l = feeds; l != NULL; l = g_list_next (l)) {
		const char *location = l->data;

		rb_debug ("Removing podcast location: %s", location);
		rb_podcast_manager_remove_feed (source->priv->podcast_mgr,
						location,
						(ret == GTK_RESPONSE_YES));
	}

	rb_list_deep_free (feeds);
}

static void
rb_podcast_source_cmd_properties_feed (GtkAction *action,
			     	       RBPodcastSource *source)
{
	RhythmDBEntry *entry;
	GtkWidget *dialog;
	const char *location;

	location = (char *) source->priv->selected_feeds->data;

	entry = rhythmdb_entry_lookup_by_location (source->priv->db,
						   location);

	if (entry != NULL) {
		dialog = rb_feed_podcast_properties_dialog_new (entry);
		rb_debug ("in feed properties");
		if (dialog)
			gtk_widget_show_all (dialog);
		else
			rb_debug ("no selection!");
	}
}

static void
rb_podcast_source_cmd_update_feed (GtkAction *action,
			     	   RBPodcastSource *source)
{
	GList *feeds, *l;

	rb_debug ("Update action");

	feeds = rb_string_list_copy (source->priv->selected_feeds);
	for (l = feeds; l != NULL; l = g_list_next (l)) {
		const char *location = l->data;

		rb_podcast_manager_subscribe_feed (source->priv->podcast_mgr,
						   location,
						   FALSE);
	}

	rb_list_deep_free (feeds);
}

static void
rb_podcast_source_cmd_update_all (GtkAction *action, RBPodcastSource *source)
{
	rb_podcast_manager_update_feeds (source->priv->podcast_mgr);
}

static void
rb_podcast_source_post_status_cell_data_func (GtkTreeViewColumn *column,
					      GtkCellRenderer *renderer,
				     	      GtkTreeModel *tree_model,
					      GtkTreeIter *iter,
				     	      RBPodcastSource *source)

{
	RhythmDBEntry *entry;
	guint value;

	gtk_tree_model_get (tree_model, iter, 0, &entry, -1);

	switch (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS)) {
	case RHYTHMDB_PODCAST_STATUS_COMPLETE:
		g_object_set (renderer, "text", _("Downloaded"), NULL);
		value = 100;
		break;
	case RHYTHMDB_PODCAST_STATUS_ERROR:
		g_object_set (renderer, "text", _("Failed"), NULL);
		value = 0;
		break;
	case RHYTHMDB_PODCAST_STATUS_WAITING:
		g_object_set (renderer, "text", _("Waiting"), NULL);
		value = 0;
		break;
	case RHYTHMDB_PODCAST_STATUS_PAUSED:
		g_object_set (renderer, "text", "", NULL);
		value = 0;
		break;
	default:
		{
			char *s;

			value = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);
			s = g_strdup_printf ("%u %%", value);

			g_object_set (renderer, "text", s, NULL);
			g_free (s);
		}
	}

	g_object_set (renderer, "visible",
		      rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS) != RHYTHMDB_PODCAST_STATUS_PAUSED,
		      NULL);

	g_object_set (renderer, "value", value, NULL);

	rhythmdb_entry_unref (entry);
}

static void
rb_podcast_source_post_feed_cell_data_func (GtkTreeViewColumn *column,
					    GtkCellRenderer *renderer,
				       	    GtkTreeModel *tree_model,
					    GtkTreeIter *iter,
				       	    RBPodcastSource *source)

{
	RhythmDBEntry *entry;
	const gchar *album;

	gtk_tree_model_get (tree_model, iter, 0, &entry, -1);
	album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);

	g_object_set (renderer, "text", album, NULL);

	rhythmdb_entry_unref (entry);
}

static gboolean
rb_podcast_source_feed_title_search_func (GtkTreeModel *model,
					  gint column,
					  const gchar *key,
					  GtkTreeIter *iter,
					  RBPodcastSource *source)
{
	char *title;
	char *fold_key;
	RhythmDBEntry *entry = NULL;
	gboolean ret;

	fold_key = rb_search_fold (key);
	gtk_tree_model_get (model, iter,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &title,
			    -1);

	entry = rhythmdb_entry_lookup_by_location (source->priv->db, title);
	if (entry != NULL) {
		g_free (title);
		title = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE_FOLDED);
	}

	ret = g_str_has_prefix (title, fold_key);

	g_free (fold_key);
	g_free (title);

	return !ret;
}

static void
rb_podcast_source_feed_title_cell_data_func (GtkTreeViewColumn *column,
					     GtkCellRenderer *renderer,
					     GtkTreeModel *tree_model,
					     GtkTreeIter *iter,
					     RBPodcastSource *source)
{
	char *title;
	char *str;
	gboolean is_all;
	guint number;
	RhythmDBEntry *entry = NULL;

	str = NULL;
	gtk_tree_model_get (tree_model, iter,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &title,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY, &is_all,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_NUMBER, &number, -1);

	entry = rhythmdb_entry_lookup_by_location (source->priv->db, title);
	if (entry != NULL) {
		g_free (title);
		title = g_strdup (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE));
	}

	if (is_all) {
		int nodes;
		const char *fmt;

		nodes = gtk_tree_model_iter_n_children  (GTK_TREE_MODEL (tree_model), NULL);
		/* Subtract one for the All node */
		nodes--;

		fmt = ngettext ("%d feed", "All %d feeds", nodes);

		str = g_strdup_printf (fmt, nodes, number);
	} else {
		str = g_strdup_printf ("%s", title);
	}

	g_object_set (G_OBJECT (renderer), "text", str,
		      "weight", G_UNLIKELY (is_all) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
		      NULL);

	g_free (str);
	g_free (title);
}

static void
rb_podcast_source_feed_error_cell_data_func (GtkTreeViewColumn *column,
					     GtkCellRenderer *renderer,
					     GtkTreeModel *tree_model,
					     GtkTreeIter *iter,
					     RBPodcastSource *source)
{
	char *title;
	RhythmDBEntry *entry = NULL;
	GdkPixbuf *pixbuf = NULL;

	gtk_tree_model_get (tree_model, iter,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &title,
			    -1);

	entry = rhythmdb_entry_lookup_by_location (source->priv->db, title);
	g_free (title);

	if (entry != NULL) {
		if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_PLAYBACK_ERROR)) {
			pixbuf = source->priv->error_pixbuf;
		}
	}
	g_object_set (renderer, "pixbuf", pixbuf, NULL);
}

static void
rb_podcast_source_post_date_cell_data_func (GtkTreeViewColumn *column,
					    GtkCellRenderer *renderer,
				     	    GtkTreeModel *tree_model,
					    GtkTreeIter *iter,
				     	    RBPodcastSource *source)
{
	RhythmDBEntry *entry;
	gulong value;
	char *str;

	gtk_tree_model_get (tree_model, iter, 0, &entry, -1);

	value = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_POST_TIME);
        if (value == 0) {
		str = g_strdup (_("Unknown"));
	} else {
		str = rb_utf_friendly_time (value);
	}

	g_object_set (G_OBJECT (renderer), "text", str, NULL);
	g_free (str);

	rhythmdb_entry_unref (entry);
}

static void
rb_podcast_source_cb_interval_changed_cb (GtkComboBox *box, gpointer cb_data)
{
	guint index = gtk_combo_box_get_active (box);
	eel_gconf_set_integer (CONF_STATE_PODCAST_DOWNLOAD_INTERVAL,
			       index);

	rb_podcast_manager_start_sync (RB_PODCAST_SOURCE (cb_data)->priv->podcast_mgr);
}

static gboolean
rb_podcast_source_load_finish_cb  (gpointer cb_data)
{
	RBPodcastSource *source  = RB_PODCAST_SOURCE (cb_data);

	rb_podcast_manager_start_sync (source->priv->podcast_mgr);

	g_signal_connect_object (G_OBJECT (source->priv->podcast_mgr),
	 		        "start_download",
			  	G_CALLBACK (rb_podcast_source_start_download_cb),
			  	source, 0);

	g_signal_connect_object (G_OBJECT (source->priv->podcast_mgr),
			  	"finish_download",
			  	G_CALLBACK (rb_podcast_source_finish_download_cb),
			  	source, 0);

	g_signal_connect_object (G_OBJECT (source->priv->podcast_mgr),
			  	"feed_updates_available",
 			  	G_CALLBACK (rb_podcast_source_feed_updates_available_cb),
			  	source, 0);

	return FALSE;
}

static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *selection_data)
{
	GList *list, *i;
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);

	list = rb_uri_list_parse ((char *)selection_data->data);

	for (i = list; i != NULL; i = i->next) {
		char *uri = NULL;

		uri = i->data;
		if ((uri != NULL) &&
		    (!rhythmdb_entry_lookup_by_location (source->priv->db, uri))) {
			rb_podcast_source_add_feed (source, uri);
		}
		
		if (selection_data->type == gdk_atom_intern ("_NETSCAPE_URL", FALSE)) {
			i = i->next;
		}
	}

	rb_list_deep_free (list);
	return TRUE;
}

static void
rb_podcast_source_start_download_cb (RBPodcastManager *pd,
				     RhythmDBEntry *entry,
				     RBPodcastSource *source)
{
	RBShell *shell = rb_podcast_source_get_shell (source);
	char *podcast_name;

	podcast_name = g_markup_escape_text (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), -1);
	rb_shell_notify_custom (shell, 4000, _("Downloading podcast"), podcast_name, NULL, FALSE);

	g_object_unref (shell);
	g_free (podcast_name);
}

static void
rb_podcast_source_finish_download_cb (RBPodcastManager *pd,
				      RhythmDBEntry *entry,
				      RBPodcastSource *source)
{
	RBShell *shell = rb_podcast_source_get_shell (source);
	char *podcast_name;

	podcast_name = g_markup_escape_text (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), -1);
	rb_shell_notify_custom (shell, 4000, _("Finished downloading podcast"), podcast_name, NULL, FALSE);

	g_object_unref (shell);
	g_free (podcast_name);
}

static void
rb_podcast_source_feed_updates_available_cb (RBPodcastManager *pd,
					     RhythmDBEntry *entry,
					     RBPodcastSource *source)
{
	RBShell *shell = rb_podcast_source_get_shell (source);
	char *podcast_name;

	podcast_name = g_markup_escape_text (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE), -1);
	rb_shell_notify_custom (shell, 4000, _("New updates available from"), podcast_name, NULL, FALSE);

	g_object_unref (shell);
	g_free (podcast_name);

}

static RBShell *
rb_podcast_source_get_shell (RBPodcastSource *source)
{
	RBShell *shell;

	g_object_get (source, "shell", &shell, NULL);

	return shell;
}

void
rb_podcast_source_shutdown	(RBPodcastSource *source)
{
	rb_debug ("podcast source shutdown");
	rb_podcast_manager_shutdown (source->priv->podcast_mgr);
}

static gint
rb_podcast_source_post_date_cell_sort_func (RhythmDBEntry *a,
					    RhythmDBEntry *b,
					    RhythmDBQueryModel *model)
{
	gulong a_val, b_val;
	gint ret;

	a_val = rhythmdb_entry_get_ulong (a, RHYTHMDB_PROP_POST_TIME);
	b_val = rhythmdb_entry_get_ulong (b, RHYTHMDB_PROP_POST_TIME);

	if (a_val != b_val)
		ret = (a_val > b_val) ? 1 : -1;
	else
		ret = rb_podcast_source_post_feed_cell_sort_func (a, b, model);

        return ret;
}

static gint
rb_podcast_source_post_status_cell_sort_func (RhythmDBEntry *a,
					      RhythmDBEntry *b,
					      RhythmDBQueryModel *model)
{
	gulong a_val, b_val;
	gint ret;

	a_val = rhythmdb_entry_get_ulong (a, RHYTHMDB_PROP_STATUS);
	b_val = rhythmdb_entry_get_ulong (b, RHYTHMDB_PROP_STATUS);

        if (a_val != b_val)
		ret = (a_val > b_val) ? 1 : -1;
	else
		ret = rb_podcast_source_post_feed_cell_sort_func (a, b, model);

	return ret;
}

static gint
rb_podcast_source_post_feed_cell_sort_func (RhythmDBEntry *a,
					    RhythmDBEntry *b,
					    RhythmDBQueryModel *model)
{
	const char *a_str, *b_str;
	gulong a_val, b_val;
	gint ret;

	/* feeds */
	a_str = rhythmdb_entry_get_string (a, RHYTHMDB_PROP_ALBUM);
	b_str = rhythmdb_entry_get_string (b, RHYTHMDB_PROP_ALBUM);

	ret = strcmp (a_str, b_str);
	if (ret != 0)
		return ret;

	a_val = rhythmdb_entry_get_ulong (a, RHYTHMDB_PROP_POST_TIME);
	b_val = rhythmdb_entry_get_ulong (b, RHYTHMDB_PROP_POST_TIME);

	if (a_val != b_val)
		return (a_val > b_val) ? 1 : -1;

	/* titles */
	a_str = rhythmdb_entry_get_string (a, RHYTHMDB_PROP_TITLE);
	b_str = rhythmdb_entry_get_string (b, RHYTHMDB_PROP_TITLE);

	ret = strcmp (a_str, b_str);
	if (ret != 0)
		return ret;

	/* location */
	a_str = rhythmdb_entry_get_string (a, RHYTHMDB_PROP_LOCATION);
	b_str = rhythmdb_entry_get_string (b, RHYTHMDB_PROP_LOCATION);

	ret = strcmp (a_str, b_str);
	return ret;
}

static gboolean
rb_podcast_source_download_process_error_cb (RBPodcastManager *pd,
					     const char *error,
					     gboolean existing,
					     RBPodcastSource *source)
{
	GtkWidget *dialog;
	int result;

	/* if the podcast feed doesn't already exist in the db,
	 * ask if the user wants to add it anyway; if it already
	 * exists, there's nothing to do besides reporting the error.
	 */
	dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (source))),
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 existing ? GTK_BUTTONS_OK : GTK_BUTTONS_YES_NO,
					 _("Error in podcast"));

	if (existing) {
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  "%s", error);
	} else {
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  _("%s. Would you like to add the podcast feed anyway?"), error);
	}

	gtk_window_set_title (GTK_WINDOW (dialog), "");
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);

	result = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	/* in the existing feed case, the response will be _OK or _NONE.
	 * we want to return FALSE here in this case, so this check works.
	 */
	return (result == GTK_RESPONSE_YES);
}

static void
rb_podcast_source_entry_activated_cb (RBEntryView *view,
				      RhythmDBEntry *entry,
				      RBPodcastSource *source)
{
	GValue val = {0,};

	/* check to see if it has already been downloaded */
	if (rb_podcast_manager_entry_downloaded (entry))
		return;

	g_value_init (&val, G_TYPE_ULONG);
	g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_WAITING);
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_STATUS, &val);
	rhythmdb_commit (source->priv->db);
	g_value_unset (&val);

	rb_podcast_manager_download_entry (source->priv->podcast_mgr, entry);
}

static gboolean
impl_can_add_to_queue (RBSource *source)
{
	RBEntryView *songs;
	GList *selection;
	GList *iter;
	gboolean ok = FALSE;

	songs = rb_source_get_entry_view (source);
	selection = rb_entry_view_get_selected_entries (songs);

	if (selection == NULL)
		return FALSE;

	/* If at least one entry has been downloaded, enable add to queue.
	 * We'll filter out those that haven't when adding to the queue.
	 */
	for (iter = selection; iter && !ok; iter = iter->next) {
		RhythmDBEntry *entry = (RhythmDBEntry *)iter->data;
		ok |= rb_podcast_manager_entry_downloaded (entry);
	}

	g_list_foreach (selection, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (selection);

	return ok;
}

static void
impl_add_to_queue (RBSource *source, RBSource *queue)
{
	RBEntryView *songs;
	GList *selection;
	GList *iter;

	songs = rb_source_get_entry_view (source);
	selection = rb_entry_view_get_selected_entries (songs);

	if (selection == NULL)
		return;

	for (iter = selection; iter; iter = iter->next) {
		RhythmDBEntry *entry = (RhythmDBEntry *)iter->data;
		if (!rb_podcast_manager_entry_downloaded (entry))
			continue;
		rb_static_playlist_source_add_entry (RB_STATIC_PLAYLIST_SOURCE (queue),
						     entry, -1);
	}

	g_list_foreach (selection, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (selection);
}

static GList *
impl_get_ui_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("PodcastUpdateAllFeeds"));
	actions = g_list_prepend (actions, g_strdup ("MusicNewPodcast"));

	return actions;
}

static GList *
impl_get_search_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("PodcastSearchEpisodes"));
	actions = g_list_prepend (actions, g_strdup ("PodcastSearchFeeds"));
	actions = g_list_prepend (actions, g_strdup ("PodcastSearchAll"));

	return actions;
}

static void
rb_podcast_source_location_added_cb (RBURIDialog *dialog,
				     const char *location,
				     RBPodcastSource *source)
{
	rb_podcast_manager_subscribe_feed (source->priv->podcast_mgr, location, FALSE);
}

static void
rb_podcast_source_cmd_new_podcast (GtkAction *action,
				   RBPodcastSource *source)
{
	GtkWidget *dialog;

	rb_debug ("Got new podcast command");

	dialog = rb_uri_dialog_new (_("New Podcast Feed"), _("URL of podcast feed:"));
	g_signal_connect_object (G_OBJECT (dialog),
				 "location-added",
				 G_CALLBACK (rb_podcast_source_location_added_cb),
				 source, 0);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static void
impl_get_status (RBSource *source, char **text, char **progress_text, float *progress)
{
	RhythmDBQueryModel *query_model;

	/* hack to get these strings marked for translation */
	if (0) {
		ngettext ("%d episode", "%d episodes", 0);
	}

	g_object_get (source, "query-model", &query_model, NULL);
	if (query_model != NULL) {
		*text = rhythmdb_query_model_compute_status_normal (query_model,
								    "%d episode",
								    "%d episodes");
		if (rhythmdb_query_model_has_pending_changes (query_model))
			*progress = -1.0f;

		g_object_unref (query_model);
	} else {
		*text = g_strdup ("");
	}
}

static guint
impl_want_uri (RBSource *source, const char *uri)
{
	if (g_str_has_prefix (uri, "http://") == FALSE)
		return 0;

	if (g_str_has_suffix (uri, ".xml") ||
	    g_str_has_suffix (uri, ".rss"))
		return 100;

	return 0;
}

static gboolean
impl_add_uri (RBSource *asource, const char *uri, const char *title, const char *genre)
{
	RBPodcastSource *source = RB_PODCAST_SOURCE (asource);
	rb_podcast_manager_subscribe_feed (source->priv->podcast_mgr, uri, FALSE);
	return TRUE;
}

static void
rb_podcast_source_entry_changed_cb (RhythmDB *db,
				    RhythmDBEntry *entry,
				    GSList *changes,
				    RBPodcastSource *source)
{
	RhythmDBEntryType entry_type;
	gboolean feed_changed;
	GSList *t;

	entry_type = rhythmdb_entry_get_entry_type (entry);
	if (entry_type != RHYTHMDB_ENTRY_TYPE_PODCAST_FEED)
		return;

	feed_changed = FALSE;
	for (t = changes; t; t = t->next) {
		RhythmDBEntryChange *change = t->data;

		if (change->prop == RHYTHMDB_PROP_PLAYBACK_ERROR) {
			feed_changed = TRUE;
			break;
		}
	}

	if (feed_changed) {
		const char *loc;
		GtkTreeIter iter;

		loc = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		if (rhythmdb_property_model_iter_from_string (source->priv->feed_model,
							      loc,
							      &iter)) {
			GtkTreePath *path;

			path = gtk_tree_model_get_path (GTK_TREE_MODEL (source->priv->feed_model),
						        &iter);
			gtk_tree_model_row_changed (GTK_TREE_MODEL (source->priv->feed_model),
						    path,
						    &iter);
			gtk_tree_path_free (path);
		}
	}
}

static void
rb_podcast_source_pixbuf_clicked_cb (RBCellRendererPixbuf *renderer,
				     const char *path_string,
				     RBPodcastSource *source)
{
	GtkTreePath *path;
	GtkTreeIter iter;

	g_return_if_fail (path_string != NULL);

	path = gtk_tree_path_new_from_string (path_string);
	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (source->priv->feed_model), &iter, path)) {
		RhythmDBEntry *entry;
		char *feed_url;

		gtk_tree_model_get (GTK_TREE_MODEL (source->priv->feed_model),
				    &iter,
				    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &feed_url,
				    -1);

		entry = rhythmdb_entry_lookup_by_location (source->priv->db, feed_url);
		if (entry != NULL) {
			const gchar *error;

			error = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_PLAYBACK_ERROR);
			if (error) {
				rb_error_dialog (NULL, _("Podcast Error"), "%s", error);
			}
		}

		g_free (feed_url);
	}

	gtk_tree_path_free (path);
}

