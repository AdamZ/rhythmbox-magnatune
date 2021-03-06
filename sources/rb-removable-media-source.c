/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2004 Christophe Fergeau  <teuf@gnome.org>
 *  Copyright (C) 2005 James Livingston  <doclivingston@gmail.com>
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
 * SECTION:rb-removable-media-source
 * @short_description: Base class for sources representing removable media
 *
 * This class provides support for transferring (and transcoding) entries to
 * the device using drag and drop or cut and paste.  The implementation must
 * at minimum provide methods for returning a list of supported media types,
 * and for constructing destination URIs for transfers.
 */

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rhythmdb.h"
#include "eel-gconf-extensions.h"
#include "rb-removable-media-source.h"
#include "rb-removable-media-manager.h"
#include "rb-encoder.h"
#include "rb-stock-icons.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-util.h"
#include "rb-file-helpers.h"

/* arbitrary length limit for file extensions */
#define EXTENSION_LENGTH_LIMIT	8

static void rb_removable_media_source_constructed (GObject *object);
static void rb_removable_media_source_dispose (GObject *object);

static void rb_removable_media_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_removable_media_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);

static void impl_delete_thyself (RBSource *source);
static void impl_paste (RBSource *source, GList *entries);
static gboolean impl_receive_drag (RBSource *asource, GtkSelectionData *data);
static gboolean impl_should_paste (RBRemovableMediaSource *source,
				   RhythmDBEntry *entry);
static guint impl_want_uri (RBSource *source, const char *uri);
static gboolean impl_uri_is_source (RBSource *source, const char *uri);
static char *impl_get_delete_action (RBSource *source);

typedef struct
{
	GVolume *volume;
	GMount *mount;
} RBRemovableMediaSourcePrivate;

G_DEFINE_TYPE (RBRemovableMediaSource, rb_removable_media_source, RB_TYPE_BROWSER_SOURCE)
#define REMOVABLE_MEDIA_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_REMOVABLE_MEDIA_SOURCE, RBRemovableMediaSourcePrivate))

enum
{
	PROP_0,
	PROP_VOLUME,
	PROP_MOUNT,
};

