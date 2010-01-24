/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Implementatin of DAAP (iTunes Music Sharing) GStreamer source
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
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

#include <gst/gst.h>
#include <string.h>

#include "rb-daap-plugin.h"
#include "rb-daap-src.h"

#define RB_TYPE_DAAP_SRC (rb_daap_src_get_type())
#define RB_DAAP_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),RB_TYPE_DAAP_SRC,RBDAAPSrc))
#define RB_DAAP_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),RB_TYPE_DAAP_SRC,RBDAAPSrcClass))
#define RB_IS_DAAP_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),RB_TYPE_DAAP_SRC))
#define RB_IS_DAAP_SRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),RB_TYPE_DAAP_SRC))

typedef struct _RBDAAPSrc RBDAAPSrc;
typedef struct _RBDAAPSrcClass RBDAAPSrcClass;

struct _RBDAAPSrc
{
	GstBin parent;

	/* uri */
	gchar *daap_uri;

	GstElement *souphttpsrc;
	GstPad *ghostpad;
};

struct _RBDAAPSrcClass
{
	GstBinClass parent_class;
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (rb_daap_src_debug);
#define GST_CAT_DEFAULT rb_daap_src_debug

static GstElementDetails rb_daap_src_details =
GST_ELEMENT_DETAILS ("RBDAAP Source",
	"Source/File",
	"Read a DAAP (music share) file",
	"Charles Schmidt <cschmidt2@emich.edu");

static RBDaapPlugin *daap_plugin = NULL;

static void rb_daap_src_uri_handler_init (gpointer g_iface, gpointer iface_data);

static void
_do_init (GType daap_src_type)
{
	static const GInterfaceInfo urihandler_info = {
		rb_daap_src_uri_handler_init,
		NULL,
		NULL
	};
	GST_DEBUG_CATEGORY_INIT (rb_daap_src_debug,
				 "daapsrc", GST_DEBUG_FG_WHITE,
				 "Rhythmbox built in DAAP source element");

	g_type_add_interface_static (daap_src_type, GST_TYPE_URI_HANDLER,
			&urihandler_info);
}

GST_BOILERPLATE_FULL (RBDAAPSrc, rb_daap_src, GstBin, GST_TYPE_BIN, _do_init);

static void rb_daap_src_dispose (GObject *object);
static void rb_daap_src_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec);
static void rb_daap_src_get_property (GObject *object,
		          guint prop_id,
			  GValue *value,
			  GParamSpec *pspec);

static GstStateChangeReturn rb_daap_src_change_state (GstElement *element, GstStateChange transition);

void
rb_daap_src_set_plugin (RBPlugin *plugin)
{
	g_assert (RB_IS_DAAP_PLUGIN (plugin));
	daap_plugin = RB_DAAP_PLUGIN (plugin);
}

enum
{
	PROP_0,
	PROP_LOCATION,
};

static void
rb_daap_src_base_init (gpointer g_class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
	gst_element_class_add_pad_template (element_class,
		gst_static_pad_template_get (&srctemplate));
	gst_element_class_set_details (element_class, &rb_daap_src_details);
}

static void
rb_daap_src_class_init (RBDAAPSrcClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *element_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->dispose = rb_daap_src_dispose;
	gobject_class->set_property = rb_daap_src_set_property;
	gobject_class->get_property = rb_daap_src_get_property;

	element_class = GST_ELEMENT_CLASS (klass);
	element_class->change_state = rb_daap_src_change_state;

	g_object_class_install_property (gobject_class, PROP_LOCATION,
			g_param_spec_string ("location",
					     "file location",
					     "location of the file to read",
					     NULL,
					     G_PARAM_READWRITE));
}

static void
rb_daap_src_init (RBDAAPSrc *src, RBDAAPSrcClass *klass)
{
	GstPad *pad;

	/* create actual source */
	src->souphttpsrc = gst_element_factory_make ("souphttpsrc", NULL);
	if (src->souphttpsrc == NULL) {
		g_warning ("couldn't create souphttpsrc element");
		return;
	}

	gst_bin_add (GST_BIN (src), src->souphttpsrc);
	gst_object_ref (src->souphttpsrc);

	/* create ghost pad */
	pad = gst_element_get_pad (src->souphttpsrc, "src");
	src->ghostpad = gst_ghost_pad_new ("src", pad);
	gst_element_add_pad (GST_ELEMENT (src), src->ghostpad);
	gst_object_ref (src->ghostpad);
	gst_object_unref (pad);

	src->daap_uri = NULL;
}

