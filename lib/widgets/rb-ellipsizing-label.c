/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* 
   arch-tag: Implementation of subclass of GtkLabel that ellipsizes the text.

   Copyright (C) 2001 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more priv.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: John Sullivan <sullivan@eazel.com>,
   	   Marco Pesenti Gritti <marco@it.gnome.org> Markup support
 */

#include "rb-ellipsizing-label.h"

#include <string.h>

struct RBEllipsizingLabelPrivate
{
	char *full_text;
	int real_width;
	int full_text_size;

	RBEllipsizeMode mode;
};

static void rb_ellipsizing_label_class_init (RBEllipsizingLabelClass *class);
static void rb_ellipsizing_label_init       (RBEllipsizingLabel      *label);

static GObjectClass *parent_class = NULL;

static int
rb_strcmp (const char *string_a, const char *string_b)
{
       return strcmp (string_a == NULL ? "" : string_a,
                      string_b == NULL ? "" : string_b);
}

static gboolean
rb_str_is_equal (const char *string_a, const char *string_b)
{
       return rb_strcmp (string_a, string_b) == 0;
}

#define ELLIPSIS "..."

/* Caution: this is an _expensive_ function */
static int
measure_string_width (const char  *string,
		      PangoLayout *layout,
		      gboolean markup)
{
	int width;

	if (markup)
	{
		pango_layout_set_markup (layout, string, -1);
	}
	else
	{
		pango_layout_set_text (layout, string, -1);
	}
	
	pango_layout_get_pixel_size (layout, &width, NULL);

	return width;
}

/* this is also plenty slow */
static void
compute_character_widths (const char    *string,
			  PangoLayout   *layout,
			  int           *char_len_return,
			  int          **widths_return,
			  int          **cuts_return,
			  gboolean       markup)
{
	int *widths;
	int *offsets;
	int *cuts;
	int char_len;
	int byte_len;
	const char *p;
	const char *nm_string;
	int i;
	PangoLayoutIter *iter;
	PangoLogAttr *attrs;
	
#define BEGINS_UTF8_CHAR(x) (((x) & 0xc0) != 0x80)

	if (markup)
	{
		pango_layout_set_markup (layout, string, -1);
	}
	else
	{
		pango_layout_set_text (layout, string, -1);
	}

	nm_string = pango_layout_get_text (layout);
	
	char_len = g_utf8_strlen (nm_string, -1);
	byte_len = strlen (nm_string);
	
	widths = g_new (int, char_len);
	offsets = g_new (int, byte_len);
	
	/* Create a translation table from byte index to char offset */
	p = nm_string;
	i = 0;
	while (*p) {
		int byte_index = p - nm_string;
		
		if (BEGINS_UTF8_CHAR (*p)) {
			offsets[byte_index] = i;
			++i;
		} else {
			offsets[byte_index] = G_MAXINT; /* segv if we try to use this */
		}
		
		++p;
	}

	/* Now fill in the widths array */		
	iter = pango_layout_get_iter (layout);

	do {
		PangoRectangle extents;
		int byte_index;

		byte_index = pango_layout_iter_get_index (iter);

		if (byte_index < byte_len) {
			pango_layout_iter_get_char_extents (iter, &extents);
			
			g_assert (BEGINS_UTF8_CHAR (nm_string[byte_index]));
			g_assert (offsets[byte_index] < char_len);
			
			widths[offsets[byte_index]] = PANGO_PIXELS (extents.width);
		}
		
	} while (pango_layout_iter_next_char (iter));

	pango_layout_iter_free (iter);

	g_free (offsets);
	
	*widths_return = widths;

	/* Now compute character offsets that are legitimate places to
	 * chop the string
	 */
	attrs = g_new (PangoLogAttr, char_len + 1);
	
	pango_get_log_attrs (nm_string, byte_len, -1,
			     pango_context_get_language (
				     pango_layout_get_context (layout)),
			     attrs,
			     char_len + 1);

	cuts = g_new (int, char_len);
	i = 0;
	while (i < char_len) {
		cuts[i] = attrs[i].is_cursor_position;

		++i;
	}

	g_free (attrs);

	*cuts_return = cuts;

	*char_len_return = char_len;
}

