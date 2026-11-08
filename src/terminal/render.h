#pragma once

#include "content/content.h"

#include <ftxui/dom/elements.hpp>

#include <string>

namespace cataclysmx {
ftxui::Element RenderTerrainCell(const TerminalAppearance& appearance);

std::string render_probe();
}