static void
rb_removable_media_source_class_init (RBRemovableMediaSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);

	object_class->constructed = rb_removable_media_source_constructed;
	object_class->dispose = rb_removable_media_source_dispose;
	object_class->set_property = rb_removable_media_source_set_property;
	object_class->get_property = rb_removable_media_source_get_property;

	source_class->impl_delete_thyself = impl_delete_thyself;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_paste = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;
  	source_class->impl_paste = impl_paste;
  	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_delete = NULL;
	source_class->impl_get_config_widget = NULL;
	source_class->impl_show_popup = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_want_uri = impl_want_uri;
	source_class->impl_uri_is_source = impl_uri_is_source;
	source_class->impl_get_delete_action = impl_get_delete_action;

	browser_source_class->impl_get_paned_key = NULL;
	browser_source_class->impl_has_drop_support = (RBBrowserSourceFeatureFunc) rb_false_function;

	klass->impl_should_paste = impl_should_paste;

	/**
	 * RBRemovableMediaSource:volume
	 *
	 * The #GVolume object that the source represents (optional)
	 */
	g_object_class_install_property (object_class,
					 PROP_VOLUME,
					 g_param_spec_object ("volume",
							      "Volume",
							      "GIO Volume",
							      G_TYPE_VOLUME,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBRemovableMediaSource:mount
	 *
	 * The #GMount object that the source represents
	 */
	g_object_class_install_property (object_class,
					 PROP_MOUNT,
					 g_param_spec_object ("mount",
							      "Mount",
							      "GIO Mount",
							      G_TYPE_MOUNT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBRemovableMediaSourcePrivate));
}

static void
rb_removable_media_source_init (RBRemovableMediaSource *self)
{
}

static void
rb_removable_media_source_constructed (GObject *object)
{
	GMount *mount;
	GIcon *icon = NULL;
	char *display_name;
	GdkPixbuf *pixbuf = NULL;
	RBRemovableMediaSourcePrivate *priv;

	RB_CHAIN_GOBJECT_METHOD (rb_removable_media_source_parent_class, constructed, object);
	priv = REMOVABLE_MEDIA_SOURCE_GET_PRIVATE (object);

	/* prefer mount details to volume details, as the nautilus sidebar does */
	if (priv->mount != NULL) {
		mount = g_object_ref (priv->mount);
	} else if (priv->volume != NULL) {
		mount = g_volume_get_mount (priv->volume);
	} else {
		mount = NULL;
	}

	if (mount != NULL) {
		display_name = g_mount_get_name (mount);
		icon = g_mount_get_icon (mount);
		rb_debug ("details from mount: display name = %s, icon = %p", display_name, icon);
	} else if (priv->volume != NULL) {
		display_name = g_volume_get_name (priv->volume);
		icon = g_volume_get_icon (priv->volume);
		rb_debug ("details from volume: display name = %s, icon = %p", display_name, icon);
	} else {
		display_name = g_strdup ("Unknown Device");
		icon = g_themed_icon_new ("multimedia-player");
	}

	g_object_set (object, "name", display_name, NULL);
	g_free (display_name);

	if (icon == NULL) {
		rb_debug ("no icon set");
		pixbuf = NULL;
	} else if (G_IS_THEMED_ICON (icon)) {
		GtkIconTheme *theme;
		const char * const *names;
		gint size;
		int i;

		theme = gtk_icon_theme_get_default ();
		gtk_icon_size_lookup (RB_SOURCE_ICON_SIZE, &size, NULL);

		i = 0;
		names = g_themed_icon_get_names (G_THEMED_ICON (icon));
		while (names[i] != NULL && pixbuf == NULL) {
			rb_debug ("looking up themed icon: %s", names[i]);
			pixbuf = gtk_icon_theme_load_icon (theme, names[i], size, 0, NULL);
			i++;
		}

	} else if (G_IS_LOADABLE_ICON (icon)) {
		rb_debug ("loading of GLoadableIcons is not implemented yet");
		pixbuf = NULL;
	}

	rb_source_set_pixbuf (RB_SOURCE (object), pixbuf);
	if (pixbuf != NULL) {
		g_object_unref (pixbuf);
	}
	if (mount != NULL) {
		g_object_unref (mount);
	}
	g_object_unref (icon);
}

static void
rb_removable_media_source_dispose (GObject *object)
{
	RBRemovableMediaSourcePrivate *priv = REMOVABLE_MEDIA_SOURCE_GET_PRIVATE (object);

	if (priv->volume) {
		g_object_unref (priv->volume);
		priv->volume = NULL;
	}
	if (priv->mount) {
		g_object_unref (priv->mount);
		priv->mount = NULL;
	}

	G_OBJECT_CLASS (rb_removable_media_source_parent_class)->dispose (object);
}

static void
rb_removable_media_source_set_property (GObject *object,
				        guint prop_id,
				        const GValue *value,
				        GParamSpec *pspec)
{
	RBRemovableMediaSourcePrivate *priv = REMOVABLE_MEDIA_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_VOLUME:
		if (priv->volume) {
			g_object_unref (priv->volume);
		}
		priv->volume = g_value_get_object (value);
		if (priv->volume) {
			g_object_ref (priv->volume);
		}
		break;
	case PROP_MOUNT:
		if (priv->mount) {
			g_object_unref (priv->mount);
		}
		priv->mount = g_value_get_object (value);
		if (priv->mount) {
			g_object_ref (priv->mount);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_removable_media_source_get_property (GObject *object,
				        guint prop_id,
				        GValue *value,
				        GParamSpec *pspec)
{
	RBRemovableMediaSourcePrivate *priv = REMOVABLE_MEDIA_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_VOLUME:
		g_value_set_object (value, priv->volume);
		break;
	case PROP_MOUNT:
		g_value_set_object (value, priv->mount);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_delete_thyself (RBSource *source)
{
	RhythmDB *db;
	RBShell *shell;
	RhythmDBEntryType entry_type;

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	g_object_get (source, "entry-type", &entry_type, NULL);
	rb_debug ("deleting all entries of type '%s'", entry_type->name);
	rhythmdb_entry_delete_by_type (db, entry_type);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	rhythmdb_commit (db);
	g_object_unref (db);
}

struct _TrackAddedData {
	RBRemovableMediaSource *source;
	char *mimetype;
};

static void
_track_added_cb (RhythmDBEntry *entry, const char *uri, guint64 dest_size, GError *error, struct _TrackAddedData *data)
{
	if (error == NULL) {
		rb_removable_media_source_track_added (data->source, entry, uri, dest_size, data->mimetype);
	} else {
		rb_removable_media_source_track_add_error (data->source, entry, uri, error);
	}
	g_free (data->mimetype);
	g_free (data);
}

static void
impl_paste (RBSource *source, GList *entries)
{
	RBRemovableMediaManager *rm_mgr;
	RBShell *shell;
	GList *l;
	RhythmDBEntryType our_entry_type;
	RBEncoder *encoder;

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell,
		      "removable-media-manager", &rm_mgr,
		      NULL);
	g_object_unref (shell);

	g_object_get (source,
		      "entry-type", &our_entry_type,
		      NULL);

	encoder = rb_encoder_new ();

	for (l = entries; l != NULL; l = l->next) {
		RhythmDBEntry *entry;
		RhythmDBEntryType entry_type;
		GList *mime_types;
		const char *entry_mime;
		char *mimetype;
		char *extension;
		char *dest;
		struct _TrackAddedData *added_data;

		dest = NULL;
		mimetype = NULL;
		extension = NULL;
		mime_types = NULL;
		entry = (RhythmDBEntry *)l->data;
		entry_type = rhythmdb_entry_get_entry_type (entry);

		if (entry_type == our_entry_type ||
		    !rb_removable_media_source_should_paste (RB_REMOVABLE_MEDIA_SOURCE (source), entry)) {
			goto impl_paste_end;
		}

		entry_mime = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);
		/* hackish mapping of gstreamer media types to mime types; this
		 * should be easier when we do proper (deep) typefinding.
		 */
		if (strcmp (entry_mime, "audio/x-wav") == 0) {
			/* if it has a bitrate, assume it's mp3-in-wav */
			if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE) != 0)
				entry_mime = "audio/mpeg";
		} else if (strcmp (entry_mime, "audio/x-m4a") == 0) {
			entry_mime = "audio/aac";
		} else if (strcmp (entry_mime, "application/x-id3") == 0) {
			entry_mime = "audio/mpeg";
		} else if (strcmp (entry_mime, "audio/x-flac") == 0) {
			entry_mime = "audio/flac";
		}

		mime_types = rb_removable_media_source_get_mime_types (RB_REMOVABLE_MEDIA_SOURCE (source));
		if (mime_types != NULL && !rb_string_list_contains (mime_types, entry_mime)) {
			if (!rb_encoder_get_preferred_mimetype (encoder, mime_types, &mimetype, &extension)) {
				rb_debug ("failed to find acceptable mime type for %s", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
				goto impl_paste_end;
			}
		} else {
			const char *s;
			char       *path;

			rb_debug ("copying using existing format");
			path = rb_uri_get_short_path_name (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
			s = g_strrstr (path, ".");
			extension = (s != NULL) ? g_strdup (s + 1) : NULL;
			g_free (path);
		}

		/* make sure the extension isn't ludicrously long */
		if (extension != NULL && strlen (extension) > EXTENSION_LENGTH_LIMIT) {
			extension[EXTENSION_LENGTH_LIMIT] = '\0';
		}

		dest = rb_removable_media_source_build_dest_uri (RB_REMOVABLE_MEDIA_SOURCE (source), entry, mimetype, extension);
		if (dest == NULL) {
			rb_debug ("could not create destination path for entry");
			goto impl_paste_end;
		}

		rb_list_deep_free (mime_types);
		if (mimetype != NULL)
			mime_types = g_list_prepend (NULL, g_strdup (mimetype));
		else
			mime_types = NULL;
		added_data = g_new0 (struct _TrackAddedData, 1);
		added_data->source = RB_REMOVABLE_MEDIA_SOURCE (source);
		added_data->mimetype = g_strdup (mimetype);
		rb_removable_media_manager_queue_transfer (rm_mgr, entry,
							   dest, mime_types,
							   (RBTransferCompleteCallback)_track_added_cb, added_data);
impl_paste_end:
		g_free (dest);
		g_free (mimetype);
		g_free (extension);
		if (mime_types)
			rb_list_deep_free (mime_types);
		if (entry_type)
			g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
	}

	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, our_entry_type);
	g_object_unref (rm_mgr);
	g_object_unref (encoder);
}

static guint
impl_want_uri (RBSource *source, const char *uri)
{
	RBRemovableMediaSourcePrivate *priv = REMOVABLE_MEDIA_SOURCE_GET_PRIVATE (source);
	GVolume *volume;
	GFile *file;
	char *device_path, *uri_path;
	int retval;
	int len;

	retval = 0;

	/* A default version for use with the audio players
	 * that use mass storage */
	file = g_file_new_for_uri (uri);
	if (g_file_has_uri_scheme (file, "file") == FALSE) {
		g_object_unref (file);
		return 0;
	}

	/* Deal with the mount root being passed, eg. file:///media/IPODNAME */
	if (priv->mount) {
		GFile *root;

		root = g_mount_get_root (priv->mount);
		retval = g_file_equal (root, file) ? 100 : 0;
		g_object_unref (root);
		if (retval) {
			g_object_unref (file);
			return retval;
		}
		volume = g_mount_get_volume (priv->mount);
	} else if (priv->volume) {
		volume = g_object_ref (priv->volume);
	} else {
		return 0;
	}

	if (volume == NULL) {
		g_object_unref (file);
		return 0;
	}

	/* Deal with the path to the device node being passed */
	device_path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	g_object_unref (volume);
	if (device_path == NULL) {
		g_object_unref (file);
		return 0;
	}

	uri_path = g_file_get_path (file);
	g_object_unref (file);
	if (uri_path == NULL)
		return 0;
	len = strlen (uri_path);
	if (uri_path[len - 1] == '/') {
		if (strncmp (uri_path, device_path, len - 1) == 0)
			retval = 100;
	} else if (strcmp (uri_path, device_path) == 0)
		retval = 100;

	g_free (device_path);
	g_free (uri_path);
	return retval;
}

static gboolean
impl_uri_is_source (RBSource *source, const char *uri)
{
	if (impl_want_uri (source, uri) == 100)
		return TRUE;
	return FALSE;
}

static RhythmDB *
get_db_for_source (RBSource *source)
{
	RBShell *shell;
	RhythmDB *db;

  	g_object_get (source, "shell", &shell, NULL);
  	g_object_get (shell, "db", &db, NULL);
  	g_object_unref (shell);

        return db;
}

static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *data)
{
	GList *entries;
	RhythmDB *db;
	char *type;

	entries = NULL;
	type = gdk_atom_name (gtk_selection_data_get_data_type (data));
        db = get_db_for_source (asource);

	if (strcmp (type, "text/uri-list") == 0) {
		GList *list;
		GList *i;

		rb_debug ("parsing uri list");
		list = rb_uri_list_parse ((const char *) gtk_selection_data_get_data (data));

		for (i = list; i != NULL; i = g_list_next (i)) {
			char *uri;
			RhythmDBEntry *entry;

			if (i->data == NULL)
				continue;

			uri = i->data;
			entry = rhythmdb_entry_lookup_by_location (db, uri);

			if (entry == NULL) {
				/* add to the library */
				rb_debug ("received drop of unknown uri: %s", uri);
			} else {
				/* add to list of entries to copy */
				entries = g_list_prepend (entries, entry);
			}
			g_free (uri);
		}
		g_list_free (list);
	} else if (strcmp (type, "application/x-rhythmbox-entry") == 0) {
		char **list;
		char **i;

		rb_debug ("parsing entry ids");
		list = g_strsplit ((const char*) gtk_selection_data_get_data (data), "\n", -1);
		for (i = list; *i != NULL; i++) {
			RhythmDBEntry *entry;
			gulong id;

			id = atoi (*i);
			entry = rhythmdb_entry_lookup_by_id (db, id);
			if (entry != NULL)
				entries = g_list_prepend (entries, entry);
		}

		g_strfreev (list);
	} else {
		rb_debug ("received unknown drop type");
	}

	g_object_unref (db);
	g_free (type);

	if (entries) {
		entries = g_list_reverse (entries);
		if (rb_source_can_paste (asource))
			rb_source_paste (asource, entries);
		g_list_free (entries);
	}

	return TRUE;
}

