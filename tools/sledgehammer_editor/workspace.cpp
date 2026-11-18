#include "workspace.h"

#include <fstream>
#include <system_error>

namespace cataclysmx::editor {
namespace {

constexpr std::size_t maxSourceBytes = 16 * 1024 * 1024;
const fs::path journalPath = fs::path("out") / "worldgen-editor" / "pending";
const fs::path preparedPath = fs::path("out") / "worldgen-editor" / "prepared";
const fs::path finishedPath = fs::path("out") / "worldgen-editor" / "finished";

bool Fail(std::string& error, std::string message) {
    error = std::move(message);
    return false;
}

bool Status(const fs::path& path, fs::file_status& status) {
    std::error_code error;
    status = fs::symlink_status(path, error);
    return !error || error == std::errc::no_such_file_or_directory;
}

bool Exists(const fs::path& path, bool& exists) {
    fs::file_status status;
    if (!Status(path, status)) { return false; }
    exists = fs::exists(status);
    return !exists || fs::is_regular_file(status);
}

bool WriteFile(const fs::path& path, std::string_view text) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) { return false; }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return file.good();
}

bool CopyTree(const fs::path& source, const fs::path& destination, std::string& error) {
    std::error_code ec;
    fs::recursive_directory_iterator it(source, ec), end;
    while (!ec && it != end) {
        const auto status = it->symlink_status(ec);
        if (ec) { break; }
        const auto target = destination / it->path().lexically_relative(source);
        if (fs::is_directory(status)) {
            fs::create_directories(target, ec);
        } else if (fs::is_regular_file(status)) {
            fs::create_directories(target.parent_path(), ec);
            if (!ec) { fs::copy_file(it->path(), target, ec); }
        } else {
            return Fail(error, "Unsupported source entry: " + PathText(it->path()));
        }
        if (!ec) { it.increment(ec); }
    }
    if (ec) { return Fail(error, "Could not copy " + PathText(source) + ": " + ec.message()); }
    return true;
}

bool CloseJournal(const fs::path& root, std::string& error) {
    std::error_code ec;
    fs::rename(root / journalPath, root / finishedPath, ec);
    if (ec) { return Fail(error, "Could not close save recovery data: " + ec.message()); }
    fs::remove_all(root / finishedPath, ec);
    return true;
}

bool RollBack(const fs::path& root, std::string& error) {
    const auto journal = root / journalPath;
    std::string entries;
    if (!ReadFile(journal / "files", entries)) { return Fail(error, "Could not read save recovery data."); }
    for (std::size_t start = 0; start < entries.size();) {
        const auto end = entries.find('\n', start);
        if (end == std::string::npos || end - start < 3 || entries[start + 1] != ' '
            || (entries[start] != '+' && entries[start] != '=')) {
            return Fail(error, "Invalid save recovery data.");
        }
        const bool isNew = entries[start] == '+';
        const auto relative = entries.substr(start + 2, end - start - 2);
        start = end + 1;
        if (!SafePath(root, relative)) { return Fail(error, "Cannot safely recover " + relative); }
        bool stillStaged = false;
        if (!Exists(journal / "write" / relative, stillStaged)) {
            return Fail(error, "Could not inspect recovery state for " + relative);
        }
        if (stillStaged) { continue; }
        const auto target = root / fs::u8path(relative);
        std::string before, after, current;
        bool exists = false;
        if ((!isNew && !ReadFile(journal / "before" / relative, before))
            || !ReadFile(journal / "after" / relative, after) || !Exists(target, exists)
            || (exists && !ReadFile(target, current))) {
            return Fail(error, "Could not recover " + relative);
        }
        if ((isNew && !exists) || (!isNew && exists && current == before)) { continue; }
        if (!exists || current != after) {
            return Fail(error, "External changes prevent automatic recovery of " + relative);
        }
        std::error_code ec;
        if (isNew) {
            fs::remove(target, ec);
        } else {
            const auto restore = journal / "restore" / relative;
            fs::create_directories(restore.parent_path(), ec);
            if (!ec) {
                fs::copy_file(journal / "before" / relative, restore, fs::copy_options::overwrite_existing, ec);
            }
            if (!ec) { fs::rename(restore, target, ec); }
        }
        if (ec) { return Fail(error, "Could not restore " + relative + ": " + ec.message()); }
    }
    return CloseJournal(root, error);
}

}

std::string PathText(const fs::path& path) {
    const auto utf8 = path.generic_u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

bool RelativePath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.back() == '/') { return false; }
    for (char ch : path) {
        if (static_cast<unsigned char>(ch) < 32 || ch == '\\' || ch == ':' || ch == '*' ||
            ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') { return false; }
    }
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') { return false; }
        if (slash == std::string_view::npos) { break; }
        path.remove_prefix(slash + 1);
    }
    return true;
}

bool EditablePath(std::string_view path, std::string_view tilesetDirectory) {
    if (!RelativePath(path)) { return false; }
    if (path.starts_with("data/") && path.ends_with(".json5")) {
        if (path.find('/', 5) == std::string_view::npos) { return true; }
        for (auto prefix : {"data/terrain/", "data/objects/", "data/items/", "data/monsters/",
                            "data/worldgen/buildings/", "data/worldgen/loot/"}) {
            if (path.starts_with(prefix)) { return true; }
        }
    }
    return path.starts_with(tilesetDirectory) && path.size() > tilesetDirectory.size()
        && path[tilesetDirectory.size()] == '/' &&
        (path.ends_with(".meta") || path.substr(tilesetDirectory.size() + 1) == "tileset.json5");
}

