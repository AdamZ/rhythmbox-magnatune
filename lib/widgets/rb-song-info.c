/* 
 *  Copyright (C) 2002 Olivier Martin <omartin@ifrance.com>
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
 *  $Id$
 */

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtkstock.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkentry.h>
#include <gtk/gtktooltips.h>
#include <gtk/gtkmenu.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkbbox.h>
#include <gtk/gtkcombo.h>
#include <gtk/gtktextview.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkalignment.h>
#include <gtk/gtkstock.h>
#include <glade/glade.h>
#include <string.h>
#include <monkey-media-stream-info.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rb-node.h"
#include "rb-song-info.h"
#include "rb-glade-helpers.h"
#include "rb-dialog.h"
#include "rb-node-song.h"

static void rb_song_info_class_init (RBSongInfoClass *klass);
static void rb_song_info_init (RBSongInfo *song_info);
static void rb_song_info_finalize (GObject *object);
static void rb_song_info_set_property (GObject *object, 
				       guint prop_id,
				       const GValue *value, 
				       GParamSpec *pspec);
static void rb_song_info_get_property (GObject *object, 
				       guint prop_id,
				       GValue *value, 
				       GParamSpec *pspec);
static void rb_song_info_response_cb (GtkDialog *dialog,
				      int response_id,
				      RBSongInfo *song_info);
static void rb_song_info_populate_dialog (RBSongInfo *song_info);
static void rb_song_info_update_title (RBSongInfo *song_info);
static void rb_song_info_update_track (RBSongInfo *song_info);
static void rb_song_info_update_bitrate (RBSongInfo *song_info);
static void rb_song_info_update_channels (RBSongInfo *song_info);
static void rb_song_info_update_size (RBSongInfo *song_info);
static void rb_song_info_update_duration (RBSongInfo *song_info);
static void rb_song_info_update_location (RBSongInfo *song_info);
static void rb_song_info_update_genre (RBSongInfo *song_info);
static void rb_song_info_update_entry (RBSongInfo *song_info,
		                       MonkeyMediaStreamInfoField field,
		                       GtkWidget *widget);
static void rb_song_info_update_comments (RBSongInfo *song_info);
static void rb_song_info_update_buttons (RBSongInfo *song_info);
static gboolean rb_song_info_update_current_values (RBSongInfo *song_info);

static void song_info_forward_clicked_cb (GtkWidget *button,
					  RBSongInfo *song_info);
static void rb_song_info_view_changed_cb (RBNodeView *node_view,
					  RBSongInfo *song_info);

struct RBSongInfoPrivate
{
	RBNodeView *node_view;

	/* infrmation on the displayed song */
	RBNode *current_node;
	MonkeyMediaStreamInfo *current_info;

	/* the dialog widgets */
	GtkTooltips *tooltips;
	GtkWidget   *forward;
	GtkWidget   *title;
	GtkWidget   *artist;
	GtkWidget   *album;
	GtkWidget   *date;
	GtkWidget   *track_cur;
	GtkWidget   *track_max;
	GtkWidget   *genre;
	GtkWidget   *comments;
	GtkWidget   *bitrate;
	GtkWidget   *channels;
	GtkWidget   *size;
	GtkWidget   *duration;
	GtkWidget   *location_ebox;
	GtkWidget   *location;
};

enum 
{
	PROP_0,
	PROP_NODE_VIEW
};

static GObjectClass *parent_class = NULL;

GType
rb_song_info_get_type (void)
{
	static GType rb_song_info_type = 0;

	if (rb_song_info_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBSongInfoClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_song_info_class_init,
			NULL,
			NULL,
			sizeof (RBSongInfo),
			0,
			(GInstanceInitFunc) rb_song_info_init
		};

		rb_song_info_type = g_type_register_static (GTK_TYPE_DIALOG,
							    "RBSongInfo",
							    &our_info, 0);
	}

	return rb_song_info_type;
}

