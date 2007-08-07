/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of Rhythmbox Audioscrobbler support
 *
 *  Copyright (C) 2005 Alex Revo <xiphoidappendix@gmail.com>,
 *		       Ruben Vermeersch <ruben@Lambda1.be>
 *            (C) 2007 Christophe Fergeau <teuf@gnome.org>
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

#define __EXTENSIONS__

#include <errno.h>

#include <string.h>
#include <time.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <gtk/gtk.h>
#include <gconf/gconf-value.h>

#include <libsoup/soup.h>
#include <libsoup/soup-uri.h>

#include "config.h"
#include "eel-gconf-extensions.h"
#include "rb-audioscrobbler.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-glade-helpers.h"
#include "rb-preferences.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-source.h"
#include "md5.h"
#include "rb-proxy-config.h"
#include "rb-cut-and-paste-code.h"
#include "rb-plugin.h"


#define CLIENT_ID "rbx"
#define CLIENT_VERSION VERSION
#define MAX_QUEUE_SIZE 1000
#define MAX_SUBMIT_SIZE	10
#define SCROBBLER_URL "http://post.audioscrobbler.com/"
#define SCROBBLER_VERSION "1.1"

#define SCROBBLER_DATE_FORMAT "%Y%%2D%m%%2D%d%%20%H%%3A%M%%3A%S"

#define EXTRA_URI_ENCODE_CHARS	"&+"

typedef struct
{
	gchar *artist;
	gchar *album;
	gchar *title;
	guint length;
	gchar *mbid;
	time_t play_time;
} AudioscrobblerEntry;

typedef struct
{
	gchar *artist;
	gchar *album;
	gchar *title;
	guint length;
	gchar *mbid;
	gchar *timestamp;
} AudioscrobblerEncodedEntry;


struct _RBAudioscrobblerPrivate
{
	RBShellPlayer *shell_player;

	/* Widgets for the prefs pane */
	GtkWidget *config_widget;
	GtkWidget *username_entry;
	GtkWidget *username_label;
	GtkWidget *password_entry;
	GtkWidget *password_label;
	GtkWidget *status_label;
	GtkWidget *submit_count_label;
	GtkWidget *submit_time_label;
	GtkWidget *queue_count_label;

	/* Data for the prefs pane */
	guint submit_count;
	char *submit_time;
	guint queue_count;
	enum {
		STATUS_OK = 0,
		HANDSHAKING,
		REQUEST_FAILED,
		BAD_USERNAME,
		BAD_PASSWORD,
		HANDSHAKE_FAILED,
		CLIENT_UPDATE_REQUIRED,
		SUBMIT_FAILED,
		QUEUE_TOO_LONG,
		GIVEN_UP,
	} status;
	char *status_msg;

	/* Submission queue */
	GQueue *queue;
	/* Entries currently being submitted */
	GQueue *submission;

	guint failures;
	/* Handshake has been done? */
	gboolean handshake;
	time_t handshake_next;
	time_t submit_next;
	time_t submit_interval;

	/* Only write the queue to a file if it has been changed */
	gboolean queue_changed;

	/* Authentication cookie + authentication info */
	gchar *md5_challenge;
	gchar *username;
	gchar *password;
	gchar *submit_url;

	/* Currently playing song info, if NULL this means the currently
	 * playing song isn't eligible to be queued
	 */
	AudioscrobblerEntry *currently_playing;
	guint current_elapsed;

	/* Preference notifications */
	guint notification_username_id;
	guint notification_password_id;

	guint timeout_id;

	/* HTTP requests session */
	SoupSession *soup_session;
	RBProxyConfig *proxy_config;

	/* callback for songs that were played offline (eg on an iPod) */
	gulong offline_play_notify_id;
};

#define RB_AUDIOSCROBBLER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOSCROBBLER, RBAudioscrobblerPrivate))


static void	     audioscrobbler_entry_init (AudioscrobblerEntry *entry);
static void	     audioscrobbler_entry_free (AudioscrobblerEntry *entry);
static void          audioscrobbler_encoded_entry_free (AudioscrobblerEncodedEntry *entry);
static AudioscrobblerEncodedEntry *audioscrobbler_entry_encode (AudioscrobblerEntry *entry);



static gboolean	     rb_audioscrobbler_load_queue (RBAudioscrobbler *audioscrobbler);
static int	     rb_audioscrobbler_save_queue (RBAudioscrobbler *audioscrobbler);
static void	     rb_audioscrobbler_print_queue (RBAudioscrobbler *audioscrobbler, gboolean submission);
static void	     rb_audioscrobbler_free_queue_entries (RBAudioscrobbler *audioscrobbler, GQueue **queue);

static void	     rb_audioscrobbler_class_init (RBAudioscrobblerClass *klass);
static void	     rb_audioscrobbler_init (RBAudioscrobbler *audioscrobbler);
static void	     rb_audioscrobbler_get_property (GObject *object,
						    guint prop_id,
						    GValue *value,
						    GParamSpec *pspec);
static void	     rb_audioscrobbler_set_property (GObject *object,
						    guint prop_id,
						    const GValue *value,
						    GParamSpec *pspec);
static void	     rb_audioscrobbler_dispose (GObject *object);
static void	     rb_audioscrobbler_finalize (GObject *object);

static void	     rb_audioscrobbler_add_timeout (RBAudioscrobbler *audioscrobbler);
static gboolean	     rb_audioscrobbler_timeout_cb (RBAudioscrobbler *audioscrobbler);

static gchar *	     mkmd5 (char *string);
static void	     rb_audioscrobbler_parse_response (RBAudioscrobbler *audioscrobbler, SoupMessage *msg);
static void	     rb_audioscrobbler_perform (RBAudioscrobbler *audioscrobbler,
					       char *url,
					       char *post_data,
						   SoupMessageCallbackFn response_handler);
static void	     rb_audioscrobbler_do_handshake (RBAudioscrobbler *audioscrobbler);
static void	     rb_audioscrobbler_do_handshake_cb (SoupMessage *msg, gpointer user_data);
static void	     rb_audioscrobbler_submit_queue (RBAudioscrobbler *audioscrobbler);
static void	     rb_audioscrobbler_submit_queue_cb (SoupMessage *msg, gpointer user_data);

static void	     rb_audioscrobbler_import_settings (RBAudioscrobbler *audioscrobbler);
static void	     rb_audioscrobbler_preferences_sync (RBAudioscrobbler *audioscrobbler);

static void	     rb_audioscrobbler_gconf_changed_cb (GConfClient *client,
							 guint cnxn_id,
							 GConfEntry *entry,
							 RBAudioscrobbler *audioscrobbler);
static void	     rb_audioscrobbler_song_changed_cb (RBShellPlayer *player,
							RhythmDBEntry *entry,
							RBAudioscrobbler *audioscrobbler);
static void	     rb_audioscrobbler_proxy_config_changed_cb (RBProxyConfig *config,
								RBAudioscrobbler *audioscrobbler);
