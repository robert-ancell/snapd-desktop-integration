/*
 * Copyright (C) 2020-2022 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "refresh_status.h"
#include "iresources.h"
#include <cairo.h>
#include <errno.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct _SdiRefreshState {
  GObject parent_instance;

  DsState *ds_state;
  gchar *app_name;
  GtkApplicationWindow *window;
  GtkWidget *progress_bar;
  GtkLabel *message;
  GtkWidget *icon;
  gchar *lock_file;
  guint timeout_id;
  guint close_id;
  gboolean pulsed;
  gboolean wait_change_in_lock_file;
};

G_DEFINE_TYPE(SdiRefreshState, sdi_refresh_state, G_TYPE_OBJECT)

static void sdi_refresh_state_dispose(GObject *object) {
  SdiRefreshState *self = SDI_REFRESH_STATE(object);

  if (self->timeout_id != 0) {
    g_source_remove(self->timeout_id);
  }
  if (self->close_id != 0) {
    g_signal_handler_disconnect(G_OBJECT(self->window), self->close_id);
  }
  g_free(self->lock_file);
  g_free(self->app_name);
  if (self->window != NULL) {
    gtk_window_destroy(GTK_WINDOW(self->window));
  }

  G_OBJECT_CLASS(sdi_refresh_state_parent_class)->dispose(object);
}

static gboolean on_close_window(GtkWindow *self, SdiRefreshState *state) {
  refresh_state_free(state);
  return TRUE;
}

static void on_hide_clicked(GtkButton *button, SdiRefreshState *state) {
  refresh_state_free(state);
}

static gboolean refresh_progress_bar(SdiRefreshState *state) {
  struct stat statbuf;
  if (state->pulsed) {
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(state->progress_bar));
  }
  if (state->lock_file == NULL) {
    return G_SOURCE_CONTINUE;
  }
  if (stat(state->lock_file, &statbuf) != 0) {
    if ((errno == ENOENT) || (errno == ENOTDIR)) {
      if (state->wait_change_in_lock_file) {
        return G_SOURCE_CONTINUE;
      }
      refresh_state_free(state);
      return G_SOURCE_REMOVE;
    }
  } else {
    if (statbuf.st_size == 0) {
      if (state->wait_change_in_lock_file) {
        return G_SOURCE_CONTINUE;
      }
      refresh_state_free(state);
      return G_SOURCE_REMOVE;
    }
  }
  // if we arrive here, we wait for the lock file to be empty
  state->wait_change_in_lock_file = FALSE;
  return G_SOURCE_CONTINUE;
}

static SdiRefreshState *find_application(DsState *ds_state,
                                         const char *app_name) {
  for (guint i = 0; i < ds_state->refreshing_list->len; i++) {
    SdiRefreshState *state = g_ptr_array_index(ds_state->refreshing_list, i);
    if (0 == g_strcmp0(state->app_name, app_name)) {
      return state;
    }
  }
  return NULL;
}

static void set_message(SdiRefreshState *state, const gchar *message) {
  if (message == NULL)
    return;
  gtk_label_set_text(state->message, message);
}

static void set_title(SdiRefreshState *state, const gchar *title) {
  if (title == NULL)
    return;
  gtk_window_set_title(GTK_WINDOW(state->window), title);
}

static void set_icon(SdiRefreshState *state, const gchar *icon) {
  if (icon == NULL)
    return;
  if (strlen(icon) == 0) {
    gtk_widget_set_visible(state->icon, FALSE);
    return;
  }
  gtk_image_set_from_icon_name(GTK_IMAGE(state->icon), icon);
  gtk_widget_set_visible(state->icon, TRUE);
}

static void set_icon_image(SdiRefreshState *state, const gchar *path) {
  g_autoptr(GFile) fimage = NULL;
  g_autoptr(GdkPixbuf) image = NULL;
  g_autoptr(GdkPixbuf) final_image = NULL;
  gint scale;

  if (path == NULL)
    return;
  if (strlen(path) == 0) {
    gtk_widget_set_visible(state->icon, FALSE);
    return;
  }
  fimage = g_file_new_for_path(path);
  if (!g_file_query_exists(fimage, NULL)) {
    gtk_widget_set_visible(state->icon, FALSE);
    return;
  }
  // This convoluted code is needed to be able to scale
  // any picture to the desired size, and also to allow
  // to set the scale and take advantage of the monitor
  // scale.
  image = gdk_pixbuf_new_from_file(path, NULL);
  if (image == NULL) {
    gtk_widget_set_visible(state->icon, FALSE);
    return;
  }
  scale = gtk_widget_get_scale_factor(GTK_WIDGET(state->icon));
  final_image = gdk_pixbuf_scale_simple(image, ICON_SIZE * scale,
                                        ICON_SIZE * scale, GDK_INTERP_BILINEAR);
  gtk_image_set_from_pixbuf(GTK_IMAGE(state->icon), final_image);
  gtk_widget_set_visible(state->icon, TRUE);
}

static void set_desktop_file(SdiRefreshState *state, const gchar *path) {
  g_autoptr(GDesktopAppInfo) app_info = NULL;
  g_autofree gchar *icon = NULL;

  if (path == NULL)
    return;

  if (strlen(path) == 0)
    return;

  app_info = g_desktop_app_info_new_from_filename(path);
  if (app_info == NULL) {
    return;
  }
  // extract the icon from the desktop file
  icon = g_desktop_app_info_get_string(app_info, "Icon");
  if (icon != NULL)
    set_icon_image(state, icon);
}

static void handle_extra_params(SdiRefreshState *state,
                                GVariant *extra_params) {
  GVariantIter iter;
  GVariant *value;
  gchar *key;

  // Do a copy to allow manage the iter in other places if needed
  g_variant_iter_init(&iter, extra_params);
  while (g_variant_iter_next(&iter, "{sv}", &key, &value)) {
    if (!g_strcmp0(key, "message")) {
      set_message(state, g_variant_get_string(value, NULL));
    } else if (!g_strcmp0(key, "title")) {
      set_title(state, g_variant_get_string(value, NULL));
    } else if (!g_strcmp0(key, "icon")) {
      set_icon(state, g_variant_get_string(value, NULL));
    } else if (!g_strcmp0(key, "icon_image")) {
      set_icon_image(state, g_variant_get_string(value, NULL));
    } else if (!g_strcmp0(key, "wait_change_in_lock_file")) {
      state->wait_change_in_lock_file = TRUE;
    } else if (!g_strcmp0(key, "desktop_file")) {
      set_desktop_file(state, g_variant_get_string(value, NULL));
    }
    g_variant_unref(value);
    g_free(key);
  }
}

void sdi_refresh_state_init(SdiRefreshState *self) {}

void sdi_refresh_state_class_init(SdiRefreshStateClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = sdi_refresh_state_dispose;
}

void handle_application_is_being_refreshed(const gchar *app_name,
                                           const gchar *lock_file_path,
                                           GVariant *extra_params,
                                           DsState *ds_state) {
  SdiRefreshState *state = NULL;
  g_autofree gchar *label_text = NULL;
  g_autoptr(GtkBuilder) builder = NULL;
  GtkButton *button;

  state = find_application(ds_state, app_name);
  if (state != NULL) {
    gtk_window_present(GTK_WINDOW(state->window));
    handle_extra_params(state, extra_params);
    return;
  }

  state = sdi_refresh_state_new(ds_state, app_name);
  if (*lock_file_path == 0) {
    state->lock_file = NULL;
  } else {
    state->lock_file = g_strdup(lock_file_path);
  }
  state->wait_change_in_lock_file = FALSE;
  builder = gtk_builder_new_from_resource(
      "/io/snapcraft/SnapDesktopIntegration/snap_is_being_refreshed_gtk4.ui");
  state->window =
      GTK_APPLICATION_WINDOW(gtk_builder_get_object(builder, "main_window"));
  state->message = GTK_LABEL(gtk_builder_get_object(builder, "app_label"));
  state->progress_bar =
      GTK_WIDGET(gtk_builder_get_object(builder, "progress_bar"));
  state->icon = GTK_WIDGET(gtk_builder_get_object(builder, "app_icon"));
  button = GTK_BUTTON(gtk_builder_get_object(builder, "button_hide"));
  label_text = g_strdup_printf(
      _("Refreshing “%s” to latest version. Please wait."), app_name);
  gtk_label_set_text(state->message, label_text);

  g_signal_connect(G_OBJECT(state->window), "close-request",
                   G_CALLBACK(on_close_window), state);
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(on_hide_clicked),
                   state);

  gtk_widget_set_visible(state->icon, FALSE);

  state->timeout_id =
      g_timeout_add(200, G_SOURCE_FUNC(refresh_progress_bar), state);
  gtk_window_present(GTK_WINDOW(state->window));
  g_ptr_array_add(ds_state->refreshing_list, state);
  handle_extra_params(state, extra_params);
}

void handle_close_application_window(const gchar *app_name,
                                     GVariant *extra_params,
                                     DsState *ds_state) {
  SdiRefreshState *state = NULL;

  state = find_application(ds_state, app_name);
  if (state == NULL) {
    return;
  }
  refresh_state_free(state);
}

void handle_set_pulsed_progress(const gchar *app_name, const gchar *bar_text,
                                GVariant *extra_params, DsState *ds_state) {
  SdiRefreshState *state = NULL;

  state = find_application(ds_state, app_name);
  if (state == NULL) {
    return;
  }
  state->pulsed = TRUE;
  if ((bar_text == NULL) || (bar_text[0] == 0)) {
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state->progress_bar),
                                   FALSE);
  } else {
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state->progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(state->progress_bar), bar_text);
  }
  handle_extra_params(state, extra_params);
}

void handle_set_percentage_progress(const gchar *app_name,
                                    const gchar *bar_text, gdouble percent,
                                    GVariant *extra_params, DsState *ds_state) {
  SdiRefreshState *state = NULL;

  state = find_application(ds_state, app_name);
  if (state == NULL) {
    return;
  }
  state->pulsed = FALSE;
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress_bar), percent);
  gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state->progress_bar), TRUE);
  if ((bar_text != NULL) && (bar_text[0] == 0)) {
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(state->progress_bar), NULL);
  } else {
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(state->progress_bar), bar_text);
  }
  handle_extra_params(state, extra_params);
}

SdiRefreshState *sdi_refresh_state_new(DsState *ds_state,
                                       const gchar *app_name) {
  SdiRefreshState *self = g_object_new(sdi_refresh_state_get_type(), NULL);
  self->app_name = g_strdup(app_name);
  self->ds_state = ds_state;
  self->pulsed = TRUE;
  return self;
}
