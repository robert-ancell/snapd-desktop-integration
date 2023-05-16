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

#pragma once

#include <gtk/gtk.h>

#include "io.snapcraft.PromptAgent.h"

G_DECLARE_FINAL_TYPE(SdiApparmorPromptDialog, sdi_apparmor_prompt_dialog, SDI,
                     APPARMOR_PROMPT_DIALOG, GtkWindow)

SdiApparmorPromptDialog *
sdi_apparmor_prompt_dialog_new(PromptAgent *prompt_agent,
                               GDBusMethodInvocation *invocation,
                               const gchar *path, GVariant *info);
