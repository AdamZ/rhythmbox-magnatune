/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Implmentation of DAAP (iTunes Music Sharing) sharing
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
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

#include <string.h>

#include <glib/gi18n.h>
#include <glib/gprintf.h>

#include "rb-daap-sharing.h"
#include "rb-daap-share.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-playlist-manager.h"
#include "eel-gconf-extensions.h"
#include "rb-preferences.h"

static RBDAAPShare *share = NULL;
static guint enable_sharing_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;
static guint require_password_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;
static guint share_name_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;
static guint share_password_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;

static void 
create_share (RBShell *shell)
{
	RhythmDB *db;
	RBPlaylistManager *playlist_manager;
	char *name;
	char *password;
	gboolean require_password;

	g_assert (share == NULL);
	rb_debug ("initialize daap sharing\n");

	name = eel_gconf_get_string (CONF_DAAP_SHARE_NAME);

	if (name == NULL || *name == '\0') {
		const gchar *real_name;

		g_free (name);

		real_name = g_get_real_name ();
		if (strcmp (real_name, "Unknown") == 0) {
			real_name = g_get_user_name ();
		}

		name = g_strdup_printf (_("%s's Music"), real_name);
		eel_gconf_set_string (CONF_DAAP_SHARE_NAME, name);
	}

	g_object_get (G_OBJECT (shell), "db", &db, "playlist-manager", &playlist_manager, NULL);

	require_password = eel_gconf_get_boolean (CONF_DAAP_REQUIRE_PASSWORD);
	if (require_password) {
		password = eel_gconf_get_string (CONF_DAAP_SHARE_PASSWORD);
	} else {
		password = NULL;
	}

	share = rb_daap_share_new (name, password, db, playlist_manager);

	g_free (name);
	g_free (password);
}

static void 
enable_sharing_changed_cb (GConfClient *client,
			   guint cnxn_id,
		     	   GConfEntry *entry,
		  	   RBShell *shell)
{
	gboolean enabled;

	enabled = eel_gconf_get_boolean (CONF_DAAP_ENABLE_SHARING);

	if (enabled) {
		if (share == NULL) {
			create_share (shell);
		}
	} else {
		rb_debug ("shutdown daap sharing");

		if (share) {
			g_object_unref (share);
		}
		share = NULL;
	}
}

static void 
require_password_changed_cb (GConfClient *client,
			     guint cnxn_id,
			     GConfEntry *entry,
			     RBShell *shell)
{
	gboolean required;
	char    *password;

	if (share == NULL) {
		return;
	}

	required = eel_gconf_get_boolean (CONF_DAAP_REQUIRE_PASSWORD);

	if (required) {
		password = eel_gconf_get_string (CONF_DAAP_SHARE_PASSWORD);
	} else {
		password = NULL;
	}

	g_object_set (G_OBJECT (share), "password", password, NULL);
	g_free (password);
}

static void 
share_name_changed_cb (GConfClient *client, 
		       guint cnxn_id, 
		       GConfEntry *entry, 
		       RBShell *shell)
{
	char *name;

	if (share == NULL) {
		return;
	}

	name = eel_gconf_get_string (CONF_DAAP_SHARE_NAME);
	g_object_set (G_OBJECT (share), "name", name, NULL);
	g_free (name);
}

static void 
share_password_changed_cb (GConfClient *client, 
			   guint cnxn_id, 
			   GConfEntry *entry, 
			   RBShell *shell)
{
	gboolean require_password;
	char    *password;

	if (share == NULL) {
		return;
	}

	require_password = eel_gconf_get_boolean (CONF_DAAP_REQUIRE_PASSWORD);

	/* Don't do anything unless we require a password */
	if (! require_password) {
		return;
	}

	password = eel_gconf_get_string (CONF_DAAP_SHARE_PASSWORD);
	g_object_set (G_OBJECT (share), "password", password, NULL);
	g_free (password);
}


void 
rb_daap_sharing_init (RBShell *shell)
{
	g_object_ref (shell);

	if (eel_gconf_get_boolean (CONF_DAAP_ENABLE_SHARING)) {
		create_share (shell);
	}

	enable_sharing_notify_id =
		eel_gconf_notification_add (CONF_DAAP_ENABLE_SHARING,
					    (GConfClientNotifyFunc) enable_sharing_changed_cb,
					    shell);
	require_password_notify_id =
		eel_gconf_notification_add (CONF_DAAP_REQUIRE_PASSWORD,
					    (GConfClientNotifyFunc) require_password_changed_cb,
					    shell);
	share_name_notify_id =
		eel_gconf_notification_add (CONF_DAAP_SHARE_NAME,
					    (GConfClientNotifyFunc) share_name_changed_cb,
					    shell);
	share_password_notify_id =
		eel_gconf_notification_add (CONF_DAAP_SHARE_PASSWORD,
					    (GConfClientNotifyFunc) share_password_changed_cb,
					    shell);
}

void 
rb_daap_sharing_shutdown (RBShell *shell)
{
	if (share) {
		rb_debug ("shutdown daap sharing");

		g_object_unref (share);
		share = NULL;
	}

	if (enable_sharing_notify_id != EEL_GCONF_UNDEFINED_CONNECTION) {
		eel_gconf_notification_remove (enable_sharing_notify_id);
		enable_sharing_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;
	}
	if (require_password_notify_id != EEL_GCONF_UNDEFINED_CONNECTION) {
		eel_gconf_notification_remove (require_password_notify_id);
		require_password_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;
	}
	if (share_name_notify_id != EEL_GCONF_UNDEFINED_CONNECTION) {
		eel_gconf_notification_remove (share_name_notify_id);
		share_name_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;
	}
	if (share_password_notify_id != EEL_GCONF_UNDEFINED_CONNECTION) {
		eel_gconf_notification_remove (share_password_notify_id);
		share_password_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;
	}

	g_object_unref (shell);
}

