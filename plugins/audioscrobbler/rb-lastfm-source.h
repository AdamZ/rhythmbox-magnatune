/* 
 *  arch-tag: Header for last.fm station source
 *
 *  Copyright (C) 2006 Matt Novenstern  <fisxoj@gmail.com>
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

#ifndef __RB_LASTFM_SOURCE_H
#define __RB_LASTFM_SOURCE_H

#include "rb-shell.h"
#include "rb-source.h"

G_BEGIN_DECLS

#define RB_TYPE_LASTFM_SOURCE         (rb_lastfm_source_get_type ())
#define RB_LASTFM_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LASTFM_SOURCE, RBLastfmSource))
#define RB_LASTFM_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LASTFM_SOURCE, RBLastfmSourceClass))
#define RB_IS_LASTFM_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LASTFM_SOURCE))
#define RB_IS_LASTFM_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LASTFM_SOURCE))
#define RB_LASTFM_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LASTFM_SOURCE, RBLastfmSourceClass))

typedef struct RBLastfmSourcePrivate RBLastfmSourcePrivate;

typedef struct
{
	RBSource parent;

	RBLastfmSourcePrivate *priv;
} RBLastfmSource;

typedef struct
{
	RBSourceClass parent;
} RBLastfmSourceClass;

GType		rb_lastfm_source_get_type	(void);
GType           rb_lastfm_source_register_type    (GTypeModule *module);

RBSource *	rb_lastfm_source_new		(RBShell *shell);

void	lastfm_set_stream_URL 	(gchar URL);


G_END_DECLS

#endif /* __RB_LASTFM_SOURCE_H */
