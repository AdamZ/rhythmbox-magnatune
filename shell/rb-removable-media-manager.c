/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  arch-tag: Implementation of Rhythmbox removable media manager
 *
 *  Copyright (C) 2005 James Livingston  <doclivingston@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grants permission for non-GPL compatible
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
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs.h>

#include "rb-removable-media-manager.h"
#include "rb-library-source.h"
#include "rb-removable-media-source.h"

#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-stock-icons.h"
#include "rhythmdb.h"
#include "rb-marshal.h"
#include "rb-util.h"

#ifdef ENABLE_TRACK_TRANSFER
#include "rb-encoder.h"
#endif

static void rb_removable_media_manager_class_init (RBRemovableMediaManagerClass *klass);
static void rb_removable_media_manager_init (RBRemovableMediaManager *mgr);
static void rb_removable_media_manager_dispose (GObject *object);
static void rb_removable_media_manager_finalize (GObject *object);
static void rb_removable_media_manager_set_property (GObject *object,
					      guint prop_id,
					      const GValue *value,
					      GParamSpec *pspec);
static void rb_removable_media_manager_get_property (GObject *object,
					      guint prop_id,
					      GValue *value,
					      GParamSpec *pspec);

static void rb_removable_media_manager_cmd_scan_media (GtkAction *action,
						       RBRemovableMediaManager *manager);
static void rb_removable_media_manager_cmd_eject_medium (GtkAction *action,
					       RBRemovableMediaManager *mgr);
static void rb_removable_media_manager_set_uimanager (RBRemovableMediaManager *mgr,
					     GtkUIManager *uimanager);

static void rb_removable_media_manager_append_media_source (RBRemovableMediaManager *mgr, RBRemovableMediaSource *source);

static void rb_removable_media_manager_mount_volume (RBRemovableMediaManager *mgr,
				GnomeVFSVolume *volume);
static void rb_removable_media_manager_unmount_volume (RBRemovableMediaManager *mgr,
				GnomeVFSVolume *volume);

static void  rb_removable_media_manager_volume_mounted_cb (GnomeVFSVolumeMonitor *monitor,
				GnomeVFSVolume *volume,
				gpointer data);
static void  rb_removable_media_manager_volume_unmounted_cb (GnomeVFSVolumeMonitor *monitor,
				GnomeVFSVolume *volume,
				gpointer data);
static gboolean rb_removable_media_manager_load_media (RBRemovableMediaManager *manager);

#ifdef ENABLE_TRACK_TRANSFER
static void do_transfer (RBRemovableMediaManager *manager);
#endif
static void rb_removable_media_manager_cmd_copy_tracks (GtkAction *action,
							RBRemovableMediaManager *mgr);

typedef struct
{
	RBShell *shell;
	gboolean disposed;

	RBSource *selected_source;

	GtkActionGroup *actiongroup;
	GtkUIManager *uimanager;

	GList *sources;
	GHashTable *volume_mapping;
	GList *cur_volume_list;
	gboolean scanned;

	GAsyncQueue *transfer_queue;
	gboolean transfer_running;
	gint transfer_total;
	gint transfer_done;
	double transfer_fraction;
} RBRemovableMediaManagerPrivate;

G_DEFINE_TYPE (RBRemovableMediaManager, rb_removable_media_manager, G_TYPE_OBJECT)
#define REMOVABLE_MEDIA_MANAGER_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_REMOVABLE_MEDIA_MANAGER, RBRemovableMediaManagerPrivate))

enum
{
	PROP_0,
	PROP_SHELL,
	PROP_SOURCE,
	PROP_SCANNED
};

enum
{
	MEDIUM_ADDED,
	TRANSFER_PROGRESS,
	CREATE_SOURCE,
	LAST_SIGNAL
};

static guint rb_removable_media_manager_signals[LAST_SIGNAL] = { 0 };

