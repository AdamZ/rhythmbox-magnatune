/*
 *  arch-tag: Header for totally random functions that didn't fit elsewhere
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_UTIL_H
#define __RB_UTIL_H

#include <stdarg.h>
#include <glib.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkuimanager.h>

G_BEGIN_DECLS

gboolean rb_true_function (gpointer dummy);
gboolean rb_false_function (gpointer dummy);
gpointer rb_null_function (gpointer dummy);
gpointer rb_copy_function (gpointer data);

int rb_gvalue_compare (GValue *a, GValue *b);

int rb_compare_gtimeval (GTimeVal *a, GTimeVal *b);
int rb_safe_strcmp (const char *a, const char *b);
char *rb_make_duration_string (guint duration);
char *rb_make_elapsed_time_string (guint elapsed, guint duration, gboolean show_remaining);

void rb_gtk_action_popup_menu (GtkUIManager *uimanager, const char *path);

GtkWidget *rb_image_new_from_stock (const gchar *stock_id, GtkIconSize size);

gchar *rb_uri_get_mount_point (const char *uri);
gboolean rb_uri_is_mounted (const char *uri);


void rb_threads_init (void);
gboolean rb_is_main_thread (void);

gchar* rb_search_fold (const char *original);
gchar** rb_string_split_words (const gchar *string);

gboolean rb_string_list_equal (GList *a, GList *b);
gboolean rb_string_list_contains (GList *list, const char *s);
void rb_list_deep_free (GList *list);
void rb_slist_deep_free (GSList *list);
GList* rb_string_list_copy (GList *list);

gboolean rb_str_in_strv (const char *needle, char **haystack);

GList* rb_collate_hash_table_keys (GHashTable *table);
GList* rb_collate_hash_table_values (GHashTable *table);

GList* rb_uri_list_parse (const char *uri_list);
const gchar* rb_mime_get_friendly_name (const gchar *mime_type);

gboolean rb_signal_accumulator_object_handled (GSignalInvocationHint *hint,
					       GValue *return_accu,
					       const GValue *handler_return,
					       gpointer dummy);
void rb_value_array_append_data (GValueArray *array, GType type, ...);
void rb_value_free (GValue *val); /* g_value_unset, g_slice_free */

void rb_assert_locked (GMutex *mutex);

G_END_DECLS

#endif /* __RB_UTIL_H */