static void          rb_audioscrobbler_offline_play_notify_cb (RhythmDB *db,
							       RhythmDBEntry *rb_entry,
							       const gchar *property_name,
							       const GValue *metadata,
							       RBAudioscrobbler *audioscrobbler);



enum
{
	PROP_0,
	PROP_SHELL_PLAYER,
	PROP_PROXY_CONFIG
};

G_DEFINE_TYPE (RBAudioscrobbler, rb_audioscrobbler, G_TYPE_OBJECT)


static GObject *
rb_audioscrobbler_constructor (GType type,
			       guint n_construct_properties,
			       GObjectConstructParam *construct_properties)
{
	GObject *obj;
	RBAudioscrobbler *audioscrobbler;
	RhythmDB *db;

	/* Invoke parent constructor. */
	RBAudioscrobblerClass *klass;
	GObjectClass *parent_class;  
	klass = RB_AUDIOSCROBBLER_CLASS (g_type_class_peek (RB_TYPE_AUDIOSCROBBLER));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	obj = parent_class->constructor (type,
					 n_construct_properties,
					 construct_properties);

	audioscrobbler = RB_AUDIOSCROBBLER (obj);
	g_object_get (G_OBJECT (audioscrobbler->priv->shell_player),
		      "db", &db, 
		      NULL);

	audioscrobbler->priv->offline_play_notify_id = 
		g_signal_connect_object (db, 
					 "entry-extra-metadata-notify::rb:offlinePlay",
					 (GCallback)rb_audioscrobbler_offline_play_notify_cb, 
					 audioscrobbler, 0);
	g_object_unref (G_OBJECT (db));

	return obj;
}

/* Class-related functions: */
static void
rb_audioscrobbler_class_init (RBAudioscrobblerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);


	object_class->constructor = rb_audioscrobbler_constructor;
	object_class->dispose = rb_audioscrobbler_dispose;
	object_class->finalize = rb_audioscrobbler_finalize;

	object_class->set_property = rb_audioscrobbler_set_property;
	object_class->get_property = rb_audioscrobbler_get_property;

	g_object_class_install_property (object_class,
					 PROP_SHELL_PLAYER,
					 g_param_spec_object ("shell-player",
							      "RBShellPlayer",
							      "RBShellPlayer object",
							      RB_TYPE_SHELL_PLAYER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_PROXY_CONFIG,
					 g_param_spec_object ("proxy-config",
							      "RBProxyConfig",
							      "RBProxyConfig object",
							      RB_TYPE_PROXY_CONFIG,
							      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerPrivate));
}

static void
rb_audioscrobbler_init (RBAudioscrobbler *audioscrobbler)
{
	rb_debug ("Initialising Audioscrobbler");
	rb_debug ("Plugin ID: %s, Version %s (Protocol %s)",
		  CLIENT_ID, CLIENT_VERSION, SCROBBLER_VERSION);

	audioscrobbler->priv = RB_AUDIOSCROBBLER_GET_PRIVATE (audioscrobbler);

	audioscrobbler->priv->queue = g_queue_new();
	audioscrobbler->priv->submission = g_queue_new();
	audioscrobbler->priv->md5_challenge = g_strdup ("");
	audioscrobbler->priv->username = NULL;
	audioscrobbler->priv->password = NULL;
	audioscrobbler->priv->submit_url = g_strdup ("");

	rb_audioscrobbler_load_queue (audioscrobbler);

	rb_audioscrobbler_import_settings (audioscrobbler);

	/* gconf notifications: */
	audioscrobbler->priv->notification_username_id =
		eel_gconf_notification_add (CONF_AUDIOSCROBBLER_USERNAME,
				    (GConfClientNotifyFunc) rb_audioscrobbler_gconf_changed_cb,
				    audioscrobbler);
	audioscrobbler->priv->notification_password_id =
		eel_gconf_notification_add (CONF_AUDIOSCROBBLER_PASSWORD,
				    (GConfClientNotifyFunc) rb_audioscrobbler_gconf_changed_cb,
				    audioscrobbler);

	rb_audioscrobbler_preferences_sync (audioscrobbler);
}

static void
rb_audioscrobbler_dispose (GObject *object)
{
	RBAudioscrobbler *audioscrobbler;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_AUDIOSCROBBLER (object));

	audioscrobbler = RB_AUDIOSCROBBLER (object);

	if (audioscrobbler->priv->offline_play_notify_id != 0) {
		RhythmDB *db;

		g_object_get (G_OBJECT (audioscrobbler->priv->shell_player),
			      "db", &db, 
			      NULL);
		g_signal_handler_disconnect (db, audioscrobbler->priv->offline_play_notify_id);
		audioscrobbler->priv->offline_play_notify_id = 0;
		g_object_unref (db);
	}


	if (audioscrobbler->priv->notification_username_id != 0) {
		eel_gconf_notification_remove (audioscrobbler->priv->notification_username_id);
		audioscrobbler->priv->notification_username_id = 0;
	}
	if (audioscrobbler->priv->notification_password_id != 0) {
		eel_gconf_notification_remove (audioscrobbler->priv->notification_password_id);
		audioscrobbler->priv->notification_password_id = 0;
	}

	if (audioscrobbler->priv->timeout_id != 0) {
		g_source_remove (audioscrobbler->priv->timeout_id);
		audioscrobbler->priv->timeout_id = 0;
	}

	if (audioscrobbler->priv->soup_session != NULL) {
		soup_session_abort (audioscrobbler->priv->soup_session);
		g_object_unref (audioscrobbler->priv->soup_session);
		audioscrobbler->priv->soup_session = NULL;
	}

	if (audioscrobbler->priv->proxy_config != NULL) {
		g_object_unref (audioscrobbler->priv->proxy_config);
		audioscrobbler->priv->proxy_config = NULL;
	}

	if (audioscrobbler->priv->shell_player != NULL) {
		g_object_unref (audioscrobbler->priv->shell_player);
		audioscrobbler->priv->shell_player = NULL;
	}

	G_OBJECT_CLASS (rb_audioscrobbler_parent_class)->dispose (object);
}

static void
rb_audioscrobbler_finalize (GObject *object)
{
	RBAudioscrobbler *audioscrobbler;

	rb_debug ("Finalizing Audioscrobbler");

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_AUDIOSCROBBLER (object));

	audioscrobbler = RB_AUDIOSCROBBLER (object);

	/* Save any remaining entries */
	rb_audioscrobbler_save_queue (audioscrobbler);

	g_free (audioscrobbler->priv->md5_challenge);
	g_free (audioscrobbler->priv->username);
	g_free (audioscrobbler->priv->password);
	g_free (audioscrobbler->priv->submit_url);
	if (audioscrobbler->priv->currently_playing != NULL) {
		audioscrobbler_entry_free (audioscrobbler->priv->currently_playing);
		audioscrobbler->priv->currently_playing = NULL;
	}

	rb_audioscrobbler_free_queue_entries (audioscrobbler, &audioscrobbler->priv->queue);
	rb_audioscrobbler_free_queue_entries (audioscrobbler, &audioscrobbler->priv->submission);

	G_OBJECT_CLASS (rb_audioscrobbler_parent_class)->finalize (object);
}