static void
rb_song_info_class_init (RBSongInfoClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->set_property = rb_song_info_set_property;
	object_class->get_property = rb_song_info_get_property;

	g_object_class_install_property (object_class,
					 PROP_NODE_VIEW,
					 g_param_spec_object ("node_view",
					                      "RBNodeView",
					                      "RBNodeView object",
					                      RB_TYPE_NODE_VIEW,
					                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	object_class->finalize = rb_song_info_finalize;
}

static void
rb_song_info_init (RBSongInfo *song_info)
{
	GladeXML *xml;
	GtkWidget *close, *label, *image, *hbox, *align;
	
	/* create the dialog and some buttons back - forward - close */
	song_info->priv = g_new0 (RBSongInfoPrivate, 1);

	g_signal_connect (G_OBJECT (song_info),
			  "response",
			  G_CALLBACK (rb_song_info_response_cb),
			  song_info);

	gtk_dialog_set_has_separator (GTK_DIALOG (song_info), FALSE);

	gtk_button_box_set_layout (GTK_BUTTON_BOX (GTK_DIALOG (song_info)->action_area),
				   GTK_BUTTONBOX_START);

	song_info->priv->forward = gtk_button_new ();

	label = gtk_label_new_with_mnemonic (_("_Next Song"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), song_info->priv->forward);
	
	image = gtk_image_new_from_stock (GTK_STOCK_GO_FORWARD, GTK_ICON_SIZE_BUTTON);
	
	hbox = gtk_hbox_new (FALSE, 2);
	
	align = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);

	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (hbox), label, FALSE, FALSE, 0);
	
	gtk_container_add (GTK_CONTAINER (song_info->priv->forward), align);
	gtk_container_add (GTK_CONTAINER (align), hbox);
	
	gtk_widget_show_all (song_info->priv->forward);

	gtk_dialog_add_action_widget (GTK_DIALOG (song_info),
			              song_info->priv->forward,
				      GTK_RESPONSE_NONE);
	g_signal_connect (G_OBJECT (song_info->priv->forward),
			  "clicked",
			  G_CALLBACK (song_info_forward_clicked_cb),
			  song_info);

	close = gtk_dialog_add_button (GTK_DIALOG (song_info),
			               GTK_STOCK_CLOSE,
			               GTK_RESPONSE_CLOSE);
	gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (GTK_DIALOG (song_info)->action_area), close, TRUE);
	gtk_dialog_set_default_response (GTK_DIALOG (song_info),
					 GTK_RESPONSE_CLOSE);
	gtk_container_set_border_width (GTK_CONTAINER (song_info), 7);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (song_info)->vbox), 8);

	gtk_window_set_title (GTK_WINDOW (song_info), _("Song Properties"));

	xml = rb_glade_xml_new ("song-info.glade",
				"song_info_vbox",
				song_info);
	glade_xml_signal_autoconnect (xml);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (song_info)->vbox),
			   glade_xml_get_widget (xml, "song_info_vbox"));

	/* get the widgets */
	song_info->priv->tooltips      = gtk_tooltips_new ();
	song_info->priv->title         = glade_xml_get_widget (xml, "song_info_title");
	song_info->priv->artist        = glade_xml_get_widget (xml, "song_info_artist");
	song_info->priv->album         = glade_xml_get_widget (xml, "song_info_album");
	song_info->priv->date          = glade_xml_get_widget (xml, "song_info_date");
	song_info->priv->track_cur     = glade_xml_get_widget (xml, "song_info_track_cur");
	song_info->priv->track_max     = glade_xml_get_widget (xml, "song_info_track_max");
	song_info->priv->genre         = glade_xml_get_widget (xml, "song_info_genre");
	song_info->priv->comments      = glade_xml_get_widget (xml, "song_info_comments");
	song_info->priv->bitrate       = glade_xml_get_widget (xml, "song_info_bitrate");
	song_info->priv->channels      = glade_xml_get_widget (xml, "song_info_channels");
	song_info->priv->size          = glade_xml_get_widget (xml, "song_info_size");
	song_info->priv->duration      = glade_xml_get_widget (xml, "song_info_duration");
	song_info->priv->location_ebox = glade_xml_get_widget (xml, "song_info_location_ebox");
	song_info->priv->location      = glade_xml_get_widget (xml, "song_info_location");

	/* default focus */
	gtk_widget_grab_focus (song_info->priv->title);

	g_object_unref (G_OBJECT (xml));
}