static GtkActionEntry rb_removable_media_manager_actions [] =
{
	{ "RemovableSourceEject", GNOME_MEDIA_EJECT, N_("_Eject"), NULL,
	  N_("Eject this medium"),
	  G_CALLBACK (rb_removable_media_manager_cmd_eject_medium) },
	{ "RemovableSourceCopyAllTracks", GTK_STOCK_CDROM, N_("_Copy to library"), NULL,
	  N_("Copy all tracks to the library"),
	  G_CALLBACK (rb_removable_media_manager_cmd_copy_tracks) },
	{ "MusicScanMedia", NULL, N_("_Scan Removable Media"), NULL,
	  N_("Scan for new Removable Media"),
	  G_CALLBACK (rb_removable_media_manager_cmd_scan_media) },
};
static guint rb_removable_media_manager_n_actions = G_N_ELEMENTS (rb_removable_media_manager_actions);

static void
rb_removable_media_manager_class_init (RBRemovableMediaManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_removable_media_manager_dispose;
	object_class->finalize = rb_removable_media_manager_finalize;
	object_class->set_property = rb_removable_media_manager_set_property;
	object_class->get_property = rb_removable_media_manager_get_property;

	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "RBSource",
							      "RBSource object",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							      "RBShell",
							      "RBShell object",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_SCANNED,
					 g_param_spec_boolean ("scanned",
						 	       "scanned",
							       "Whether a scan has been performed",
							       FALSE,
							       G_PARAM_READABLE));

	rb_removable_media_manager_signals[MEDIUM_ADDED] =
		g_signal_new ("medium_added",
			      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemovableMediaManagerClass, medium_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);

	rb_removable_media_manager_signals[TRANSFER_PROGRESS] =
		g_signal_new ("transfer-progress",
			      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemovableMediaManagerClass, transfer_progress),
			      NULL, NULL,
			      rb_marshal_VOID__INT_INT_DOUBLE,
			      G_TYPE_NONE,
			      3, G_TYPE_INT, G_TYPE_INT, G_TYPE_DOUBLE);

	rb_removable_media_manager_signals[CREATE_SOURCE] =
		g_signal_new ("create-source",
			      RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRemovableMediaManagerClass, create_source),
			      rb_signal_accumulator_object_handled, NULL,
			      rb_marshal_OBJECT__OBJECT,
			      RB_TYPE_SOURCE,
			      1, GNOME_VFS_TYPE_VOLUME);

	g_type_class_add_private (klass, sizeof (RBRemovableMediaManagerPrivate));
}

static void
rb_removable_media_manager_init (RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);

	priv->volume_mapping = g_hash_table_new (NULL, NULL);
	priv->transfer_queue = g_async_queue_new ();

	g_idle_add ((GSourceFunc)rb_removable_media_manager_load_media, mgr);
}

static void
rb_removable_media_manager_dispose (GObject *object)
{
	RBRemovableMediaManager *mgr = RB_REMOVABLE_MEDIA_MANAGER (object);
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);

	if (!priv->disposed)
	{
		GnomeVFSVolumeMonitor *monitor = gnome_vfs_get_volume_monitor ();

		g_signal_handlers_disconnect_by_func (G_OBJECT (monitor),
						      G_CALLBACK (rb_removable_media_manager_volume_mounted_cb),
						      mgr);
		g_signal_handlers_disconnect_by_func (G_OBJECT (monitor),
						      G_CALLBACK (rb_removable_media_manager_volume_unmounted_cb),
						      mgr);
	}

	if (priv->sources) {
		g_list_free (priv->sources);
		priv->sources = NULL;
	}

	priv->disposed = TRUE;

	G_OBJECT_CLASS (rb_removable_media_manager_parent_class)->dispose (object);
}

static void
rb_removable_media_manager_finalize (GObject *object)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (object);

	g_hash_table_destroy (priv->volume_mapping);
	g_async_queue_unref (priv->transfer_queue);

	G_OBJECT_CLASS (rb_removable_media_manager_parent_class)->finalize (object);
}