RBAudioscrobbler*
rb_audioscrobbler_new (RBShellPlayer *shell_player,
		       RBProxyConfig *proxy_config)
{
	return g_object_new (RB_TYPE_AUDIOSCROBBLER,
			     "shell-player", shell_player,
			     "proxy-config", proxy_config,
			     NULL);
}

static void
rb_audioscrobbler_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	RBAudioscrobbler *audioscrobbler = RB_AUDIOSCROBBLER (object);

	switch (prop_id) {
	case PROP_SHELL_PLAYER:
		audioscrobbler->priv->shell_player = g_value_get_object (value);
		g_object_ref (G_OBJECT (audioscrobbler->priv->shell_player));
		g_signal_connect_object (G_OBJECT (audioscrobbler->priv->shell_player),
					 "playing-song-changed",
					 G_CALLBACK (rb_audioscrobbler_song_changed_cb),
					 audioscrobbler, 0);
		break;
	case PROP_PROXY_CONFIG:
		audioscrobbler->priv->proxy_config = g_value_get_object (value);
		g_object_ref (G_OBJECT (audioscrobbler->priv->proxy_config));
		g_signal_connect_object (G_OBJECT (audioscrobbler->priv->proxy_config),
					 "config-changed",
					 G_CALLBACK (rb_audioscrobbler_proxy_config_changed_cb),
					 audioscrobbler, 0);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_audioscrobbler_get_property (GObject *object,
				guint prop_id,
				GValue *value,
				GParamSpec *pspec)
{
	RBAudioscrobbler *audioscrobbler = RB_AUDIOSCROBBLER (object);

	switch (prop_id) {
	case PROP_SHELL_PLAYER:
		g_value_set_object (value, audioscrobbler->priv->shell_player);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* Add the audioscrobbler thread timer */
static void
rb_audioscrobbler_add_timeout (RBAudioscrobbler *audioscrobbler)
{
	if (!audioscrobbler->priv->timeout_id) {
		rb_debug ("Adding Audioscrobbler timer (15 seconds)");
		audioscrobbler->priv->timeout_id = 
			g_timeout_add (15000, (GSourceFunc) rb_audioscrobbler_timeout_cb,
				       audioscrobbler);
	}
}

static gboolean
rb_audioscrobbler_is_queueable (RhythmDBEntry *entry)
{
	const char *title;
	const char *artist;
	gulong duration;
	RhythmDBEntryType type;

	/* First, check if the entry is appropriate for sending to 
	 * audioscrobbler
	 */
	type = rhythmdb_entry_get_entry_type (entry);
	if (type->category != RHYTHMDB_ENTRY_NORMAL) {
		return FALSE;
	}
	if (type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST) {
		return FALSE;
	}
	if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_PLAYBACK_ERROR)) {
		return FALSE;
	}

	title = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
	artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
	duration = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);

	/* The specification (v1.1) tells us to ignore entries which do not
	 * verify those conditions
	 */
	return 	((duration >= 30) &&
		 (strcmp (artist, _("Unknown")) != 0) &&
		 (strcmp (title, _("Unknown")) != 0));
}

static AudioscrobblerEntry *
rb_audioscrobbler_create_entry (RhythmDBEntry *rb_entry)
{
	AudioscrobblerEntry *as_entry = g_new0 (AudioscrobblerEntry, 1);

	as_entry->title = rhythmdb_entry_dup_string (rb_entry,
						     RHYTHMDB_PROP_TITLE);
	as_entry->artist = rhythmdb_entry_dup_string (rb_entry,
						      RHYTHMDB_PROP_ARTIST);
	as_entry->album = rhythmdb_entry_dup_string (rb_entry,
						     RHYTHMDB_PROP_ALBUM);
	if (strcmp (as_entry->album, _("Unknown")) == 0) {
		g_free (as_entry->album);
		as_entry->album = g_strdup ("");
	}
	as_entry->length = rhythmdb_entry_get_ulong (rb_entry,
						     RHYTHMDB_PROP_DURATION);
	as_entry->mbid = rhythmdb_entry_dup_string (rb_entry,
						    RHYTHMDB_PROP_MUSICBRAINZ_TRACKID);

	return as_entry;
}

static gboolean
rb_audioscrobbler_add_to_queue (RBAudioscrobbler *audioscrobbler,
				AudioscrobblerEntry *entry)
{	
	if (g_queue_get_length (audioscrobbler->priv->queue) < MAX_QUEUE_SIZE){
		g_queue_push_tail (audioscrobbler->priv->queue, entry);
		audioscrobbler->priv->queue_changed = TRUE;
		audioscrobbler->priv->queue_count++;
		return TRUE;
	} else {
		rb_debug ("Queue is too long.  Not adding song to queue");
		g_free (audioscrobbler->priv->status_msg);
		audioscrobbler->priv->status = QUEUE_TOO_LONG;
		audioscrobbler->priv->status_msg = NULL;
		return FALSE;
	}
}

static void
maybe_add_current_song_to_queue (RBAudioscrobbler *audioscrobbler)
{
	guint elapsed;
	int elapsed_delta;
	AudioscrobblerEntry *cur_entry;

	cur_entry = audioscrobbler->priv->currently_playing;
	if (cur_entry == NULL) {
		return;
	}

	rb_debug ("Adding currently playing song to queue");

	rb_shell_player_get_playing_time (audioscrobbler->priv->shell_player, &elapsed, NULL);
	elapsed_delta = elapsed - audioscrobbler->priv->current_elapsed;
	audioscrobbler->priv->current_elapsed = elapsed;
	
	if ((elapsed >= cur_entry->length / 2 || elapsed >= 240) && elapsed_delta < 20) {
		time (&cur_entry->play_time);
		if (rb_audioscrobbler_add_to_queue (audioscrobbler, cur_entry)){
			audioscrobbler->priv->currently_playing = NULL;
		}
		
		rb_audioscrobbler_preferences_sync (audioscrobbler);
	} else if (elapsed_delta > 20) {
		rb_debug ("Skipping detected; not submitting current song");
		/* not sure about this - what if I skip to somewhere towards
		 * the end, but then go back and listen to the whole song?
		 */
		audioscrobbler_entry_free (audioscrobbler->priv->currently_playing);
		audioscrobbler->priv->currently_playing = NULL;

	}
}

