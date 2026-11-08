#pragma once

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

struct SettingsDirectory {
    std::filesystem::path root;
    SettingsDirectory() {
        std::error_code error;
        const auto parent = std::filesystem::temp_directory_path(error);
        REQUIRE_FALSE(error);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; ; ++attempt) {
            root = parent / ("cataclysmx-settings-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            const bool created = std::filesystem::create_directory(root, error);
            REQUIRE_FALSE(error);
            if (created) { break; }
        }
    }
    ~SettingsDirectory() { std::error_code error; std::filesystem::remove_all(root, error); }
    std::string File(const char* relative) const { return (root / relative).string(); }
    void Write(const char* relative, const char* text) const {
        const auto file = root / relative;
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        REQUIRE_FALSE(error);
        std::ofstream stream(file, std::ios::binary);
        stream << text;
        REQUIRE(stream.good());
    }
    std::string Read(const char* relative) const {
        std::ifstream stream(root / relative, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
};