static void
rb_removable_media_manager_set_property (GObject *object,
				  guint prop_id,
				  const GValue *value,
				  GParamSpec *pspec)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
	{
		priv->selected_source = g_value_get_object (value);
		break;
	}
	case PROP_SHELL:
	{
		GtkUIManager *uimanager;

		priv->shell = g_value_get_object (value);
		g_object_get (priv->shell,
			      "ui-manager", &uimanager,
			      NULL);
		rb_removable_media_manager_set_uimanager (RB_REMOVABLE_MEDIA_MANAGER (object), uimanager);
		g_object_unref (uimanager);
		break;
	}
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_removable_media_manager_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_SOURCE:
		g_value_set_object (value, priv->selected_source);
		break;
	case PROP_SHELL:
		g_value_set_object (value, priv->shell);
		break;
	case PROP_SCANNED:
		g_value_set_boolean (value, priv->scanned);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBRemovableMediaManager *
rb_removable_media_manager_new (RBShell *shell)
{
	return g_object_new (RB_TYPE_REMOVABLE_MEDIA_MANAGER,
			     "shell", shell,
			     NULL);
}

static gboolean
rb_removable_media_manager_load_media (RBRemovableMediaManager *manager)
{
	GnomeVFSVolumeMonitor *monitor = gnome_vfs_get_volume_monitor ();

	GDK_THREADS_ENTER ();
	
	/*
	 * Monitor new (un)mounted file systems to look for new media
	 *
	 * both pre-unmount and unmounted callbacks are registered because it is
	 * better to do it before the unmount, but sometimes we don't get those
	 * (e.g. someone pressing the eject button on a cd drive). If we get the
	 * pre-unmount signal, the corrosponding unmounted signal is ignored
	 */
	g_signal_connect (G_OBJECT (monitor), "volume-mounted",
			  G_CALLBACK (rb_removable_media_manager_volume_mounted_cb),
			  manager);
	g_signal_connect (G_OBJECT (monitor), "volume-pre-unmount",
			  G_CALLBACK (rb_removable_media_manager_volume_unmounted_cb),
			  manager);
	g_signal_connect (G_OBJECT (monitor), "volume-unmounted",
			  G_CALLBACK (rb_removable_media_manager_volume_unmounted_cb),
			  manager);

	GDK_THREADS_LEAVE ();
	return FALSE;
}

static void
rb_removable_media_manager_volume_mounted_cb (GnomeVFSVolumeMonitor *monitor,
			   GnomeVFSVolume *volume,
			   gpointer data)
{
	RBRemovableMediaManager *mgr = RB_REMOVABLE_MEDIA_MANAGER (data);

	rb_removable_media_manager_mount_volume (mgr, volume);
}

static gboolean
remove_volume_by_source (GnomeVFSVolume *volume, RBSource *source,
			 RBSource *ref_source)
{
	return (ref_source == source);
}

static void
rb_removable_media_manager_source_deleted_cb (RBSource *source, RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);

	rb_debug ("removing source %p", source);
	g_hash_table_foreach_remove (priv->volume_mapping,
				     (GHRFunc)remove_volume_by_source,
				     source);
	priv->sources = g_list_remove (priv->sources, source);
}

static void
rb_removable_media_manager_volume_unmounted_cb (GnomeVFSVolumeMonitor *monitor,
			     GnomeVFSVolume *volume,
			     gpointer data)
{
	RBRemovableMediaManager *mgr = RB_REMOVABLE_MEDIA_MANAGER (data);

	g_assert (volume != NULL);
	rb_removable_media_manager_unmount_volume (mgr, volume);
}