/**
 * rb_removable_media_source_build_dest_uri:
 * @source: an #RBRemovableMediaSource
 * @entry: the #RhythmDBEntry to build a URI for
 * @mimetype: destination media type
 * @extension: extension associated with destination media type
 *
 * Constructs a URI to use as the destination for a transfer or transcoding
 * operation.  The URI may be on the device itself, if the device is mounted
 * into the normal filesystem or through gvfs, or it may be a temporary
 * location used to store the file before uploading it to the device.
 *
 * The destination URI should conform to the device's normal URI format,
 * and should use the provided extension instead of the extension from
 * the source entry.
 *
 * Return value: constructed URI
 */
char *
rb_removable_media_source_build_dest_uri (RBRemovableMediaSource *source,
					  RhythmDBEntry *entry,
					  const char *mimetype,
					  const char *extension)
{
	RBRemovableMediaSourceClass *klass = RB_REMOVABLE_MEDIA_SOURCE_GET_CLASS (source);
	char *uri = NULL;
	char *sane_uri = NULL;

	if (klass->impl_build_dest_uri) {
		uri = klass->impl_build_dest_uri (source, entry, mimetype, extension);
	} else {
		uri = NULL;
	}

	sane_uri = rb_sanitize_uri_for_filesystem(uri);
	g_return_val_if_fail(sane_uri != NULL, NULL);
	g_free(uri);
	uri = sane_uri;

	rb_debug ("Built dest URI for mime='%s', extension='%s': '%s'",
		  mimetype,
		  extension,
		  uri);

	return uri;
}

