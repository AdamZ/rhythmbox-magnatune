/*
 *  arch-tag: Header for preferences dialog object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#include <gtk/gtkdialog.h>
#include "rb-preferences.h"

#ifndef __RB_SHELL_PREFERENCES_H
#define __RB_SHELL_PREFERENCES_H

G_BEGIN_DECLS

#define RB_TYPE_SHELL_PREFERENCES         (rb_shell_preferences_get_type ())
#define RB_SHELL_PREFERENCES(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SHELL_PREFERENCES, RBShellPreferences))
#define RB_SHELL_PREFERENCES_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SHELL_PREFERENCES, RBShellPreferencesClass))
#define RB_IS_SHELL_PREFERENCES(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SHELL_PREFERENCES))
#define RB_IS_SHELL_PREFERENCES_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SHELL_PREFERENCES))
#define RB_SHELL_PREFERENCES_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SHELL_PREFERENCES, RBShellPreferencesClass))

typedef struct RBShellPreferencesPrivate RBShellPreferencesPrivate;

typedef struct
{
	GtkDialog parent;

	RBShellPreferencesPrivate *priv;
} RBShellPreferences;

typedef struct
{
	GtkDialogClass parent_class;
} RBShellPreferencesClass;

GType		rb_shell_preferences_get_type		(void);

GtkWidget	*rb_shell_preferences_new		(GList *views);
void		rb_shell_preferences_append_page	(RBShellPreferences *prefs,
							 const char *name,
							 GtkWidget *widget);

G_END_DECLS

#endif /* __RB_SHELL_PREFERENCES_H */
