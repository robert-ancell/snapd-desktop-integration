/*
 * Copyright (C) 2023 Canonical Ltd
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

#include "sdi-apparmor-prompt-agent.h"
#include "io.snapcraft.AppArmorPrompt.h"
#include "io.snapcraft.PromptAgent.h"
#include "sdi-apparmor-prompt-dialog.h"

struct _SdiApparmorPromptAgent {
  GObject parent_instance;

  PromptAgent *prompt_agent;
};

G_DEFINE_TYPE(SdiApparmorPromptAgent, sdi_apparmor_prompt_agent, G_TYPE_OBJECT)

#define PROMPT_SERVER_BUS_NAME "io.snapcraft.AppArmorPrompt"
#define PROMPT_SERVER_PATH "/io/snapcraft/AppArmorPrompt"

#define PROMPT_AGENT_PATH "/io/snapcraft/PromptAgent"

static void prompt_server_appeared_cb(GDBusConnection *connection,
                                      const gchar *name,
                                      const gchar *name_owner,
                                      gpointer user_data) {

  g_autoptr(GError) error = NULL;
  g_autoptr(AppArmorPrompt) proxy =
      app_armor_prompt_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, name,
                                      PROMPT_SERVER_PATH, NULL, &error);
  if (proxy == NULL) {
    g_warning("Failed to create AppArmor prompt proxy: %s", error->message);
    return;
  }

  if (!app_armor_prompt_call_register_agent_sync(proxy, PROMPT_AGENT_PATH, NULL,
                                                 &error)) {
    g_warning("Failed to register AppArmor prompt agent: %s", error->message);
    return;
  }
}

static gboolean handle_prompt(PromptAgent *prompt_agent,
                              GDBusMethodInvocation *invocation,
                              const gchar *path, GVariant *info,
                              gpointer user_data) {
  SdiApparmorPromptDialog *dialog =
      sdi_apparmor_prompt_dialog_new(prompt_agent, invocation, path, info);
  gtk_window_present(GTK_WINDOW(dialog));

  return TRUE;
}

static void sdi_apparmor_prompt_agent_dispose(GObject *object) {
  SdiApparmorPromptAgent *self = SDI_APPARMOR_PROMPT_AGENT(object);

  g_clear_object(&self->prompt_agent);

  G_OBJECT_CLASS(sdi_apparmor_prompt_agent_parent_class)->dispose(object);
}

void sdi_apparmor_prompt_agent_init(SdiApparmorPromptAgent *self) {
  self->prompt_agent = prompt_agent_skeleton_new();
  g_signal_connect(self->prompt_agent, "handle_prompt",
                   G_CALLBACK(handle_prompt), NULL);
}

void sdi_apparmor_prompt_agent_class_init(SdiApparmorPromptAgentClass *klass) {
  G_OBJECT_CLASS(klass)->dispose = sdi_apparmor_prompt_agent_dispose;
}

SdiApparmorPromptAgent *sdi_apparmor_prompt_agent_new() {
  return g_object_new(sdi_apparmor_prompt_agent_get_type(), NULL);
}

gboolean sdi_apparmor_prompt_agent_start(SdiApparmorPromptAgent *self,
                                         GError **error) {
  g_autoptr(GDBusConnection) session_bus =
      g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
  if (!g_dbus_interface_skeleton_export(
          G_DBUS_INTERFACE_SKELETON(self->prompt_agent), session_bus,
          PROMPT_AGENT_PATH, error)) {
    return FALSE;
  }

  g_autoptr(GDBusConnection) system_bus =
      g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
  g_bus_watch_name_on_connection(system_bus, PROMPT_SERVER_BUS_NAME,
                                 G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
                                 prompt_server_appeared_cb, NULL, NULL, NULL);

  return TRUE;
}
