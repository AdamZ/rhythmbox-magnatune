/*
 *  Copyright (C) 2002  Ricardo Fern�ndez Pascual
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __gul_toolbar_item_factory_h
#define __gul_toolbar_item_factory_h

#include "gul-toolbar-item.h"

GulTbItem *	gul_toolbar_item_create_from_string	(const gchar *str);
GSList *	gul_toolbar_list_item_types		(void);

#endif
