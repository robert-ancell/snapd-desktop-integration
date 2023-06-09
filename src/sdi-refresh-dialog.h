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

#pragma once

#include <gtk/gtk.h>

typedef struct {
  gchar *app_name;
  GtkApplicationWindow *window;
  GtkProgressBar *progress_bar;
  GtkLabel *message;
  GtkWidget *icon;
  gchar *lock_file;
  guint timeout_id;
  guint close_id;
  gboolean pulsed;
  gboolean wait_change_in_lock_file;
} SdiRefreshDialog;

void handle_application_is_being_refreshed(const gchar *app_name,
                                           const gchar *lock_file_path,
                                           GVariant *extra_params,
                                           SdiRefreshMonitor *monitor);
void handle_application_refresh_completed(const gchar *app_name,
                                          GVariant *extra_params,
                                          SdiRefreshMonitor *monitor);
void handle_set_pulsed_progress(const gchar *app_name, const gchar *bar_text,
                                GVariant *extra_params,
                                SdiRefreshMonitor *monitor);
void handle_set_percentage_progress(const gchar *app_name,
                                    const gchar *bar_text, gdouble percentage,
                                    GVariant *extra_params,
                                    SdiRefreshMonitor *monitor);
SdiRefreshDialog *sdi_refresh_dialog_new(const gchar *app_name);

void sdi_refresh_dialog_free(SdiRefreshDialog *state);