typedef struct
{
	GString *string;
	int start_offset;
	int end_offset;
	int position;
} EllipsizeStringData;

static void
start_element_handler (GMarkupParseContext *context,
                       const gchar         *element_name,
                       const gchar        **attribute_names,
                       const gchar        **attribute_values,
                       gpointer             user_data,
                       GError             **error)
{
	EllipsizeStringData *data = (EllipsizeStringData *)user_data;
	int i;
	
	g_string_append_c (data->string, '<');
	g_string_append (data->string, element_name);

	for (i = 0; attribute_names[i] != NULL; i++)
	{
		g_string_append_c (data->string, ' ');
		g_string_append (data->string, attribute_names[i]);
		g_string_append (data->string, "=\"");
		g_string_append (data->string, attribute_values[i]);
		g_string_append_c (data->string, '"');
	}

	g_string_append_c (data->string, '>');
}

static void
end_element_handler    (GMarkupParseContext *context,
                        const gchar         *element_name,
                        gpointer             user_data,
                        GError             **error)
{
	EllipsizeStringData *data = (EllipsizeStringData *)user_data;

	g_string_append (data->string, "</");
	g_string_append (data->string, element_name);
	g_string_append_c (data->string, '>');
}

static void
append_ellipsized_text (const char *text,
			EllipsizeStringData *data,
			int text_len,
			gboolean markup)
{
	int position;
	int new_position;
	char *escaped;
	
	position = data->position;
	new_position = data->position + text_len;
	
	if (position > data->start_offset &&
	    new_position < data->end_offset)
	{
		return;
	}
	else if ((position < data->start_offset &&
	          new_position < data->start_offset) ||
	         (position > data->end_offset &&
	          new_position > data->end_offset))
	{
		if (markup)
		{
			escaped = g_markup_escape_text (text, strlen (text));
			g_string_append (data->string, escaped);
			g_free (escaped);
		}
		else
			g_string_append (data->string, text);
	}
	else if (position <= data->start_offset &&
		 new_position >= data->end_offset)
	{
		if (position < data->start_offset)
		{
			if (markup)
			{
				escaped = g_markup_escape_text (text, data->start_offset - position);
				g_string_append (data->string, escaped);
				g_free (escaped);
			}
			else
				g_string_append_len (data->string, text, data->start_offset - position);
		}

		g_string_append (data->string, ELLIPSIS);
		
		if (new_position > data->end_offset)
		{
			if (markup)
			{
				escaped = g_markup_escape_text (text + data->end_offset - position,
								position + text_len - data->end_offset);
				g_string_append (data->string, escaped);
				g_free (escaped);
			}
			else
				g_string_append_len (data->string, text + data->end_offset - position,
						     position + text_len - data->end_offset);
		}
	}

	data->position = new_position;
}

static void
text_handler           (GMarkupParseContext *context,
                        const gchar         *text,
                        gsize                text_len,
                        gpointer             user_data,
                        GError             **error)
{
	EllipsizeStringData *data = (EllipsizeStringData *)user_data;

	append_ellipsized_text (text, data, text_len, TRUE);
}

static GMarkupParser pango_markup_parser = {
  start_element_handler,
  end_element_handler,
  text_handler,
  NULL,
  NULL
};

static char *
ellipsize_string (const char *string, 
		  int start_offset,
		  int end_offset,
		  gboolean markup)
{
	GString *str;
	EllipsizeStringData data;
	char *result;
	GMarkupParseContext *c;
	
	str = g_string_new (NULL);
	data.string = str;
	data.start_offset = start_offset;
	data.end_offset = end_offset;
	data.position = 0;

	if (markup)
	{
		c = g_markup_parse_context_new (&pango_markup_parser, 
					        0, &data, NULL);
		g_markup_parse_context_parse (c, string, -1, NULL);
		g_markup_parse_context_free (c);
	}
	else
	{
		append_ellipsized_text (string, &data,
					g_utf8_strlen (string, -1), FALSE);
	}
	
	result = str->str;
	g_string_free (str, FALSE);
	return result;
}

