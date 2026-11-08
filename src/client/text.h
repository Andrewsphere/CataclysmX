#pragma once

#include "core/server.h"
#include "content/content.h"

namespace cataclysmx {

TerminalAppearance CellAppearance(const ViewCell& cell, const ContentCatalog& content);
String render_text(const WorldView& view, const ContentCatalog& content);

}