/* updates the queue and submits entries as required */
static gboolean
rb_audioscrobbler_timeout_cb (RBAudioscrobbler *audioscrobbler)
{
	maybe_add_current_song_to_queue (audioscrobbler);

	/* do handshake if we need to */
	rb_audioscrobbler_do_handshake (audioscrobbler);

	/* if there's something in the queue, submit it if we can, save it otherwise */
	if (!g_queue_is_empty(audioscrobbler->priv->queue)) {
		if (audioscrobbler->priv->handshake)
			rb_audioscrobbler_submit_queue (audioscrobbler);
		else
			rb_audioscrobbler_save_queue (audioscrobbler);
	}
	return TRUE;
}

static void
rb_audioscrobbler_offline_play_notify_cb (RhythmDB *db,
					  RhythmDBEntry *rb_entry,
					  const gchar *property_name,
					  const GValue *metadata,
					  RBAudioscrobbler *audioscrobbler)
{
	g_return_if_fail (G_VALUE_HOLDS_ULONG (metadata));

	/* FIXME: do sanity checks on play_date value? */
	if (rb_audioscrobbler_is_queueable (rb_entry)) {
		AudioscrobblerEntry *as_entry;
		
		as_entry = rb_audioscrobbler_create_entry (rb_entry);
		as_entry->play_time = g_value_get_ulong (metadata);
		rb_audioscrobbler_add_to_queue (audioscrobbler, as_entry);
	}
}



/* Audioscrobbler functions: */
static gchar *
mkmd5 (char *string)
{
	md5_state_t md5state;
	guchar md5pword[16];
	gchar md5_response[33];

	int j = 0;

	memset (md5_response, 0, sizeof (md5_response));

	md5_init (&md5state);
	md5_append (&md5state, (unsigned char*)string, strlen (string));
	md5_finish (&md5state, md5pword);

	for (j = 0; j < 16; j++) {
		char a[3];
		sprintf (a, "%02x", md5pword[j]);
		md5_response[2*j] = a[0];
		md5_response[2*j+1] = a[1];
	}

	return (g_strdup (md5_response));
}

static void
rb_audioscrobbler_parse_response (RBAudioscrobbler *audioscrobbler, SoupMessage *msg)
{
	rb_debug ("Parsing response, status=%d", msg->status_code);

	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code) && (msg->response).body != NULL) {
		gchar *body;
		gchar **breaks;

		body = g_malloc0 ((msg->response).length + 1);
		memcpy (body, (msg->response).body, (msg->response).length);

		g_strstrip (body);
		breaks = g_strsplit (body, "\n", 4);
		int i;

		g_free (audioscrobbler->priv->status_msg);
		audioscrobbler->priv->status = STATUS_OK;
		audioscrobbler->priv->status_msg = NULL;
		for (i = 0; breaks[i] != NULL; i++) {
			rb_debug ("RESPONSE: %s", breaks[i]);
			if (g_str_has_prefix (breaks[i], "UPTODATE")) {
				rb_debug ("UPTODATE");

				if (breaks[i+1] != NULL) {
					g_free (audioscrobbler->priv->md5_challenge);
					audioscrobbler->priv->md5_challenge = g_strdup (breaks[i+1]);
					rb_debug ("MD5 challenge: \"%s\"", audioscrobbler->priv->md5_challenge);

					if (breaks[i+2] != NULL) {
						g_free (audioscrobbler->priv->submit_url);
						audioscrobbler->priv->submit_url = g_strdup (breaks[i+2]);
						rb_debug ("Submit URL: \"%s\"", audioscrobbler->priv->submit_url);
						i++;
					}
					i++;
				}

			} else if (g_str_has_prefix (breaks[i], "UPDATE")) {
				rb_debug ("UPDATE");
				audioscrobbler->priv->status = CLIENT_UPDATE_REQUIRED;

				if (breaks[i+1] != NULL) {
					g_free (audioscrobbler->priv->md5_challenge);
					audioscrobbler->priv->md5_challenge = g_strdup (breaks[i+1]);
					rb_debug ("MD5 challenge: \"%s\"", audioscrobbler->priv->md5_challenge);

					if (breaks[i+2] != NULL) {
						g_free (audioscrobbler->priv->submit_url);
						audioscrobbler->priv->submit_url = g_strdup (breaks[i+2]);
						rb_debug ("Submit URL: \"%s\"", audioscrobbler->priv->submit_url);
						i++;
					}
					i++;
				}

			} else if (g_str_has_prefix (breaks[i], "FAILED")) {
				audioscrobbler->priv->status = HANDSHAKE_FAILED;

				if (strlen (breaks[i]) > 7) {
					rb_debug ("FAILED: \"%s\"", breaks[i] + 7);
					audioscrobbler->priv->status_msg = g_strdup (breaks[i] + 7);
				} else {
					rb_debug ("FAILED");
				}


			} else if (g_str_has_prefix (breaks[i], "BADUSER")) {
				rb_debug ("BADUSER");
				audioscrobbler->priv->status = BAD_USERNAME;
			} else if (g_str_has_prefix (breaks[i], "BADAUTH")) {
				rb_debug ("BADAUTH");
				audioscrobbler->priv->status = BAD_PASSWORD;
			} else if (g_str_has_prefix (breaks[i], "OK")) {
				rb_debug ("OK");
			} else if (g_str_has_prefix (breaks[i], "INTERVAL ")) {
				audioscrobbler->priv->submit_interval = g_ascii_strtod(breaks[i] + 9, NULL);
				rb_debug ("INTERVAL: %s", breaks[i] + 9);
			}
		}

		/* respect the last submit interval we were given */
		if (audioscrobbler->priv->submit_interval > 0)
			audioscrobbler->priv->submit_next = time(NULL) + audioscrobbler->priv->submit_interval;

		g_strfreev (breaks);
		g_free (body);
	} else {
		audioscrobbler->priv->status = REQUEST_FAILED;
		audioscrobbler->priv->status_msg = g_strdup (soup_status_get_phrase (msg->status_code));
	}
}

static gboolean
idle_unref_cb (GObject *object)
{
	g_object_unref (object);
	return FALSE;
}

/*
 * NOTE: the caller *must* unref the audioscrobbler object in an idle
 * handler created in the callback.
 */
static void
rb_audioscrobbler_perform (RBAudioscrobbler *audioscrobbler,
			   char *url,
			   char *post_data,
			   SoupMessageCallbackFn response_handler)
{
	SoupMessage *msg;

	msg = soup_message_new (post_data == NULL ? "GET" : "POST", url);

	if (post_data != NULL) {
		rb_debug ("Submitting to Audioscrobbler: %s", post_data);
		soup_message_set_request (msg,
					  "application/x-www-form-urlencoded",
					  SOUP_BUFFER_SYSTEM_OWNED,
					  post_data,
					  strlen (post_data));
	}

	/* create soup session, if we haven't got one yet */
	if (!audioscrobbler->priv->soup_session) {
		SoupUri *uri;

		uri = rb_proxy_config_get_libsoup_uri (audioscrobbler->priv->proxy_config);
		audioscrobbler->priv->soup_session = soup_session_async_new_with_options (
					"proxy-uri", uri,
					NULL);
		if (uri)
			soup_uri_free (uri);
	}

	soup_session_queue_message (audioscrobbler->priv->soup_session,
				    msg,
				    response_handler,
				    g_object_ref (audioscrobbler));
}

