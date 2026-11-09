#pragma once

#include "core/server.h"
#include "content/content.h"

namespace cataclysmx {
// Shared strict grammar for script and live command frontends.
// use <item-id> <capability-id> <local-action> [item-id|n/s/e/w]
// Action-ID syntax and generic argument shape only; the server checks the schema.
bool ParseItemCommand(eastl::string_view text, Command& command);
String ItemDetails(const WorldView& view, const ContentCatalog& content);

TerminalAppearance CellAppearance(const ViewCell& cell, const ContentCatalog& content);
String render_text(const WorldView& view, const ContentCatalog& content);

}