static void
rb_song_info_finalize (GObject *object)
{
	RBSongInfo *song_info;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SONG_INFO (object));

	song_info = RB_SONG_INFO (object);

	g_return_if_fail (song_info->priv != NULL);

	if (song_info->priv->current_info != NULL)
	{
		g_object_unref (G_OBJECT (song_info->priv->current_info));
	}

	g_free (song_info->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_song_info_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBSongInfo *song_info = RB_SONG_INFO (object);

	switch (prop_id)
	{
	case PROP_NODE_VIEW:
		{
			RBNodeView *node_view = g_value_get_object (value);
			song_info->priv->node_view = node_view;
			rb_song_info_update_current_values (song_info);

			/* install some callbacks on the node view */
			g_signal_connect_object (G_OBJECT (node_view),
						 "changed",
						 G_CALLBACK (rb_song_info_view_changed_cb),
						 song_info,
						 G_CONNECT_AFTER);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_song_info_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBSongInfo *song_info = RB_SONG_INFO (object);

	switch (prop_id)
	{
	case PROP_NODE_VIEW:
		g_value_set_object (value, song_info->priv->node_view);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GtkWidget *
rb_song_info_new (RBNodeView *node_view)
{
	RBSongInfo *song_info;

	g_return_val_if_fail (RB_IS_NODE_VIEW (node_view), NULL);

	if (rb_node_view_have_selection (node_view) == FALSE) 
		return NULL;

	/* create the dialog */
	song_info = g_object_new (RB_TYPE_SONG_INFO, "node_view", node_view, NULL);

	g_return_val_if_fail (song_info->priv != NULL, NULL);

	rb_song_info_populate_dialog (song_info);

	return GTK_WIDGET (song_info);
}

static void
rb_song_info_response_cb (GtkDialog *dialog,
			  int response_id,
			  RBSongInfo *song_info)
{
	if (response_id == GTK_RESPONSE_CLOSE)
		gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void 
rb_song_info_populate_dialog (RBSongInfo *song_info)
{
	/* update the buttons sensitivity */
	rb_song_info_update_buttons (song_info);
	
	/* update the fields values */
	rb_song_info_update_title (song_info);
	rb_song_info_update_entry (song_info,
				   MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
				   song_info->priv->artist);
	rb_song_info_update_entry (song_info,
				   MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
				   song_info->priv->album);
	rb_song_info_update_entry (song_info,
				   MONKEY_MEDIA_STREAM_INFO_FIELD_DATE,
				   song_info->priv->date);
	rb_song_info_update_track (song_info);
	rb_song_info_update_genre (song_info);
	rb_song_info_update_comments (song_info);
	rb_song_info_update_bitrate (song_info);
	rb_song_info_update_channels (song_info);
	rb_song_info_update_size (song_info);
	rb_song_info_update_duration (song_info);
	rb_song_info_update_location (song_info);
}

static void
rb_song_info_update_title (RBSongInfo *song_info)
{
	GValue value = { 0, };
	const char *text = NULL;

	if (song_info->priv->current_info != NULL)
	{
		monkey_media_stream_info_get_value (song_info->priv->current_info, 
						    MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
						    0,
						    &value);
		text = g_value_get_string (&value);
	}
	else
	{
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, "");
	}

	if (text != NULL)
	{
		char *tmp;
		
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->title), text);

		tmp = g_strdup_printf (_("%s Properties"), text);
		gtk_window_set_title (GTK_WINDOW (song_info), tmp);
		g_free (tmp);
	}
	else
	{
		if (song_info->priv->current_node != NULL)
		{
			const char *url;
			char *tmp;

			url = rb_node_get_property_string (song_info->priv->current_node,
					                   RB_NODE_SONG_PROP_LOCATION);

			tmp = g_strdup_printf (_("%s Properties"), url);
			gtk_window_set_title (GTK_WINDOW (song_info), tmp);
			g_free (tmp);
		}
		else
			gtk_window_set_title (GTK_WINDOW (song_info), _("Song Properties"));
	}

	g_value_unset (&value);
}

static void
rb_song_info_update_comments (RBSongInfo *song_info)
{
	GValue value = { 0, };
	const char *text = NULL;
	GtkTextBuffer *buffer;
	GtkTextIter start, end;

	if (song_info->priv->current_info != NULL)
	{
		monkey_media_stream_info_get_value (song_info->priv->current_info, 
						    MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT,
						    0,
						    &value);
		text = g_value_get_string (&value);
	}
	else
	{
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, "");
	}

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (song_info->priv->comments));

	gtk_text_buffer_get_bounds (buffer, &start, &end);

	gtk_text_buffer_delete (buffer, &start, &end);

	if (text != NULL)
		gtk_text_buffer_insert (buffer, &start, text, -1);

	g_value_unset (&value);
}

