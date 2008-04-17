/* 
 * Copyright (C) 2003 Ross Burton <ross@burtonini.com>
 *
 * Sound Juicer - sj-structures.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Rhythmbox authors hereby grants permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: Ross Burton <ross@burtonini.com>
 */

#include "config.h"

#include "sj-structures.h"
#include <glib/gmessages.h>
#include <glib/glist.h>

/**
 * Free a TrackDetails*
 */
void track_details_free(TrackDetails *track)
{
  g_return_if_fail (track != NULL);
  g_free (track->title);
  g_free (track->artist);
  g_free (track->track_id);
  g_free (track->artist_id);
  g_free (track->artist_sortname);
  g_free (track);
}

/**
 * Free a AlbumDetails*
 */
void album_details_free(AlbumDetails *album)
{
  g_return_if_fail (album != NULL);
  g_free (album->title);
  g_free (album->artist);
  g_free (album->genre);
  g_free (album->album_id);
  if (album->release_date) g_date_free (album->release_date);
  g_list_foreach (album->tracks, (GFunc)track_details_free, NULL);
  g_list_free (album->tracks);
  g_free (album->artist_sortname);
  g_free (album);
}