static char *
rb_string_ellipsize_start (const char *string, PangoLayout *layout, int width,
			   int *resulting_width, gboolean markup)
{
	int *cuts;
	int *widths;
	int char_len;
	int truncate_offset;
	int bytes_end;
	
	/* Zero-length string can't get shorter - catch this here to
	 * avoid expensive calculations
	 */
	if (*string == '\0')
		return g_strdup ("");

	/* I'm not sure if this short-circuit is a net win; it might be better
	 * to just dump this, and always do the compute_character_widths() etc.
	 * down below.
	 */
	*resulting_width = measure_string_width (string, layout, markup);

	if (*resulting_width <= width) {
		/* String is already short enough. */
		return g_strdup (string);
	}

	/* Remove width of an ellipsis */
	width -= measure_string_width (ELLIPSIS, layout, markup);

	if (width < 0) {
		/* No room even for an ellipsis. */
		return g_strdup ("");
	}

	/* Our algorithm involves removing enough chars from the string to bring
	 * the width to the required small size. However, due to ligatures,
	 * combining characters, etc., it's not guaranteed that the algorithm
	 * always works 100%. It's sort of a heuristic thing. It should work
	 * nearly all the time... but I wouldn't put in
	 * g_assert (width of resulting string < width).
	 *
	 * Hmm, another thing that this breaks with is explicit line breaks
	 * in "string"
	 */

	compute_character_widths (string, layout, &char_len, &widths, &cuts, markup);

        for (truncate_offset = 1; truncate_offset < char_len; truncate_offset++) {

        	*resulting_width -= widths[truncate_offset];

        	if (*resulting_width <= width &&
		    cuts[truncate_offset]) {
			break;
        	}
        }

	g_free (cuts);
	g_free (widths);
	
	bytes_end = g_utf8_offset_to_pointer (string, truncate_offset) - string;
	
	return ellipsize_string (string, 0, bytes_end, markup);
}

static char *
rb_string_ellipsize_end (const char *string, PangoLayout *layout, int width,
			 int *resulting_width, gboolean markup)
{
	int *cuts;
	int *widths;
	int char_len;
	int truncate_offset;
	int bytes_end;
	
	/* See explanatory comments in ellipsize_start */
	
	if (*string == '\0')
		return g_strdup ("");

	*resulting_width = measure_string_width (string, layout, markup);
	
	if (*resulting_width <= width) {
		return g_strdup (string);
	}

	width -= measure_string_width (ELLIPSIS, layout, markup);

	if (width < 0) {
		return g_strdup ("");
	}
	
	compute_character_widths (string, layout, &char_len, &widths, &cuts, markup);
	
        for (truncate_offset = char_len - 1; truncate_offset > 0; truncate_offset--) {
		*resulting_width -= widths[truncate_offset];
        	if (*resulting_width <= width &&
		    cuts[truncate_offset]) {
			break;
        	}
        }

	g_free (cuts);
	g_free (widths);

	bytes_end = g_utf8_offset_to_pointer (string, truncate_offset) - string;
	
	return ellipsize_string (string, bytes_end,
				 char_len, markup);
}