static void
rb_song_info_update_track (RBSongInfo *song_info)
{
	GValue value = { 0, };
	int val;

	monkey_media_stream_info_get_value (song_info->priv->current_info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
					    0,
					    &value);
	val = g_value_get_int (&value);
	if (val >= 0)
	{
		char *tmp;
		tmp = g_strdup_printf ("%.2d", val);
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->track_cur), tmp);
		g_free (tmp);
	}
	g_value_unset (&value);

	monkey_media_stream_info_get_value (song_info->priv->current_info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER,
					    0,
					    &value);
	val = g_value_get_int (&value);
	if (val >= 0)
	{
		char *tmp;
		tmp = g_strdup_printf ("%.2d", val);
		gtk_entry_set_text (GTK_ENTRY (song_info->priv->track_max), tmp);
		g_free (tmp);
	}
	g_value_unset (&value);
}

static void
rb_song_info_update_entry (RBSongInfo *song_info,
		           MonkeyMediaStreamInfoField field,
		           GtkWidget *widget)
{
	GValue value = { 0, };
	const char *text = NULL;

	if (song_info->priv->current_info != NULL)
	{
		monkey_media_stream_info_get_value (song_info->priv->current_info, 
						    field,
						    0,
						    &value);
		text = g_value_get_string (&value);
	}
	else
	{
		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, "");
	}

	if (text != NULL)
		gtk_entry_set_text (GTK_ENTRY (widget), text);
	g_value_unset (&value);
}

static void
rb_song_info_update_genre (RBSongInfo *song_info)
{
	const char *genre;
	GValue value = { 0, };

	monkey_media_stream_info_get_value (song_info->priv->current_info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,
					    0,
					    &value);
	genre = g_value_get_string (&value);

	gtk_combo_set_popdown_strings (GTK_COMBO (song_info->priv->genre),
				       monkey_media_stream_info_list_all_genres ());

	if (genre != NULL)
		gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (song_info->priv->genre)->entry),
				    genre);
	
	g_value_unset (&value);
}

static void
rb_song_info_update_bitrate (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;

	monkey_media_stream_info_get_value (song_info->priv->current_info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE,
					    0,
					    &value);
	text = g_strdup_printf (_("%d kbps"), g_value_get_int (&value));
	gtk_label_set_text (GTK_LABEL (song_info->priv->bitrate), text);
	g_free (text);
	g_value_unset (&value);
}

static void
rb_song_info_update_channels (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;
	int channels;

	monkey_media_stream_info_get_value (song_info->priv->current_info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS,
					    0,
					    &value);
	channels = g_value_get_int (&value);
	switch (channels)
	{
		case 1:
			text = g_strdup (_("Mono"));
			break;
		case 2:
			text = g_strdup (_("Stereo"));
			break;
		default:
			text = g_strdup_printf ("%d", channels);
			break;
	}

	gtk_label_set_text (GTK_LABEL (song_info->priv->channels), text);
	g_free (text);
	g_value_unset (&value);
}

static void
rb_song_info_update_size (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;
	long size;

	monkey_media_stream_info_get_value (song_info->priv->current_info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE,
					    0,
					    &value);
	size = g_value_get_long (&value);
	text = gnome_vfs_format_file_size_for_display (size);
	gtk_label_set_text (GTK_LABEL (song_info->priv->size), text);
	g_free (text);
	g_value_unset (&value);
}