bool ReadFile(const fs::path& path, std::string& text) {
    std::error_code error;
    const auto bytes = fs::file_size(path, error);
    if (error || bytes > maxSourceBytes) { return false; }
    std::ifstream file(path, std::ios::binary);
    if (!file) { return false; }
    text.resize(static_cast<std::size_t>(bytes));
    file.read(text.data(), static_cast<std::streamsize>(bytes));
    return file.good() && file.peek() == std::char_traits<char>::eof();
}

bool SafePath(const fs::path& root, std::string_view path) {
    if (!RelativePath(path)) { return false; }
    auto absolute = root;
    for (const auto& part : fs::u8path(path)) {
        absolute /= part;
        fs::file_status status;
        if (!Status(absolute, status) || fs::is_symlink(status)) { return false; }
    }
    return true;
}

bool CopySources(const fs::path& root, const fs::path& candidate,
                 std::string_view tilesetDirectory, std::string& error) {
    if (!SafePath(root, "data") || !SafePath(root, tilesetDirectory)) {
        return Fail(error, "Content and tileset directories must be inside the checkout.");
    }
    return CopyTree(root / "data", candidate / "data", error)
        && CopyTree(root / fs::u8path(tilesetDirectory), candidate / fs::u8path(tilesetDirectory), error);
}

bool WriteCandidate(const fs::path& candidate, std::span<const SourceEdit> edits, std::string& error) {
    for (const auto& edit : edits) {
        if (!WriteFile(candidate / fs::u8path(edit.path), edit.text)) {
            return Fail(error, "Could not stage " + edit.path);
        }
    }
    return true;
}

bool MatchesCheckout(const fs::path& root, std::span<const SourceEdit> edits, std::string& error) {
    for (const auto& edit : edits) {
        bool exists = false;
        std::string current;
        const auto file = root / fs::u8path(edit.path);
        if (!SafePath(root, edit.path) || !Exists(file, exists)
            || (exists && !ReadFile(file, current))) {
            return Fail(error, "Could not read " + edit.path);
        }
        if (exists != edit.before.has_value() || (exists && current != *edit.before)) {
            return Fail(error, edit.path + " changed on disk. Reload the pack before saving.");
        }
    }
    return true;
}

bool RecoverSave(const fs::path& root, std::string& error) {
    if (!SafePath(root, PathText(journalPath)) || !SafePath(root, PathText(preparedPath))
        || !SafePath(root, PathText(finishedPath))) { return Fail(error, "Unsafe save recovery location."); }
    std::error_code ec;
    fs::remove_all(root / finishedPath, ec);
    if (!ec) { fs::remove_all(root / preparedPath, ec); }
    if (ec) { return Fail(error, "Could not clean temporary save data: " + ec.message()); }
    fs::file_status pending;
    if (!Status(root / journalPath, pending)) { return Fail(error, "Could not inspect save recovery data."); }
    return !fs::exists(pending) || RollBack(root, error);
}

bool SaveSources(const fs::path& root, std::span<const SourceEdit> edits, std::string& error) {
    if (edits.empty()) { return true; }
    if (!RecoverSave(root, error) || !MatchesCheckout(root, edits, error)) { return false; }
    const auto prepared = root / preparedPath;
    std::string entries;
    for (const auto& edit : edits) {
        const auto relative = fs::u8path(edit.path);
        if (!WriteFile(prepared / "after" / relative, edit.text)
            || !WriteFile(prepared / "write" / relative, edit.text)
            || (edit.before && !WriteFile(prepared / "before" / relative, *edit.before))) {
            return Fail(error, "Could not prepare " + edit.path);
        }
        if (edit.before) {
            std::error_code ec;
            const auto permissions = fs::status(root / relative, ec).permissions();
            if (!ec) { fs::permissions(prepared / "write" / relative, permissions, ec); }
            if (!ec) { fs::permissions(prepared / "before" / relative, permissions, ec); }
            if (ec) { return Fail(error, "Could not preserve permissions for " + edit.path); }
        }
        entries += (edit.before ? "= " : "+ ") + edit.path + '\n';
    }
    if (!WriteFile(prepared / "files", entries) || !MatchesCheckout(root, edits, error)) {
        return error.empty() ? Fail(error, "Could not record the prepared save.") : false;
    }
    const auto journal = root / journalPath;
    std::error_code ec;
    fs::rename(prepared, journal, ec);
    if (ec) { return Fail(error, "Could not start the save: " + ec.message()); }
    for (const auto& edit : edits) {
        const auto relative = fs::u8path(edit.path);
        const auto target = root / relative;
        if (!MatchesCheckout(root, {&edit, 1}, error)) { break; }
        fs::create_directories(target.parent_path(), ec);
        if (!ec) { fs::rename(journal / "write" / relative, target, ec); }
        if (ec) {
            error = "Could not save " + edit.path + ": " + ec.message();
            break;
        }
    }
    if (error.empty() && CloseJournal(root, error)) { return true; }
    std::string recovery;
    if (!RollBack(root, recovery)) { error += "\n" + recovery; }
    return false;
}

}