static void
rb_removable_media_manager_mount_volume (RBRemovableMediaManager *mgr, GnomeVFSVolume *volume)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);
	RBRemovableMediaSource *source = NULL;
	char *fs_type, *device_path, *display_name, *hal_udi, *icon_name;
	GnomeVFSDeviceType device_type;

	g_assert (volume != NULL);

	if (g_hash_table_lookup (priv->volume_mapping, volume) != NULL)
		return;

	if (!gnome_vfs_volume_is_mounted (volume))
		return;

	/* ignore network volumes */
	device_type = gnome_vfs_volume_get_device_type (volume);
	if (device_type == GNOME_VFS_DEVICE_TYPE_NFS ||
	    device_type == GNOME_VFS_DEVICE_TYPE_AUTOFS ||
	    device_type == GNOME_VFS_DEVICE_TYPE_SMB ||
	    device_type == GNOME_VFS_DEVICE_TYPE_NETWORK)
		return;

	fs_type = gnome_vfs_volume_get_filesystem_type (volume);
	device_path = gnome_vfs_volume_get_device_path (volume);
	display_name = gnome_vfs_volume_get_display_name (volume);
	hal_udi = gnome_vfs_volume_get_hal_udi (volume);
	icon_name = gnome_vfs_volume_get_icon (volume);
	rb_debug ("detecting new media - device type=%d", device_type);
	rb_debug ("detecting new media - volume type=%d", gnome_vfs_volume_get_volume_type (volume));
	rb_debug ("detecting new media - fs type=%s", fs_type);
	rb_debug ("detecting new media - device path=%s", device_path);
	rb_debug ("detecting new media - display name=%s", display_name);
	rb_debug ("detecting new media - hal udi=%s", hal_udi);
	rb_debug ("detecting new media - icon=%s", icon_name);

	/* rb_xxx_source_new first checks if the 'volume' parameter corresponds
	 * to a medium of type 'xxx', and returns NULL if it doesn't.
	 * When volume is of the appropriate type, it creates a new source
	 * to handle this volume
	 */

	g_signal_emit (G_OBJECT (mgr), rb_removable_media_manager_signals[CREATE_SOURCE], 0,
		       volume, &source);

	if (source) {
		g_hash_table_insert (priv->volume_mapping, volume, source);
		rb_removable_media_manager_append_media_source (mgr, source);
	} else {
		rb_debug ("Unhandled media");
	}

	g_free (fs_type);
	g_free (device_path);
	g_free (display_name);
	g_free (hal_udi);
	g_free (icon_name);
}

static void
rb_removable_media_manager_unmount_volume (RBRemovableMediaManager *mgr, GnomeVFSVolume *volume)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);
	RBRemovableMediaSource *source;

	g_assert (volume != NULL);

	rb_debug ("media removed");
	source = g_hash_table_lookup (priv->volume_mapping, volume);
	if (source) {
		rb_source_delete_thyself (RB_SOURCE (source));
	}
}

static void
rb_removable_media_manager_append_media_source (RBRemovableMediaManager *mgr, RBRemovableMediaSource *source)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);

	priv->sources = g_list_prepend (priv->sources, source);
	g_signal_connect_object (G_OBJECT (source), "deleted",
				 G_CALLBACK (rb_removable_media_manager_source_deleted_cb), mgr, 0);

	g_signal_emit (G_OBJECT (mgr), rb_removable_media_manager_signals[MEDIUM_ADDED], 0,
		       source);
}

static void
rb_removable_media_manager_set_uimanager (RBRemovableMediaManager *mgr,
					  GtkUIManager *uimanager)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);

	if (priv->uimanager != NULL) {
		if (priv->actiongroup != NULL) {
			gtk_ui_manager_remove_action_group (priv->uimanager,
							    priv->actiongroup);
		}
		g_object_unref (G_OBJECT (priv->uimanager));
		priv->uimanager = NULL;
	}

	priv->uimanager = uimanager;

	if (priv->uimanager != NULL) {
		g_object_ref (priv->uimanager);
	}

	if (priv->actiongroup == NULL) {
		priv->actiongroup = gtk_action_group_new ("RemovableMediaActions");
		gtk_action_group_set_translation_domain (priv->actiongroup,
							 GETTEXT_PACKAGE);
		gtk_action_group_add_actions (priv->actiongroup,
					      rb_removable_media_manager_actions,
					      rb_removable_media_manager_n_actions,
					      mgr);
	}

#ifndef ENABLE_TRACK_TRANSFER
	{
		GtkAction *action;

		action = gtk_action_group_get_action (priv->actiongroup, "RemovableSourceCopyAllTracks");
		gtk_action_set_visible (action, FALSE);
	}