/**
 * rb_removable_media_source_get_mime_types:
 * @source: an #RBRemovableMediaSource
 *
 * Returns a #GList of allocated media type strings describing the
 * formats supported by the device.  If possible, these should be
 * sorted in order of preference, as the first entry in the list
 * for which an encoder is available will be used.
 *
 * Common media types include "audio/mpeg" for MP3, "application/ogg"
 * for Ogg Vorbis, "audio/x-flac" for FLAC, and "audio/x-aac" for
 * MP4/AAC.
 *
 * Return value: list of media types
 */
GList *
rb_removable_media_source_get_mime_types (RBRemovableMediaSource *source)
{
	RBRemovableMediaSourceClass *klass = RB_REMOVABLE_MEDIA_SOURCE_GET_CLASS (source);

	if (klass->impl_get_mime_types)
		return klass->impl_get_mime_types (source);
	else
		return NULL;
}

/**
 * rb_removable_media_source_get_format_descriptions:
 * @source: a #RBRemovableMediaSource
 *
 * Returns a #GList of allocated media format descriptions for
 * the formats supported by the device.  The list and the strings
 * it holds must be freed by the caller.
 *
 * Return value: list of descriptions.
 */
GList *
rb_removable_media_source_get_format_descriptions (RBRemovableMediaSource *source)
{
	GList *mime;
	GList *desc = NULL;
	GList *t;

	mime = rb_removable_media_source_get_mime_types (source);
	for (t = mime; t != NULL; t = t->next) {
		const char *mimetype;
		char *content_type;

		mimetype = t->data;
		content_type = g_content_type_from_mime_type (mimetype);
		if (content_type != NULL) {
			char *description;
			description = g_content_type_get_description (content_type);
			desc = g_list_append (desc, description);
		} else {
			desc = g_list_append (desc, g_strdup (mimetype));
		}
	}

	rb_list_deep_free (mime);
	return desc;
}

