/*
 *  Copyright (C) 2006  Jonathan Matthew  <jonathan@kaolin.wh9.net>
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

#ifndef __RB_IMPORT_ERRORS_SOURCE_H
#define __RB_IMPORT_ERRORS_SOURCE_H

#include "rb-shell.h"
#include "rb-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_IMPORT_ERRORS_SOURCE         (rb_import_errors_source_get_type ())
#define RB_IMPORT_ERRORS_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IMPORT_ERRORS_SOURCE, RBImportErrorsSource))
#define RB_IMPORT_ERRORS_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IMPORT_ERRORS_SOURCE, RBImportErrorsSourceClass))
#define RB_IS_IMPORT_ERRORS_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IMPORT_ERRORS_SOURCE))
#define RB_IS_IMPORT_ERRORS_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IMPORT_ERRORS_SOURCE))
#define RB_IMPORT_ERRORS_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IMPORT_ERRORS_SOURCE, RBImportErrorsSourceClass))

typedef struct _RBImportErrorsSource RBImportErrorsSource;
typedef struct _RBImportErrorsSourceClass RBImportErrorsSourceClass;
typedef struct _RBImportErrorsSourcePrivate RBImportErrorsSourcePrivate;

struct _RBImportErrorsSource
{
	RBSource parent;

	RBImportErrorsSourcePrivate *priv;
};

struct _RBImportErrorsSourceClass
{
	RBSourceClass parent;
};

GType		rb_import_errors_source_get_type		(void);

RBSource *      rb_import_errors_source_new			(RBShell *shell,
								 RhythmDBEntryType entry_type);

G_END_DECLS

#endif /* __RB_IMPORT_ERRORS_SOURCE_H */