#endif

	gtk_ui_manager_insert_action_group (priv->uimanager,
					    priv->actiongroup,
					    0);
}

static void
rb_removable_media_manager_eject_medium_cb (gboolean succeeded,
					   const char *error,
					   const char *detailed_error,
					   gpointer *data)
{
	if (succeeded)
		return;

	rb_error_dialog (NULL, error, "%s", detailed_error);
}

static void
rb_removable_media_manager_cmd_eject_medium (GtkAction *action, RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);
	RBRemovableMediaSource *source = RB_REMOVABLE_MEDIA_SOURCE (priv->selected_source);
	GnomeVFSVolume *volume;

	g_object_get (source, "volume", &volume, NULL);
	rb_removable_media_manager_unmount_volume (mgr, volume);
	gnome_vfs_volume_eject (volume, (GnomeVFSVolumeOpCallback)rb_removable_media_manager_eject_medium_cb, mgr);
	gnome_vfs_volume_unref (volume);
}

static void
rb_removable_media_manager_cmd_scan_media (GtkAction *action, RBRemovableMediaManager *manager)
{
	rb_removable_media_manager_scan (manager);
}

struct VolumeCheckData
{
	RBRemovableMediaManager *manager;
	GList *volume_list;
	GList *volumes_to_remove;
};

static void
rb_removable_media_manager_check_volume (GnomeVFSVolume *volume,
					 RBRemovableMediaSource *source,
					 struct VolumeCheckData *check_data)
{
	/* if the volume is no longer present, queue it for removal */
	if (g_list_find (check_data->volume_list, volume) == NULL)
		check_data->volumes_to_remove = g_list_prepend (check_data->volumes_to_remove, volume);
}

static void
rb_removable_media_manager_unmount_volume_swap (GnomeVFSVolume *volume, RBRemovableMediaManager *manager)
{
	rb_removable_media_manager_unmount_volume (manager, volume);
}

void
rb_removable_media_manager_scan (RBRemovableMediaManager *manager)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (manager);
	GnomeVFSVolumeMonitor *monitor = gnome_vfs_get_volume_monitor ();
	GList *list, *it;
	GnomeVFSVolume *volume;
	struct VolumeCheckData check_data;

	priv->scanned = TRUE;

	list = gnome_vfs_volume_monitor_get_mounted_volumes (monitor);

	/* see if any removable media has gone */
	check_data.volume_list = list;
	check_data.manager = manager;
	check_data.volumes_to_remove = NULL;
	g_hash_table_foreach (priv->volume_mapping,
			      (GHFunc) rb_removable_media_manager_check_volume,
			      &check_data);
	g_list_foreach (check_data.volumes_to_remove,
			(GFunc) rb_removable_media_manager_unmount_volume_swap,
			manager);
	g_list_free (check_data.volumes_to_remove);

	/* look for new volume media */
	for (it = list; it != NULL; it = g_list_next (it)) {
		volume = GNOME_VFS_VOLUME (it->data);
		rb_removable_media_manager_mount_volume (manager, volume);
		gnome_vfs_volume_unref (volume);
	}
	g_list_free (list);
}

#ifdef ENABLE_TRACK_TRANSFER
/* Track transfer */

typedef struct {
	RBRemovableMediaManager *manager;
	RhythmDBEntry *entry;
	char *dest;
	GList *mime_types;
	gboolean failed;
	RBTranferCompleteCallback callback;
	gpointer userdata;
} TransferData;

static void
emit_progress (RBRemovableMediaManager *mgr)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);

	g_signal_emit (G_OBJECT (mgr), rb_removable_media_manager_signals[TRANSFER_PROGRESS], 0,
		       priv->transfer_done,
		       priv->transfer_total,
		       priv->transfer_fraction);
}