static void
rb_song_info_update_duration (RBSongInfo *song_info)
{
	GValue value = { 0, };
	char *text;
	long duration;
	int minutes, seconds;

	monkey_media_stream_info_get_value (song_info->priv->current_info, 
					    MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
					    0,
					    &value);
	duration = g_value_get_long (&value);
	minutes = duration / 60;
	seconds = duration % 60;
	text = g_strdup_printf ("%d:%02d", minutes, seconds);

	gtk_label_set_text (GTK_LABEL (song_info->priv->duration), text);
	g_free (text);
	g_value_unset (&value);
}

static void
rb_song_info_update_location (RBSongInfo *song_info)
{
	const char *text;
	char *basename;

	g_return_if_fail (song_info != NULL);

	text = rb_node_get_property_string (song_info->priv->current_node,
			                    RB_NODE_SONG_PROP_LOCATION);

	if (text != NULL)
	{
		char *tmp;
		
		basename = g_path_get_basename (text);
		tmp = gnome_vfs_unescape_string_for_display (basename);
		g_free (basename);
		gtk_label_set_text (GTK_LABEL (song_info->priv->location), tmp);
		g_free (tmp);
	
		tmp = gnome_vfs_unescape_string_for_display (text);
		gtk_tooltips_set_tip (song_info->priv->tooltips,
				      song_info->priv->location_ebox,
				      tmp, NULL);
		g_free (tmp);
	}
}

static void
song_info_forward_clicked_cb (GtkWidget *button,
			      RBSongInfo *song_info)
{
	const char *url;
	MonkeyMediaStreamInfo *info = NULL;
	RBNode *node = rb_node_view_get_node (song_info->priv->node_view,
					      song_info->priv->current_node,
					      TRUE);

	g_return_if_fail (node != NULL);

	/* update our node and info, then refresh the dlg */
	song_info->priv->current_node = node;

	url = rb_node_get_property_string (node,
			                   RB_NODE_SONG_PROP_LOCATION);
	
	info = monkey_media_stream_info_new (url, NULL);
	song_info->priv->current_info = info;

	/* update the node view */
	rb_node_view_select_node (song_info->priv->node_view, node);
	rb_node_view_scroll_to_node (song_info->priv->node_view, node);
}

/*
 * rb_song_info_update_buttons: update back/forward sensitivity
 */
static void
rb_song_info_update_buttons (RBSongInfo *song_info)
{
	RBNode *node = NULL;

	/* forward */
	node = rb_node_view_get_node (song_info->priv->node_view,
				      song_info->priv->current_node,
				      TRUE);

	gtk_widget_set_sensitive (song_info->priv->forward,
				  node != NULL);
}

/*
 * rb_song_info_view_changed_cb: when the view has changed
 * we need to refresh the information displayed
 */
static void
rb_song_info_view_changed_cb (RBNodeView *node_view,
			      RBSongInfo *song_info)
{
	/* the view has changed, so we reload the node information */
	g_object_unref (G_OBJECT (song_info->priv->current_info));

	if (rb_song_info_update_current_values (song_info) == FALSE)
	{
		return;
	}

	/* and refresh */
	rb_song_info_populate_dialog (song_info);
}

/*
 * rb_song_info_update_current_values: get the selection from
 * the node view and then updates current_node & current_info
 */
static gboolean
rb_song_info_update_current_values (RBSongInfo *song_info)
{
	const char *url;
	RBNode *node = NULL;
	MonkeyMediaStreamInfo *info;
	GList *selected_nodes;

	/* get the node */
	selected_nodes = rb_node_view_get_selection (song_info->priv->node_view);

	if ((selected_nodes == NULL) ||
	    (selected_nodes->data == NULL) ||
	    (RB_IS_NODE (selected_nodes->data) == FALSE))
	{
		song_info->priv->current_info = NULL;
		song_info->priv->current_node = NULL;
		gtk_widget_destroy (GTK_WIDGET (song_info));

		return FALSE;
	}

	song_info ->priv->current_node = node = selected_nodes->data;

	/* get the stream info */
	url = rb_node_get_property_string (node,
			                   RB_NODE_SONG_PROP_LOCATION);

	info = monkey_media_stream_info_new (url, NULL);
	song_info->priv->current_info = info;

	return TRUE;
}