static char *
rb_string_ellipsize_middle (const char *string, PangoLayout *layout, int width,
			    int *resulting_width, gboolean markup)
{
	int *cuts;
	int *widths;
	int char_len;
	int starting_fragment_length;
	int ending_fragment_offset;
	int bytes_start;
	int bytes_end;
	
	/* See explanatory comments in ellipsize_start */
	
	if (*string == '\0')
		return g_strdup ("");

	*resulting_width = measure_string_width (string, layout, markup);
	
	if (*resulting_width <= width) {
		return g_strdup (string);
	}

	width -= measure_string_width (ELLIPSIS, layout, markup);

	if (width < 0) {
		return g_strdup ("");
	}
	
	compute_character_widths (string, layout, &char_len, &widths, &cuts, markup);
	
	starting_fragment_length = char_len / 2;
	ending_fragment_offset = starting_fragment_length + 1;
	
	/* depending on whether the original string length is odd or even, start by
	 * shaving off the characters from the starting or ending fragment
	 */
	if (char_len % 2) {
		goto shave_end;
	}

	while (starting_fragment_length > 0 || ending_fragment_offset < char_len) {
		if (*resulting_width <= width &&
		    cuts[ending_fragment_offset] &&
		    cuts[starting_fragment_length]) {
			break;
		}

		if (starting_fragment_length > 0) {
			*resulting_width -= widths[starting_fragment_length];
			starting_fragment_length--;
		}

	shave_end:
		if (*resulting_width <= width &&
		    cuts[ending_fragment_offset] &&
		    cuts[starting_fragment_length]) {
			break;
		}

		if (ending_fragment_offset < char_len) {
			*resulting_width -= widths[ending_fragment_offset];
			ending_fragment_offset++;
		}
	}

	g_free (cuts);
	g_free (widths);	

	bytes_start = g_utf8_offset_to_pointer (string, starting_fragment_length) - string;
	bytes_end = g_utf8_offset_to_pointer (string, ending_fragment_offset) - string;
	
	return ellipsize_string (string, bytes_start, bytes_end, markup);
}


/**
 * set_text_ellipsized
 *
 * @layout: a pango layout
 * @string: A a string to be ellipsized.
 * @width: Desired maximum width in points.
 * @resulting_width: The actual width used. 
 * @mode: The desired ellipsizing mode.
 * 
 * Truncates a string if required to fit in @width and sets it on the
 * layout. Truncation involves removing characters from the start, middle or end
 * respectively and replacing them with "...". Algorithm is a bit
 * fuzzy, won't work 100%.
 * 
 */
static gboolean
set_text_ellipsized (PangoLayout  *layout,
		     const char   *string,
		     int           width,
		     int	   *resulting_width,
		     RBEllipsizeMode mode,
		     gboolean markup)
{
	gboolean ret;
	char *s;

	g_return_val_if_fail (PANGO_IS_LAYOUT (layout), FALSE);
	g_return_val_if_fail (string != NULL, FALSE);
	g_return_val_if_fail (width >= 0, FALSE);
	
	switch (mode) {
	case RB_ELLIPSIZE_START:
		s = rb_string_ellipsize_start (string, layout, width,
					       resulting_width, markup);
		break;
	case RB_ELLIPSIZE_MIDDLE:
		s = rb_string_ellipsize_middle (string, layout, width,
						resulting_width, markup);
		break;
	case RB_ELLIPSIZE_END:
		s = rb_string_ellipsize_end (string, layout, width,
					     resulting_width, markup);
		break;
	default:
		g_return_val_if_reached (FALSE);
		s = NULL;
	}

	ret = (strcmp (s, string) != 0);

	if (markup)
	{
		pango_layout_set_markup (layout, s, -1);
	}
	else
	{
		pango_layout_set_text (layout, s, -1);
	}

	g_free (s);

	return ret;
}

GType 
rb_ellipsizing_label_get_type (void)
{
        static GType rb_ellipsizing_label_type = 0;

        if (rb_ellipsizing_label_type == 0)
        {
                static const GTypeInfo our_info =
                {
                        sizeof (RBEllipsizingLabelClass),
                        NULL, /* base_init */
                        NULL, /* base_finalize */
                        (GClassInitFunc) rb_ellipsizing_label_class_init,
                        NULL,
                        NULL, /* class_data */
                        sizeof (RBEllipsizingLabel),
                        0, /* n_preallocs */
                        (GInstanceInitFunc) rb_ellipsizing_label_init
                };

                rb_ellipsizing_label_type = g_type_register_static (GTK_TYPE_LABEL,
                                                             	     "RBEllipsizingLabel",
                                                                     &our_info, 0);
        }

        return rb_ellipsizing_label_type;
}