static void
error_cb (RBEncoder *encoder, GError *error, TransferData *data)
{
	rb_debug ("Error transferring track to %s: %s", data->dest, error->message);
	rb_error_dialog (NULL, _("Error transferring track"), "%s", error->message);

	data->failed = TRUE;
	rb_encoder_cancel (encoder);
}

static void
progress_cb (RBEncoder *encoder, double fraction, TransferData *data)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (data->manager);

	rb_debug ("transfer progress %f", (float)fraction);
	priv->transfer_fraction = fraction;
	emit_progress (data->manager);
}

static void
completed_cb (RBEncoder *encoder, TransferData *data)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (data->manager);

	rb_debug ("completed transferring track to %s", data->dest);
	if (!data->failed)
		(data->callback) (data->entry, data->dest, data->userdata);

	priv->transfer_running = FALSE;
	priv->transfer_done++;
	priv->transfer_fraction = 0.0;
	do_transfer (data->manager);

	g_object_unref (G_OBJECT (encoder));
	g_free (data->dest);
	rb_list_deep_free (data->mime_types);
	g_free (data);
}

static void
do_transfer (RBRemovableMediaManager *manager)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (manager);
	TransferData *data;
	RBEncoder *encoder;

	g_assert (rb_is_main_thread ());

	emit_progress (manager);

	if (priv->transfer_running)
		return;

	data = g_async_queue_try_pop (priv->transfer_queue);
	if (data == NULL) {
		priv->transfer_total = 0;
		priv->transfer_done = 0;
		emit_progress (manager);
		return;
	}

	priv->transfer_running = TRUE;
	priv->transfer_fraction = 0.0;

	encoder = rb_encoder_new ();
	g_signal_connect (G_OBJECT (encoder),
			  "error", G_CALLBACK (error_cb),
			  data);
	g_signal_connect (G_OBJECT (encoder),
			  "progress", G_CALLBACK (progress_cb),
			  data);
	g_signal_connect (G_OBJECT (encoder),
			  "completed", G_CALLBACK (completed_cb),
			  data);
	rb_encoder_encode (encoder, data->entry, data->dest, data->mime_types);
}

void
rb_removable_media_manager_queue_transfer (RBRemovableMediaManager *manager,
					  RhythmDBEntry *entry,
					  const char *dest,
					  GList *mime_types,
					  RBTranferCompleteCallback callback,
					  gpointer userdata)
{
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (manager);
	TransferData *data;

	g_assert (rb_is_main_thread ());

	data = g_new0 (TransferData, 1);
	data->manager = manager;
	data->entry = entry;
	data->dest = g_strdup (dest);
	data->mime_types = rb_string_list_copy (mime_types);
	data->callback = callback;
	data->userdata = userdata;

	g_async_queue_push (priv->transfer_queue, data);
	priv->transfer_total++;
	do_transfer (manager);
}

static gboolean
copy_entry (RhythmDBQueryModel *model,
	    GtkTreePath *path,
	    GtkTreeIter *iter,
	    GList **list)
{
	GList *l;
	l = g_list_append (*list, rhythmdb_query_model_iter_to_entry (model, iter));
	*list = l;
	return FALSE;
}
#endif

static void
rb_removable_media_manager_cmd_copy_tracks (GtkAction *action, RBRemovableMediaManager *mgr)
{
#ifdef ENABLE_TRACK_TRANSFER
	RBRemovableMediaManagerPrivate *priv = REMOVABLE_MEDIA_MANAGER_GET_PRIVATE (mgr);
	RBRemovableMediaSource *source;
	RBLibrarySource *library;
	RhythmDBQueryModel *model;
	GList *list = NULL;

	source = RB_REMOVABLE_MEDIA_SOURCE (priv->selected_source);
	g_object_get (source, "query-model", &model, NULL);
	g_object_get (priv->shell, "library-source", &library, NULL);

	gtk_tree_model_foreach (GTK_TREE_MODEL (model), (GtkTreeModelForeachFunc)copy_entry, &list);
	rb_source_paste (RB_SOURCE (library), list);
	g_list_free (list);

	g_object_unref (model);
	g_object_unref (library);
#endif
}