/**
 * rb_removable_media_source_should_paste_no_duplicate:
 * @source: an #RBRemovableMediaSource
 * @entry: a #RhythmDBEntry to consider pasting
 *
 * This implementation of #rb_removable_media_should_paste checks for
 * an existing entry on the device that matches the title, album, artist,
 * and track number of the entry being considered.
 *
 * Return value: %TRUE if the entry should be transferred to the device.
 */
gboolean
rb_removable_media_source_should_paste_no_duplicate (RBRemovableMediaSource *source,
						     RhythmDBEntry *entry)
{
	RhythmDBEntryType entry_type;
	RhythmDB *db;
	RBShell *shell;
	const char *title;
	const char *album;
	const char *artist;
	gulong track_number;
	GtkTreeModel *query_model;
	GtkTreeIter iter;
	gboolean no_match;
	 
	RBRemovableMediaSourceClass *rms_class = RB_REMOVABLE_MEDIA_SOURCE_CLASS (g_type_class_peek_parent (RB_REMOVABLE_MEDIA_SOURCE_GET_CLASS (source)));
	/* chain up to parent impl */
	if (!rms_class->impl_should_paste (source, entry))
		return FALSE;

	g_object_get (source, "shell", &shell, "entry-type", &entry_type, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	title = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
	album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
	artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
	track_number = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER);
	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (query_model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, entry_type,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_ARTIST, artist,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_ALBUM, album,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TITLE, title,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TRACK_NUMBER, track_number,
				RHYTHMDB_QUERY_END);

	no_match = (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (query_model),
						 &iter));
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
	g_object_unref(query_model);
	g_object_unref (db);
	if (no_match == FALSE) {
		rb_debug ("not adding %lu - %s - %s - %s to removable device since it's already present", track_number, title, album, artist);
	}
	return no_match;
}