static gboolean
rb_audioscrobbler_should_handshake (RBAudioscrobbler *audioscrobbler)
{
	/* Perform handshake if necessary. Only perform handshake if
	 *   - we have no current handshake; AND
	 *   - we have waited the appropriate amount of time between
	 *     handshakes; AND
	 *   - we have a non-empty username
	 */
	if (audioscrobbler->priv->handshake) {
		rb_debug ("Not doing handshake; we already have one");
		return FALSE;
	}

	if (time (NULL) < audioscrobbler->priv->handshake_next) {
		rb_debug ("Too soon; time=%lu, handshake_next=%lu",
			  time (NULL),
			  audioscrobbler->priv->handshake_next);
		return FALSE;
	}

	if ((audioscrobbler->priv->username == NULL) ||
	    (strcmp (audioscrobbler->priv->username, "") == 0)) {
		rb_debug ("No username set");
		return FALSE;
	}

	return TRUE;
}

static void
rb_audioscrobbler_do_handshake (RBAudioscrobbler *audioscrobbler)
{
	gchar *username;
	gchar *url;

	if (!rb_audioscrobbler_should_handshake (audioscrobbler)) {
		return;
	}

	username = soup_uri_encode (audioscrobbler->priv->username, EXTRA_URI_ENCODE_CHARS);
	url = g_strdup_printf ("%s?hs=true&p=%s&c=%s&v=%s&u=%s",
			       SCROBBLER_URL,
			       SCROBBLER_VERSION,
			       CLIENT_ID,
			       CLIENT_VERSION,
			       username);
	g_free (username);

	/* Make sure we wait at least 30 minutes between handshakes. */
	audioscrobbler->priv->handshake_next = time (NULL) + 1800;

	rb_debug ("Performing handshake with Audioscrobbler server: %s", url);

	audioscrobbler->priv->status = HANDSHAKING;
	rb_audioscrobbler_preferences_sync (audioscrobbler);

	rb_audioscrobbler_perform (audioscrobbler,
				   url,
				   NULL,
				   rb_audioscrobbler_do_handshake_cb);

	g_free (url);
}


static void
rb_audioscrobbler_do_handshake_cb (SoupMessage *msg, gpointer user_data)
{
	RBAudioscrobbler *audioscrobbler = RB_AUDIOSCROBBLER(user_data);

	rb_debug ("Handshake response");
	rb_audioscrobbler_parse_response (audioscrobbler, msg);
	rb_audioscrobbler_preferences_sync (audioscrobbler);

	switch (audioscrobbler->priv->status) {
	case STATUS_OK:
	case CLIENT_UPDATE_REQUIRED:
		audioscrobbler->priv->handshake = TRUE;
		audioscrobbler->priv->failures = 0;
		break;
	default:
		rb_debug ("Handshake failed");
		++audioscrobbler->priv->failures;
		break;
	}

	g_idle_add ((GSourceFunc) idle_unref_cb, audioscrobbler);
}


static gchar *
rb_audioscrobbler_build_authentication_data (RBAudioscrobbler *audioscrobbler)
{
	gchar *md5_password;
	gchar *md5_temp;
	gchar *md5_response;
	gchar *username;
	gchar *post_data;
	time_t now;

	/* Conditions:
	 *   - Must have username and password
	 *   - Must have md5_challenge
	 *   - Queue must not be empty
	 */
	if ((audioscrobbler->priv->username == NULL) 
	    || (*audioscrobbler->priv->username == '\0')) {
		rb_debug ("No username set");
		return NULL;
	}
	
	if ((audioscrobbler->priv->password == NULL) 
	    || (*audioscrobbler->priv->password == '\0')) {
		rb_debug ("No password set");
		return NULL;
	}
		
	if (*audioscrobbler->priv->md5_challenge == '\0') {
		rb_debug ("No md5 challenge");
		return NULL;
	}

	time(&now);
	if (now < audioscrobbler->priv->submit_next) {
		rb_debug ("Too soon (next submission in %ld seconds)",
			  audioscrobbler->priv->submit_next - now);
		return NULL;
	}

	if (g_queue_is_empty (audioscrobbler->priv->queue)) {
		rb_debug ("No queued songs to submit");
		return NULL;
	}

	md5_password = mkmd5 (audioscrobbler->priv->password);
	md5_temp = g_strconcat (md5_password,
				audioscrobbler->priv->md5_challenge,
				NULL);
	md5_response = mkmd5 (md5_temp);
	
	username = soup_uri_encode (audioscrobbler->priv->username, 
				    EXTRA_URI_ENCODE_CHARS);
	post_data = g_strdup_printf ("u=%s&s=%s&", username, md5_response);
	
	g_free (md5_password);
	g_free (md5_temp);
	g_free (md5_response);
	g_free (username);
	
	return post_data;
}

static gchar *
rb_audioscrobbler_build_post_data (RBAudioscrobbler *audioscrobbler,
				   const gchar *authentication_data)
{
	g_return_val_if_fail (!g_queue_is_empty (audioscrobbler->priv->queue),
			      NULL);

	gchar *post_data = g_strdup (authentication_data);
	int i = 0;
	do {
		AudioscrobblerEntry *entry;
		AudioscrobblerEncodedEntry *encoded;
		gchar *new;
		/* remove first queue entry */
		entry = g_queue_pop_head (audioscrobbler->priv->queue);
		encoded = audioscrobbler_entry_encode (entry);
		new = g_strdup_printf ("%sa[%d]=%s&t[%d]=%s&b[%d]=%s&m[%d]=%s&l[%d]=%d&i[%d]=%s&",
				       post_data,
				       i, encoded->artist,
				       i, encoded->title,
				       i, encoded->album,
				       i, encoded->mbid,
				       i, encoded->length,
				       i, encoded->timestamp);
		audioscrobbler_encoded_entry_free (encoded);
		g_free (post_data);
		post_data = new;

		/* add to submission list */
		g_queue_push_tail (audioscrobbler->priv->submission, 
				   entry);
		i++;
	} while ((!g_queue_is_empty(audioscrobbler->priv->queue)) && (i < MAX_SUBMIT_SIZE));
	
	return post_data;
}

static void
rb_audioscrobbler_submit_queue (RBAudioscrobbler *audioscrobbler)
{
	gchar *auth_data;

	auth_data = rb_audioscrobbler_build_authentication_data (audioscrobbler);
	if (auth_data != NULL) {
		gchar *post_data;
	
		post_data = rb_audioscrobbler_build_post_data (audioscrobbler,
							       auth_data);
		g_free (auth_data);
		rb_debug ("Submitting queue to Audioscrobbler");
		rb_audioscrobbler_print_queue (audioscrobbler, TRUE);

		rb_audioscrobbler_perform (audioscrobbler,
					   audioscrobbler->priv->submit_url,
					   post_data,
					   rb_audioscrobbler_submit_queue_cb);
		 /* libsoup will free post_data when the request is finished */
	}
}

