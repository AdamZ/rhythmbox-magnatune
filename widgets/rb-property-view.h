/*
 *  arch-tag: Header for widget to display RhythmDB metadata
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

#ifndef __RB_PROPERTY_VIEW_H
#define __RB_PROPERTY_VIEW_H

#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkdnd.h>

#include "rhythmdb.h"
#include "rhythmdb-property-model.h"
#include "rb-entry-view.h"

G_BEGIN_DECLS

#define RB_TYPE_PROPERTY_VIEW         (rb_property_view_get_type ())
#define RB_PROPERTY_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PROPERTY_VIEW, RBPropertyView))
#define RB_PROPERTY_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_PROPERTY_VIEW, RBPropertyViewClass))
#define RB_IS_PROPERTY_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PROPERTY_VIEW))
#define RB_IS_PROPERTY_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_PROPERTY_VIEW))
#define RB_PROPERTY_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_PROPERTY_VIEW, RBPropertyViewClass))

typedef struct RBPropertyViewPrivate RBPropertyViewPrivate;

typedef struct
{
	GtkScrolledWindow parent;

	RBPropertyViewPrivate *priv;
} RBPropertyView;

typedef struct
{
	GtkScrolledWindowClass parent;

	void (*property_selected)	(RBPropertyView *view, const char *name);
	void (*properties_selected)	(RBPropertyView *view, GList *properties);
	void (*property_activated)	(RBPropertyView *view, const char *name);
	void (*selection_reset)		(RBPropertyView *view);
} RBPropertyViewClass;

GType		rb_property_view_get_type		(void);

RBPropertyView *rb_property_view_new			(RhythmDB *db, guint propid,
							 const char *title);

void		rb_property_view_set_selection_mode	(RBPropertyView *view,
							 GtkSelectionMode mode);

void		rb_property_view_reset			(RBPropertyView *view);

void		rb_property_view_set_selection		(RBPropertyView *view,
							 const GList *names);

RhythmDBPropertyModel * rb_property_view_get_model	(RBPropertyView *view);

void		rb_property_view_set_model		(RBPropertyView *view,
							 RhythmDBPropertyModel *model);

guint		rb_property_view_get_num_properties	(RBPropertyView *view);

G_END_DECLS

#endif /* __RB_PROPERTY_VIEW_H */