static gboolean
impl_should_paste (RBRemovableMediaSource *source, RhythmDBEntry *entry)
{
	RhythmDBEntryType entry_type = rhythmdb_entry_get_entry_type (entry);
	gboolean should_paste = (entry_type->category == RHYTHMDB_ENTRY_NORMAL);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
	return should_paste;
}

/**
 * rb_removable_media_source_should_paste:
 * @source: an #RBRemovableMediaSource
 * @entry: a #RhythmDBEntry to consider pasting
 *
 * Checks whether @entry should be transferred to the device.
 * The source can check whether a matching entry already exists on the device,
 * for instance.  See @rb_removable_media_source_should_paste_no_duplicate
 * a useful implementation.
 *
 * Return value: %TRUE if the entry should be transferred to the device
 */
gboolean
rb_removable_media_source_should_paste (RBRemovableMediaSource *source,
					RhythmDBEntry *entry)
{
	RBRemovableMediaSourceClass *klass = RB_REMOVABLE_MEDIA_SOURCE_GET_CLASS (source);

	return klass->impl_should_paste (source, entry);
}

/**
 * rb_removable_media_source_track_added:
 * @source: an #RBRemovableMediaSource
 * @entry: the source #RhythmDBEntry for the transfer
 * @uri: the destination URI
 * @filesize: size of the destination file
 * @mimetype: media type of the destination file
 *
 * This is called when a transfer to the device has completed.
 * If the source's impl_track_added method returns %TRUE, the destination
 * URI will be added to the database using the entry type for the device.
 *
 * If the source uses a temporary area as the destination for transfers,
 * it can instead upload the destination file to the device and create an
 * entry for it, then return %FALSE.
 */
void
rb_removable_media_source_track_added (RBRemovableMediaSource *source,
				       RhythmDBEntry *entry,
				       const char *uri,
				       guint64 filesize,
				       const char *mimetype)
{
	RBRemovableMediaSourceClass *klass = RB_REMOVABLE_MEDIA_SOURCE_GET_CLASS (source);
	gboolean add_to_db = TRUE;

	if (klass->impl_track_added)
		add_to_db = klass->impl_track_added (source, entry, uri, filesize, mimetype);

	if (add_to_db) {
		RhythmDBEntryType entry_type;
		RhythmDB *db;
		RBShell *shell;

		g_object_get (source, "shell", &shell, NULL);
		g_object_get (shell, "db", &db, NULL);
		g_object_unref (shell);

		g_object_get (source, "entry-type", &entry_type, NULL);
		rhythmdb_add_uri_with_types (db, uri, entry_type, RHYTHMDB_ENTRY_TYPE_INVALID, RHYTHMDB_ENTRY_TYPE_INVALID);
		g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

		g_object_unref (db);
	}
}

/**
 * rb_removable_media_source_track_add_error:
 * @source: an #RBRemovableMediaSource
 * @entry: the source #RhythmDBEntry for the transfer
 * @uri: the destination URI
 * @error: the transfer error information
 *
 * This is called when a transfer fails.  If the source's
 * impl_track_add_error implementation returns %TRUE, an error dialog
 * will be displayed to the user containing the error message, unless
 * the error indicates that the destination file already exists.
 */
void
rb_removable_media_source_track_add_error (RBRemovableMediaSource *source,
					   RhythmDBEntry *entry,
					   const char *uri,
					   GError *error)
{
	RBRemovableMediaSourceClass *klass = RB_REMOVABLE_MEDIA_SOURCE_GET_CLASS (source);
	gboolean show_dialog = TRUE;

	if (klass->impl_track_add_error)
		show_dialog = klass->impl_track_add_error (source, entry, uri, error);

	if (show_dialog) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			rb_debug ("not displaying 'file exists' error for %s", uri);
		} else {
			rb_error_dialog (NULL, _("Error transferring track"), "%s", error->message);
		}
	}
}

static char *
impl_get_delete_action (RBSource *source)
{
	return g_strdup ("EditDelete");
}