static void
rb_ellipsizing_label_init (RBEllipsizingLabel *label)
{
	label->priv = g_new0 (RBEllipsizingLabelPrivate, 1);

	label->priv->mode = -1;
}

static void
rb_ellipsizing_label_finalize (GObject *object)
{
	RBEllipsizingLabel *label;

	label = RB_ELLIPSIZING_LABEL (object);

	g_free (label->priv->full_text);
	g_free (label->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

GtkWidget*
rb_ellipsizing_label_new (const char *string)
{
	RBEllipsizingLabel *label;
  
	label = g_object_new (RB_TYPE_ELLIPSIZING_LABEL, NULL);
	rb_ellipsizing_label_set_text (label, string);
  
	return GTK_WIDGET (label);
}

void
rb_ellipsizing_label_set_text (RBEllipsizingLabel *label, 
				const char          *string)
{
	g_return_if_fail (RB_IS_ELLIPSIZING_LABEL (label));

	if (rb_str_is_equal (string, label->priv->full_text)) {
		return;
	}

	label->priv->full_text_size = -1;

	g_free (label->priv->full_text);
	label->priv->full_text = g_strdup (string);

	/* Queues a resize as side effect */
	gtk_label_set_text (GTK_LABEL (label), label->priv->full_text);
}

void
rb_ellipsizing_label_set_markup (RBEllipsizingLabel *label, 
				 const char          *string)
{
	g_return_if_fail (RB_IS_ELLIPSIZING_LABEL (label));

	if (rb_str_is_equal (string, label->priv->full_text)) {
		return;
	}

	label->priv->full_text_size = -1;

	g_free (label->priv->full_text);
	label->priv->full_text = g_strdup (string);

	/* Queues a resize as side effect */
	gtk_label_set_markup (GTK_LABEL (label), label->priv->full_text);
}

void
rb_ellipsizing_label_set_mode (RBEllipsizingLabel *label,
		               RBEllipsizeMode mode)
{
	g_return_if_fail (RB_IS_ELLIPSIZING_LABEL (label));

	label->priv->mode = mode;
}

int
rb_ellipsizing_label_get_width (RBEllipsizingLabel *label)
{
	return label->priv->real_width;
}

static void
real_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
	GTK_WIDGET_CLASS (parent_class)->size_request (widget, requisition);

	/* Don't demand any particular width; will draw ellipsized into whatever size we're given */
	requisition->width = 0;
}

static void	
real_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	RBEllipsizingLabel *label;
	gboolean markup;
	PangoLayout *layout;
	
	markup = gtk_label_get_use_markup (GTK_LABEL (widget));
	
	label = RB_ELLIPSIZING_LABEL (widget);
	
	/* This is the bad hack of the century, using private
	 * GtkLabel layout object. If the layout is NULL
	 * then it got blown away since size request,
	 * we just punt in that case, I don't know what to do really.
	 */

	layout = gtk_label_get_layout (GTK_LABEL (label));

	g_return_if_fail (layout != NULL);

	if (label->priv->full_text == NULL) {
		pango_layout_set_text (GTK_LABEL (label)->layout, "", -1);
	} else {
		RBEllipsizeMode mode;

		if (label->priv->mode != -1)
			mode = label->priv->mode;

		if (ABS (GTK_MISC (label)->xalign - 0.5) < 1e-12)
			mode = RB_ELLIPSIZE_MIDDLE;
		else if (GTK_MISC (label)->xalign < 0.5)
			mode = RB_ELLIPSIZE_END;
		else
			mode = RB_ELLIPSIZE_START;

		label->ellipsized = set_text_ellipsized (GTK_LABEL (label)->layout,
							 label->priv->full_text,
							 allocation->width,
							 &label->priv->real_width,
							 mode,
							 markup);

		gtk_widget_queue_draw (GTK_WIDGET (label));
	}

	GTK_WIDGET_CLASS (parent_class)->size_allocate (widget, allocation);
}

