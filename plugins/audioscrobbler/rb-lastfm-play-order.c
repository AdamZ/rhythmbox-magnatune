/*
 *  Copyright (C) 2008  Jonathan Matthew  <jonathan@d14n.org>
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

#include "rb-lastfm-play-order.h"
#include "rb-debug.h"

static void rb_lastfm_play_order_class_init (RBLastfmPlayOrderClass *klass);

G_DEFINE_TYPE (RBLastfmPlayOrder, rb_lastfm_play_order, RB_TYPE_PLAY_ORDER)

RBPlayOrder *
rb_lastfm_play_order_new (RBShellPlayer *player)
{
	RBLastfmPlayOrder *lorder;

	lorder = g_object_new (RB_TYPE_LASTFM_PLAY_ORDER,
			       "player", player,
			       NULL);

	return RB_PLAY_ORDER (lorder);
}

static RhythmDBEntry *
rb_lastfm_play_order_get_next (RBPlayOrder *porder)
{
	RhythmDBQueryModel *model;
	RhythmDBEntry *entry;

	g_return_val_if_fail (porder != NULL, NULL);
	g_return_val_if_fail (RB_IS_LASTFM_PLAY_ORDER (porder), NULL);

	model = rb_play_order_get_query_model (porder);
	if (model == NULL)
		return NULL;

        entry = rb_play_order_get_playing_entry (porder);
	if (entry != NULL) {
		RhythmDBEntry *next;
		next = rhythmdb_query_model_get_next_from_entry (model, entry);
		rhythmdb_entry_unref (entry);
		return next;
	} else {
		GtkTreeIter iter;
		if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter))
			return NULL;
		return rhythmdb_query_model_iter_to_entry (model, &iter);
	}
}

static RhythmDBEntry *
rb_lastfm_play_order_get_previous (RBPlayOrder *porder)
{
	/* can never go back */
	return NULL;
}

static void
rb_lastfm_play_order_init (RBLastfmPlayOrder *porder)
{
}

static void
rb_lastfm_play_order_class_init (RBLastfmPlayOrderClass *klass)
{
	RBPlayOrderClass *porder = RB_PLAY_ORDER_CLASS (klass);
	porder->get_next = rb_lastfm_play_order_get_next;
	porder->get_previous = rb_lastfm_play_order_get_previous;
}

