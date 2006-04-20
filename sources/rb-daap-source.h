/*
 *  Header for DAAP (iTunes Music Sharing) source object
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

#ifndef __RB_DAAP_SOURCE_H
#define __RB_DAAP_SOURCE_H

#include "rb-shell.h"
#include "rb-browser-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_DAAP_SOURCE         (rb_daap_source_get_type ())
#define RB_DAAP_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DAAP_SOURCE, RBDAAPSource))
#define RB_DAAP_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DAAP_SOURCE, RBDAAPSourceClass))
#define RB_IS_DAAP_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DAAP_SOURCE))
#define RB_IS_DAAP_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DAAP_SOURCE))
#define RB_DAAP_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DAAP_SOURCE, RBDAAPSourceClass))

typedef struct RBDAAPSourcePrivate RBDAAPSourcePrivate;

typedef struct {
	RBBrowserSource parent;

	RBDAAPSourcePrivate *priv;
} RBDAAPSource;

typedef struct {
	RBBrowserSourceClass parent;
} RBDAAPSourceClass;

RBSource * 
rb_daap_sources_init (RBShell *shell);

void 
rb_daap_sources_shutdown (RBShell *shell);

GType 
rb_daap_source_get_type (void);

RBDAAPSource *	
rb_daap_source_find_for_uri (const gchar *uri);

gchar *
rb_daap_source_get_headers (RBDAAPSource *source, 
			    const gchar *uri, 
			    glong time,
			    gint64 *bytes);

G_END_DECLS

#endif /* __RB_DAAP_SOURCE_H */