static gboolean
real_expose_event (GtkWidget *widget, GdkEventExpose *event)
{
	RBEllipsizingLabel *label;
	GtkRequisition req;
	
	label = RB_ELLIPSIZING_LABEL (widget);

	/* push/pop the actual size so expose draws in the right
	 * place, yes this is bad hack central. Here we assume the
	 * ellipsized text has been set on the layout in size_allocate
	 */
	GTK_WIDGET_CLASS (parent_class)->size_request (widget, &req);
	widget->requisition.width = label->priv->real_width;
	GTK_WIDGET_CLASS (parent_class)->expose_event (widget, event);
	widget->requisition.width = 0;

	return FALSE;
}


static void
rb_ellipsizing_label_class_init (RBEllipsizingLabelClass *klass)
{
	GtkWidgetClass *widget_class;

	parent_class = g_type_class_peek_parent (klass);
	
	widget_class = GTK_WIDGET_CLASS (klass);

	G_OBJECT_CLASS (klass)->finalize = rb_ellipsizing_label_finalize;

	widget_class->size_request = real_size_request;
	widget_class->size_allocate = real_size_allocate;
	widget_class->expose_event = real_expose_event;
}

gboolean
rb_ellipsizing_label_get_ellipsized (RBEllipsizingLabel *label)
{
	g_return_val_if_fail (RB_IS_ELLIPSIZING_LABEL (label), FALSE);

	return label->ellipsized;
}

int
rb_ellipsizing_label_get_full_text_size (RBEllipsizingLabel *label)
{
	if (label->priv->full_text_size == -1)
		label->priv->full_text_size = measure_string_width (
				label->priv->full_text,
				GTK_LABEL (label)->layout,
				gtk_label_get_use_markup (GTK_LABEL (label)));

	return label->priv->full_text_size;
}

#ifdef RB_ELLIPSIZING_LABEL_TEST

#include <gtk/gtk.h>

static void
entry_changed_cb (GtkEntry *entry, RBEllipsizingLabel *label)
{
	rb_ellipsizing_label_set_text (RB_ELLIPSIZING_LABEL (label),
				       gtk_entry_get_text (entry));
}

int
main (int argc, char **argv)
{
	GtkWidget *toplevel, *vbox, *hbox, *hbox2, *entry, *entry2;
	GtkWidget *pre_label, *label, *label2, *label3, *mid_label;

	gtk_init (&argc, &argv);

	toplevel = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (toplevel), 250, 100);

	vbox = gtk_vbox_new (FALSE, 0);
	hbox = gtk_hbox_new (FALSE, 0);
	hbox2 = gtk_hbox_new (FALSE, 0);

	pre_label = gtk_label_new ("From ");
	label = rb_ellipsizing_label_new ("");
	rb_ellipsizing_label_set_mode (RB_ELLIPSIZING_LABEL (label), RB_ELLIPSIZE_END);
	mid_label = gtk_label_new (" by ");
	label2 = rb_ellipsizing_label_new ("");
	rb_ellipsizing_label_set_mode (RB_ELLIPSIZING_LABEL (label), RB_ELLIPSIZE_END);
	label3 = rb_ellipsizing_label_new ("");
	rb_ellipsizing_label_set_mode (RB_ELLIPSIZING_LABEL (label), RB_ELLIPSIZE_END);
	
	entry = gtk_entry_new ();
	g_signal_connect (G_OBJECT (entry), "changed",
			  G_CALLBACK (entry_changed_cb), label);
	g_signal_connect (G_OBJECT (entry), "changed",
			  G_CALLBACK (entry_changed_cb), label3);
	entry2 = gtk_entry_new ();
	g_signal_connect (G_OBJECT (entry2), "changed",
			  G_CALLBACK (entry_changed_cb), label2);
	
	gtk_box_pack_start_defaults (GTK_BOX (vbox), entry);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), entry2);
	gtk_box_pack_start (GTK_BOX (vbox), label3, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), pre_label, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), mid_label, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), label2, TRUE, TRUE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (vbox), hbox);
	gtk_container_add (GTK_CONTAINER (toplevel), vbox);

	gtk_widget_show_all (toplevel);

	gtk_main ();
}
#endif