static void
rb_daap_src_dispose (GObject *object)
{
	RBDAAPSrc *src;
	src = RB_DAAP_SRC (object);

	if (src->ghostpad) {
		gst_object_unref (src->ghostpad);
		src->ghostpad = NULL;
	}

	if (src->souphttpsrc) {
		gst_object_unref (src->souphttpsrc);
		src->souphttpsrc = NULL;
	}

	g_free (src->daap_uri);
	src->daap_uri = NULL;

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
rb_daap_src_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec)
{
	RBDAAPSrc *src = RB_DAAP_SRC (object);

	switch (prop_id) {
		case PROP_LOCATION:
			/* XXX check stuff */
			if (src->daap_uri) {
				g_free (src->daap_uri);
				src->daap_uri = NULL;
			}
			src->daap_uri = g_strdup (g_value_get_string (value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
rb_daap_src_get_property (GObject *object,
		          guint prop_id,
			  GValue *value,
			  GParamSpec *pspec)
{
	RBDAAPSrc *src = RB_DAAP_SRC (object);

	switch (prop_id) {
		case PROP_LOCATION:
			g_value_set_string (value, src->daap_uri);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

GstStateChangeReturn
rb_daap_src_change_state (GstElement *element, GstStateChange transition)
{
	RBDAAPSrc *src = RB_DAAP_SRC (element);

	switch (transition) {
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			const char *http = "http";
			char *httpuri;
			GstStructure *headers;
			RBDAAPSource *source;

			/* Retrieve extra headers for the HTTP connection. */
			source = rb_daap_plugin_find_source_for_uri (daap_plugin, src->daap_uri);
			if (source == NULL) {
				g_warning ("Unable to lookup source for URI: %s", src->daap_uri);
				return GST_STATE_CHANGE_FAILURE;
			}

			/* The following can fail if the source is no longer connected */
			headers = rb_daap_source_get_headers (source, src->daap_uri);
			if (headers == NULL) {
				return GST_STATE_CHANGE_FAILURE;
			}

			g_object_set (src->souphttpsrc, "extra-headers", headers, NULL);
			gst_structure_free (headers);

			/* Set daap://... URI as http:// on souphttpsrc to ready connection. */
			httpuri = g_strdup (src->daap_uri);
			strncpy (httpuri, http, 4);

			g_object_set (src->souphttpsrc, "location", httpuri, NULL);
			g_free (httpuri);
			break;
		}

		case GST_STATE_CHANGE_READY_TO_PAUSED:
			break;

		case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
			break;

		default:
			break;
	}

	return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static gboolean
plugin_init (GstPlugin *plugin)
{
	gboolean ret = gst_element_register (plugin, "rbdaapsrc", GST_RANK_PRIMARY, RB_TYPE_DAAP_SRC);
	return ret;
}

GST_PLUGIN_DEFINE_STATIC (GST_VERSION_MAJOR,
			  GST_VERSION_MINOR,
			  "rbdaap",
			  "element to access DAAP music share files",
			  plugin_init,
			  VERSION,
			  "GPL",
			  PACKAGE,
			  "");

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
rb_daap_src_uri_get_type (void)
{
	return GST_URI_SRC;
}

static gchar **
rb_daap_src_uri_get_protocols (void)
{
	static gchar *protocols[] = {"daap", NULL};

	return protocols;
}

static const gchar *
rb_daap_src_uri_get_uri (GstURIHandler *handler)
{
	RBDAAPSrc *src = RB_DAAP_SRC (handler);

	return src->daap_uri;
}

static gboolean
rb_daap_src_uri_set_uri (GstURIHandler *handler,
			 const gchar *uri)
{
	RBDAAPSrc *src = RB_DAAP_SRC (handler);

	if (GST_STATE (src) == GST_STATE_PLAYING || GST_STATE (src) == GST_STATE_PAUSED) {
		return FALSE;
	}

	g_object_set (G_OBJECT (src), "location", uri, NULL);

	return TRUE;
}

static void
rb_daap_src_uri_handler_init (gpointer g_iface,
			      gpointer iface_data)
{
	GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

	iface->get_type = rb_daap_src_uri_get_type;
	iface->get_protocols = rb_daap_src_uri_get_protocols;
	iface->get_uri = rb_daap_src_uri_get_uri;
	iface->set_uri = rb_daap_src_uri_set_uri;
}