static void
rb_g_queue_concat (GQueue *q1, GQueue *q2)
{
	GList *elem;

	while (!g_queue_is_empty (q2)) {
		elem = g_queue_pop_head_link (q2);
		g_queue_push_tail_link (q1, elem);
	}
}

static void
rb_audioscrobbler_submit_queue_cb (SoupMessage *msg, gpointer user_data)
{
	RBAudioscrobbler *audioscrobbler = RB_AUDIOSCROBBLER (user_data);

	rb_debug ("Submission response");
	rb_audioscrobbler_parse_response (audioscrobbler, msg);

	if (audioscrobbler->priv->status == STATUS_OK) {
		rb_debug ("Queue submitted successfully");
		rb_audioscrobbler_free_queue_entries (audioscrobbler, &audioscrobbler->priv->submission);
		audioscrobbler->priv->submission = g_queue_new ();
		rb_audioscrobbler_save_queue (audioscrobbler);

		audioscrobbler->priv->submit_count += audioscrobbler->priv->queue_count;
		audioscrobbler->priv->queue_count = 0;

		g_free (audioscrobbler->priv->submit_time);
		audioscrobbler->priv->submit_time = rb_utf_friendly_time (time (NULL));
	} else {
		++audioscrobbler->priv->failures;

		/* add failed submission entries back to queue */
		rb_g_queue_concat (audioscrobbler->priv->submission, 
				   audioscrobbler->priv->queue);
		g_assert (g_queue_is_empty (audioscrobbler->priv->queue));
		g_queue_free (audioscrobbler->priv->queue);
		audioscrobbler->priv->queue = audioscrobbler->priv->submission;
		audioscrobbler->priv->submission = g_queue_new ();;
		rb_audioscrobbler_save_queue (audioscrobbler);

		rb_audioscrobbler_print_queue (audioscrobbler, FALSE);

		if (audioscrobbler->priv->failures >= 3) {
			rb_debug ("Queue submission has failed %d times; caching tracks locally",
				  audioscrobbler->priv->failures);
			g_free (audioscrobbler->priv->status_msg);

			audioscrobbler->priv->handshake = FALSE;
			audioscrobbler->priv->status = GIVEN_UP;
			audioscrobbler->priv->status_msg = NULL;
		} else {
			rb_debug ("Queue submission failed %d times", audioscrobbler->priv->failures);
		}
	}

	rb_audioscrobbler_preferences_sync (audioscrobbler);
	g_idle_add ((GSourceFunc) idle_unref_cb, audioscrobbler);
}

/* Configuration functions: */
static void
rb_audioscrobbler_import_settings (RBAudioscrobbler *audioscrobbler)
{
	/* import gconf settings. */
	g_free (audioscrobbler->priv->username);
	g_free (audioscrobbler->priv->password);
	audioscrobbler->priv->username = eel_gconf_get_string (CONF_AUDIOSCROBBLER_USERNAME);
	audioscrobbler->priv->password = eel_gconf_get_string (CONF_AUDIOSCROBBLER_PASSWORD);

	rb_audioscrobbler_add_timeout (audioscrobbler);
	audioscrobbler->priv->status = HANDSHAKING;

	audioscrobbler->priv->submit_time = g_strdup (_("Never"));
}

