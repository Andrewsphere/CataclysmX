#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cataclysmx::editor {

namespace fs = std::filesystem;

struct SourceEdit {
    std::string path;
    std::optional<std::string> before;
    std::string text;
};

bool RelativePath(std::string_view path);
bool EditablePath(std::string_view path, std::string_view tilesetDirectory);
bool ReadFile(const fs::path& path, std::string& text);
std::string PathText(const fs::path& path);

// All checkout paths are traversed without following symlinks.
bool SafePath(const fs::path& root, std::string_view path);
bool CopySources(const fs::path& root, const fs::path& candidate,
                 std::string_view tilesetDirectory, std::string& error);
bool WriteCandidate(const fs::path& candidate, std::span<const SourceEdit> edits, std::string& error);
bool MatchesCheckout(const fs::path& root, std::span<const SourceEdit> edits, std::string& error);

// The launcher holds the checkout lock while invoking these operations.
bool RecoverSave(const fs::path& root, std::string& error);
bool SaveSources(const fs::path& root, std::span<const SourceEdit> edits, std::string& error);

}
