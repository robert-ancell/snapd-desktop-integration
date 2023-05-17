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

#include <libnotify/notify.h>

#include "sdi-application.h"
#include "sdi-refresh-monitor.h"
#include "sdi-theme-monitor.h"

struct _SdiApplication {
  GtkApplication parent_instance;

  SnapdClient *snapd_client;
  SdiThemeMonitor *theme_monitor;
  SdiRefreshMonitor *refresh_monitor;
};

G_DEFINE_TYPE(SdiApplication, sdi_application, GTK_TYPE_APPLICATION)

static GOptionEntry entries[] = {{"snapd-socket-path", 0, 0,
                                  G_OPTION_ARG_FILENAME, NULL,
                                  "Snapd socket path", "PATH"},
                                 {NULL}};

static void sdi_application_dispose(GObject *object) {
  SdiApplication *self = SDI_APPLICATION(object);

  g_clear_object(&self->snapd_client);
  g_clear_object(&self->theme_monitor);
  g_clear_object(&self->refresh_monitor);

  G_OBJECT_CLASS(sdi_application_parent_class)->dispose(object);
}

static gint command_line(GApplication *application,
                         GApplicationCommandLine *command_line) {
  g_application_activate(G_APPLICATION(application));

  return -1;
}

static gint handle_local_options(GApplication *application,
                                 GVariantDict *options) {
  SdiApplication *self = SDI_APPLICATION(application);

  const gchar *snapd_socket_path = NULL;
  g_variant_dict_lookup(options, "snapd-socket-path", "&s", &snapd_socket_path);
  printf("%p\n", snapd_socket_path);
  if (snapd_socket_path != NULL) {
    snapd_client_set_socket_path(self->snapd_client, snapd_socket_path);
  } else if (g_getenv("SNAP") != NULL) {
    snapd_client_set_socket_path(self->snapd_client, "/run/snapd-snap.socket");
  }

  return -1;
}

static void startup(GApplication *application) {
  SdiApplication *self = SDI_APPLICATION(application);

  G_APPLICATION_CLASS(sdi_application_parent_class)->startup(application);

  notify_init("snapd-desktop-integration");
  if (!sdi_refresh_monitor_start(self->refresh_monitor,
                                 g_application_get_dbus_connection(application),
                                 NULL)) {
    g_message("Failed to export the DBus Desktop Integration API");
  }
}

static void activate(GApplication *application) {
  SdiApplication *self = SDI_APPLICATION(application);

  // because, by default, there are no windows, so the application would quit
  g_application_hold(application);

  sdi_theme_monitor_start(self->theme_monitor);
}

static void shutdown(GApplication *application) {
  notify_uninit();
  G_APPLICATION_CLASS(sdi_application_parent_class)->shutdown(application);
}

static void sdi_application_init(SdiApplication *self) {
  self->snapd_client = snapd_client_new();
  self->theme_monitor = sdi_theme_monitor_new(self->snapd_client);
  self->refresh_monitor = sdi_refresh_monitor_new();

  g_application_add_main_option_entries(G_APPLICATION(self), entries);
  g_application_set_flags(G_APPLICATION(self),
                          G_APPLICATION_ALLOW_REPLACEMENT |
                              G_APPLICATION_REPLACE |
                              G_APPLICATION_HANDLES_COMMAND_LINE);
}

static void sdi_application_class_init(SdiApplicationClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = sdi_application_dispose;
  G_APPLICATION_CLASS(klass)->command_line = command_line;
  G_APPLICATION_CLASS(klass)->handle_local_options = handle_local_options;
  G_APPLICATION_CLASS(klass)->startup = startup;
  G_APPLICATION_CLASS(klass)->shutdown = shutdown;
  G_APPLICATION_CLASS(klass)->activate = activate;
}

SdiApplication *sdi_application_new() {
  return g_object_new(sdi_application_get_type(), "application-id",
                      "io.snapcraft.SnapDesktopIntegration", NULL);
}