static void
rb_audioscrobbler_preferences_sync (RBAudioscrobbler *audioscrobbler)
{
	const char *status;
	char *free_this = NULL;
	char *v;

	if (audioscrobbler->priv->config_widget == NULL)
		return;

	rb_debug ("Syncing data with preferences window");
	v = audioscrobbler->priv->username;
	gtk_entry_set_text (GTK_ENTRY (audioscrobbler->priv->username_entry),
			    v ? v : "");
	v = audioscrobbler->priv->password;
	gtk_entry_set_text (GTK_ENTRY (audioscrobbler->priv->password_entry),
			    v ? v : "");

	switch (audioscrobbler->priv->status) {
	case STATUS_OK:
		status = _("OK");
		break;
	case HANDSHAKING:
		status = _("Logging in");
		break;
	case REQUEST_FAILED:
		status = _("Request failed");
		break;
	case BAD_USERNAME:
		status = _("Incorrect username");
		break;
	case BAD_PASSWORD:
		status = _("Incorrect password");
		break;
	case HANDSHAKE_FAILED:
		status = _("Handshake failed");
		break;
	case CLIENT_UPDATE_REQUIRED:
		status = _("Client update required");
		break;
	case SUBMIT_FAILED:
		status = _("Track submission failed");
		break;
	case QUEUE_TOO_LONG:
		status = _("Queue is too long");
		break;
	case GIVEN_UP:
		status = _("Track submission failed too many times");
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	if (audioscrobbler->priv->status_msg && audioscrobbler->priv->status_msg[0] != '\0') {
		free_this = g_strdup_printf ("%s: %s", status, audioscrobbler->priv->status_msg);
		status = free_this;
	}

	gtk_label_set_text (GTK_LABEL (audioscrobbler->priv->status_label),
			    status);
	g_free (free_this);

	free_this = g_strdup_printf ("%u", audioscrobbler->priv->submit_count);
	gtk_label_set_text (GTK_LABEL (audioscrobbler->priv->submit_count_label), free_this);
	g_free (free_this);

	free_this = g_strdup_printf ("%u", audioscrobbler->priv->queue_count);
	gtk_label_set_text (GTK_LABEL (audioscrobbler->priv->queue_count_label), free_this);
	g_free (free_this);

	gtk_label_set_text (GTK_LABEL (audioscrobbler->priv->submit_time_label),
			    audioscrobbler->priv->submit_time);
}

GtkWidget *
rb_audioscrobbler_get_config_widget (RBAudioscrobbler *audioscrobbler,
				     RBPlugin *plugin)
{
	GladeXML *xml;
	char *gladefile;

	if (audioscrobbler->priv->config_widget) {
		return audioscrobbler->priv->config_widget;
	}

	gladefile = rb_plugin_find_file (plugin, "audioscrobbler-prefs.glade");
	g_assert (gladefile != NULL);
	xml = rb_glade_xml_new (gladefile, "audioscrobbler_vbox", audioscrobbler);
	g_free (gladefile);

	audioscrobbler->priv->config_widget = glade_xml_get_widget (xml, "audioscrobbler_vbox");
	audioscrobbler->priv->username_entry = glade_xml_get_widget (xml, "username_entry");
	audioscrobbler->priv->username_label = glade_xml_get_widget (xml, "username_label");
	audioscrobbler->priv->password_entry = glade_xml_get_widget (xml, "password_entry");
	audioscrobbler->priv->password_label = glade_xml_get_widget (xml, "password_label");
	audioscrobbler->priv->status_label = glade_xml_get_widget (xml, "status_label");
	audioscrobbler->priv->queue_count_label = glade_xml_get_widget (xml, "queue_count_label");
	audioscrobbler->priv->submit_count_label = glade_xml_get_widget (xml, "submit_count_label");
	audioscrobbler->priv->submit_time_label = glade_xml_get_widget (xml, "submit_time_label");

	rb_glade_boldify_label (xml, "audioscrobbler_label");

	g_object_unref (G_OBJECT (xml));

	rb_audioscrobbler_preferences_sync (audioscrobbler);

	return audioscrobbler->priv->config_widget;
}


/* Callback functions: */

static void
rb_audioscrobbler_proxy_config_changed_cb (RBProxyConfig *config,
					   RBAudioscrobbler *audioscrobbler)
{
	SoupUri *uri;

	if (audioscrobbler->priv->soup_session) {
		uri = rb_proxy_config_get_libsoup_uri (config);
		g_object_set (G_OBJECT (audioscrobbler->priv->soup_session),
					"proxy-uri", uri,
					NULL);
		if (uri)
			soup_uri_free (uri);
	}
}

static void
rb_audioscrobbler_gconf_changed_cb (GConfClient *client,
				    guint cnxn_id,
				    GConfEntry *entry,
				    RBAudioscrobbler *audioscrobbler)
{
	rb_debug ("GConf key updated: \"%s\"", entry->key);
	if (strcmp (entry->key, CONF_AUDIOSCROBBLER_USERNAME) == 0) {
		const gchar *username;

		g_free (audioscrobbler->priv->username);
		audioscrobbler->priv->username = NULL;

		username = gconf_value_get_string (entry->value);
		if (username != NULL) {
			audioscrobbler->priv->username = g_strdup (username);
		}

		if (audioscrobbler->priv->username_entry) {
			char *v = audioscrobbler->priv->username;
			gtk_entry_set_text (GTK_ENTRY (audioscrobbler->priv->username_entry),
					    v ? v : "");
		}

		audioscrobbler->priv->handshake = FALSE;
	} else if (strcmp (entry->key, CONF_AUDIOSCROBBLER_PASSWORD) == 0) {
		const gchar *password;

		g_free (audioscrobbler->priv->password);
		audioscrobbler->priv->password = NULL;

		password = gconf_value_get_string (entry->value);
		if (password != NULL) {
			audioscrobbler->priv->password = g_strdup (password);
		}

		if (audioscrobbler->priv->password_entry) {
			char *v = audioscrobbler->priv->password;
			gtk_entry_set_text (GTK_ENTRY (audioscrobbler->priv->password_entry),
					    v ? v : "");
		}
	} else {
		rb_debug ("Unhandled GConf key updated: \"%s\"", entry->key);
	}
}

static void
rb_audioscrobbler_song_changed_cb (RBShellPlayer *player,
				   RhythmDBEntry *entry,
				   RBAudioscrobbler *audioscrobbler)
{
	guint time;

	if (audioscrobbler->priv->currently_playing != NULL) {
		audioscrobbler_entry_free (audioscrobbler->priv->currently_playing);
		audioscrobbler->priv->currently_playing = NULL;
	}

	if (entry == NULL) {
		return;
	}

	rb_shell_player_get_playing_time (audioscrobbler->priv->shell_player,
					  &time, NULL);
	audioscrobbler->priv->current_elapsed = (int) time;

	if (rb_audioscrobbler_is_queueable (entry) && (time < 15)) {
		AudioscrobblerEntry *as_entry;
		
		/* even if it's the same song, it's being played again from
		 * the start so we can queue it again.
		 */
		as_entry = rb_audioscrobbler_create_entry (entry);
		audioscrobbler->priv->currently_playing = as_entry;
	}
}


void
rb_audioscrobbler_username_entry_changed_cb (GtkEntry *entry,
					     RBAudioscrobbler *audioscrobbler)
{
	eel_gconf_set_string (CONF_AUDIOSCROBBLER_USERNAME,
			      gtk_entry_get_text (entry));
}

void
rb_audioscrobbler_username_entry_activate_cb (GtkEntry *entry,
					      RBAudioscrobbler *audioscrobbler)
{
	gtk_widget_grab_focus (audioscrobbler->priv->password_entry);
}

void
rb_audioscrobbler_password_entry_changed_cb (GtkEntry *entry,
					     RBAudioscrobbler *audioscrobbler)
{
	eel_gconf_set_string (CONF_AUDIOSCROBBLER_PASSWORD,
			      gtk_entry_get_text (entry));
}

void
rb_audioscrobbler_password_entry_activate_cb (GtkEntry *entry,
					      RBAudioscrobbler *audioscrobbler)
{
	/* ? */
}

/* AudioscrobblerEntry functions: */
static void
audioscrobbler_entry_init (AudioscrobblerEntry *entry)
{
	entry->artist = g_strdup ("");
	entry->album = g_strdup ("");
	entry->title = g_strdup ("");
	entry->length = 0;
	entry->play_time = 0;
	entry->mbid = g_strdup ("");
}

static void
audioscrobbler_entry_free (AudioscrobblerEntry *entry)
{
	g_free (entry->artist);
	g_free (entry->album);
	g_free (entry->title);
	g_free (entry->mbid);

	g_free (entry);
}

static AudioscrobblerEncodedEntry *
audioscrobbler_entry_encode (AudioscrobblerEntry *entry)
{

	AudioscrobblerEncodedEntry *encoded;

	encoded = g_new0 (AudioscrobblerEncodedEntry, 1);
	
	encoded->artist = soup_uri_encode (entry->artist, 
					   EXTRA_URI_ENCODE_CHARS);
	encoded->title = soup_uri_encode (entry->title,
					  EXTRA_URI_ENCODE_CHARS);
	encoded->album = soup_uri_encode (entry->album, 
					  EXTRA_URI_ENCODE_CHARS);
	encoded->mbid = soup_uri_encode (entry->mbid, 
					 EXTRA_URI_ENCODE_CHARS);
	encoded->timestamp = g_new0 (gchar, 30);
	strftime (encoded->timestamp, 30, SCROBBLER_DATE_FORMAT, 
		  gmtime (&entry->play_time));

	encoded->length = entry->length;

	return encoded;
}

static 
void audioscrobbler_encoded_entry_free (AudioscrobblerEncodedEntry *entry)
{
	g_free (entry->artist);
	g_free (entry->album);
	g_free (entry->title);
	g_free (entry->mbid);
	g_free (entry->timestamp);

	g_free (entry);
}


/* Queue functions: */

static AudioscrobblerEntry*
rb_audioscrobbler_load_entry_from_string (const char *string)
{
	AudioscrobblerEntry *entry;
	int i = 0;
	char **breaks;

	entry = g_new0 (AudioscrobblerEntry, 1);
	audioscrobbler_entry_init (entry);

	breaks = g_strsplit (string, "&", 6);

	for (i = 0; breaks[i] != NULL; i++) {
		char **breaks2 = g_strsplit (breaks[i], "=", 2);

		if (breaks2[0] != NULL && breaks2[1] != NULL) {
			if (g_str_has_prefix (breaks2[0], "a")) {
				g_free (entry->artist);
				entry->artist = g_strdup (breaks2[1]);
				soup_uri_decode (entry->artist);
			}
			if (g_str_has_prefix (breaks2[0], "t")) {
				g_free (entry->title);
				entry->title = g_strdup (breaks2[1]);
				soup_uri_decode (entry->title);
			}
			if (g_str_has_prefix (breaks2[0], "b")) {
				g_free (entry->album);
				entry->album = g_strdup (breaks2[1]);
				soup_uri_decode (entry->album);
			}
			if (g_str_has_prefix (breaks2[0], "m")) {
				g_free (entry->mbid);
				entry->mbid = g_strdup (breaks2[1]);
				soup_uri_decode (entry->mbid);
			}
			if (g_str_has_prefix (breaks2[0], "l")) {
				entry->length = atoi (breaks2[1]);
			}
			if (g_str_has_prefix (breaks2[0], "i")) {
				struct tm tm;
				strptime (breaks2[1], SCROBBLER_DATE_FORMAT, 
					  &tm);
				entry->play_time = mktime (&tm);
			}
		}

		g_strfreev (breaks2);
	}

	g_strfreev (breaks);

	if (strcmp (entry->artist, "") == 0 || strcmp (entry->title, "") == 0) {
		audioscrobbler_entry_free (entry);
		entry = NULL;
	}

	return entry;
}

static gboolean
rb_audioscrobbler_load_queue (RBAudioscrobbler *audioscrobbler)
{
	char *pathname, *uri;
	GnomeVFSResult result;
	char *data;
	int size;

	pathname = g_build_filename (rb_dot_dir (), "audioscrobbler.queue", NULL);
	uri = g_filename_to_uri (pathname, NULL, NULL);
	g_free (pathname);
	rb_debug ("Loading Audioscrobbler queue from \"%s\"", uri);

	result = gnome_vfs_read_entire_file (uri, &size, &data);
	g_free (uri);

	/* do stuff */
	if (result == GNOME_VFS_OK) {
		char *start = data, *end;

		/* scan along the file's data, turning each line into a string */
		while (start < (data + size)) {
			AudioscrobblerEntry *entry;

			/* find the end of the line, to terminate the string */
			end = g_utf8_strchr (start, -1, '\n');
			if (end == NULL)
				break;
			*end = 0;

			entry = rb_audioscrobbler_load_entry_from_string (start);
			if (entry) {
				g_queue_push_tail (audioscrobbler->priv->queue,
						   entry);
				audioscrobbler->priv->queue_count++;
			}

			start = end + 1;
		}
	}

	if (result != GNOME_VFS_OK) {
		rb_debug ("Unable to load Audioscrobbler queue from disk: %s",
			  gnome_vfs_result_to_string (result));
	}

	g_free (data);
	return (result == GNOME_VFS_OK);
}

static char *
rb_audioscrobbler_save_entry_to_string (AudioscrobblerEntry *entry)
{
	char *result;
	AudioscrobblerEncodedEntry *encoded;

	encoded = audioscrobbler_entry_encode (entry);
	result = g_strdup_printf ("a=%s&t=%s&b=%s&m=%s&l=%d&i=%s\n",
				  encoded->artist, 
				  encoded->title,
				  encoded->album, 
				  encoded->mbid,
				  encoded->length,
				  encoded->timestamp);
	audioscrobbler_encoded_entry_free (encoded);
	return result;
}

static gboolean
rb_audioscrobbler_save_queue (RBAudioscrobbler *audioscrobbler)
{
	char *pathname;
	GnomeVFSHandle *handle = NULL;
	GnomeVFSResult result;

	if (!audioscrobbler->priv->queue_changed) {
		return TRUE;
	}

	pathname = g_build_filename (rb_dot_dir (), "audioscrobbler.queue", NULL);
	rb_debug ("Saving Audioscrobbler queue to \"%s\"", pathname);

	result = gnome_vfs_create (&handle, pathname, GNOME_VFS_OPEN_WRITE, FALSE, 0600);
	g_free (pathname);

	if (result == GNOME_VFS_OK) {
		GString *s = g_string_new (NULL);
		GList *l;

		for (l = audioscrobbler->priv->queue->head;
		     l != NULL;
		     l = g_list_next (l)) {
			AudioscrobblerEntry *entry;
			char *str;
			entry = (AudioscrobblerEntry *) l->data;
			str = rb_audioscrobbler_save_entry_to_string (entry);
			result = gnome_vfs_write (handle, str, strlen (str), 
						  NULL);
			g_free (str);
			if (result != GNOME_VFS_OK)
				break;
		}
		g_string_free (s, TRUE);
	}

	if (result != GNOME_VFS_OK) {
		rb_debug ("Unable to save Audioscrobbler queue to disk: %s",
			  gnome_vfs_result_to_string (result));
	} else {
		audioscrobbler->priv->queue_changed = FALSE;
	}

	if (handle)
		gnome_vfs_close (handle);
	return (result == GNOME_VFS_OK);
}

static void
rb_audioscrobbler_print_queue (RBAudioscrobbler *audioscrobbler, gboolean submission)
{
	GList *l;
	AudioscrobblerEntry *entry;
	int i = 0;

	if (submission) {
		l = audioscrobbler->priv->submission->head;
		rb_debug ("Audioscrobbler submission (%d entries): ", 
			  g_queue_get_length (audioscrobbler->priv->submission));

	} else {
		l = audioscrobbler->priv->queue->head;
		rb_debug ("Audioscrobbler queue (%d entries): ", 
			  g_queue_get_length (audioscrobbler->priv->queue));
	}

	for (; l != NULL; l = g_list_next (l)) {
		char timestamp[30];
		entry = (AudioscrobblerEntry *) l->data;
		
		rb_debug ("%-3d  artist: %s", ++i, entry->artist);
		rb_debug ("      album: %s", entry->album);
		rb_debug ("      title: %s", entry->title);
		rb_debug ("     length: %d", entry->length);
		strftime (timestamp, 30, SCROBBLER_DATE_FORMAT, 
			  gmtime (&entry->play_time));
		rb_debug ("  timestamp: %s", timestamp);
	}
}

static void
rb_audioscrobbler_free_queue_entries (RBAudioscrobbler *audioscrobbler, GQueue **queue)
{
	g_queue_foreach (*queue, (GFunc) audioscrobbler_entry_free, NULL);
	g_queue_free (*queue);
	*queue = NULL;

	audioscrobbler->priv->queue_changed = TRUE;
}
