/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2007  James Henstridge <james@jamesh.id.au>
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

#ifndef RB_RADIO_TUNER_H
#define RB_RADIO_TUNER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_RADIO_TUNER         (rb_radio_tuner_get_type ())
#define RB_RADIO_TUNER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_RADIO_TUNER, RBRadioTuner))
#define RB_RADIO_TUNER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), RB_TYPE_RADIO_TUNER, RBRadioTunerClass))
#define RB_IS_RADIO_TUNER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_RADIO_TUNER))
#define RB_IS_RADIO_TUNER_CLASS(o)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_RADIO_TUNER))
#define RB_RADIO_TUNER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_RADIO_TUNER, RBRadioTunerClass))

typedef struct _RBRadioTuner RBRadioTuner;
typedef struct _RBRadioTunerPrivate RBRadioTunerPrivate;
typedef struct _RBRadioTunerClass RBRadioTunerClass;

struct _RBRadioTuner {
	GObject parent;
	RBRadioTunerPrivate *priv;

	gchar *card_name;

	double frequency;
	double min_freq;
	double max_freq;

	guint32 signal;

	guint is_stereo : 1;
	guint is_muted : 1;
};

struct _RBRadioTunerClass {
	GObjectClass parent_class;

	void (* changed) (RBRadioTuner *self);
};

GType         rb_radio_tuner_get_type      (void);
GType         rb_radio_tuner_register_type (GTypeModule *module);

RBRadioTuner *rb_radio_tuner_new           (const gchar *devname,
					    GError **err);
void          rb_radio_tuner_update        (RBRadioTuner *self);
gboolean      rb_radio_tuner_set_frequency (RBRadioTuner *self,
					    double frequency);
gboolean      rb_radio_tuner_set_mute      (RBRadioTuner *self,
					    gboolean mute);

G_END_DECLS

#endif /* RB_RADIO_TUNER_H */
