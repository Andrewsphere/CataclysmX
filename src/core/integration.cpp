#include "core/integration.h"

#include <EASTL/vector.h>
#include <EASTL/allocator_malloc.h>
#include <ankerl/unordered_dense.h>
#include <slot_map/slot_map.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>
#include <yyjson.h>

#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#error Engine must compile without exceptions
#endif
#ifndef SPDLOG_NO_EXCEPTIONS
#error The compiled spdlog package must export SPDLOG_NO_EXCEPTIONS
#endif

namespace cataclysmx {

bool core_smoke() {
    eastl::vector<int, eastl::allocator_malloc> values{3, 5, 8};
    ankerl::unordered_dense::map<int, int> lookup;
    lookup.emplace(1, values[0] + values[1] + values[2]);
    dod::slot_map64<int> slots;
    const auto stale = slots.emplace(21);
    slots.erase(stale);
    const auto live = slots.emplace(34);
    static_assert(sizeof(live) == sizeof(std::uint64_t));
    const int* value = slots.get(live);
    return lookup.at(1) == 16 && slots.get(stale) == nullptr && value && *value == 34;
}

bool json_smoke(const char* json) {
    yyjson_doc* document = yyjson_read(json, std::char_traits<char>::length(json),
        YYJSON_READ_JSON5);
    if (!document) {
        return false;
    }
    const bool valid = yyjson_get_int(yyjson_obj_get(yyjson_doc_get_root(document), "seed")) == 42;
    yyjson_doc_free(document);
    return valid;
}

bool logging_smoke(const char* directory) {
    const std::string filename = std::string(directory) + "/cataclysmx-smoke.log";
    auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filename, 4096, 2);
    auto console = std::make_shared<spdlog::sinks::stdout_sink_mt>();
    spdlog::logger logger("integration", {console, file});
    logger.info("CataclysmX compiled no-exceptions logging probe");
    logger.flush();
    return true;
}

[[noreturn]] void logging_failure_probe() {
    // Test in a child process: a file cannot be created below an existing file.
    const char* marker = "cataclysmx-log-failure-marker";
#ifdef _MSC_VER
    std::FILE* file = nullptr;
    fopen_s(&file, marker, "wb");
    // Keep a deliberate abort noninteractive even with the MSVC debug CRT.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#else
    std::FILE* file = std::fopen(marker, "wb");
#endif
    if (!file) {
        std::exit(2);
    }
    std::fclose(file);
    // The terminate handler verifies that the compiled binary calls abort, not throw.
    std::set_terminate([] { std::_Exit(3); });
    std::signal(SIGABRT, [](int) {
        std::remove("cataclysmx-log-failure-marker");
        std::fputs("SPDLOG_NO_EXCEPTIONS verified\n", stdout);
        std::fflush(stdout);
        std::_Exit(0);
    });
    spdlog::sinks::rotating_file_sink_mt sink(std::string(marker) + "/blocked.log", 1024, 1);
    std::exit(4);
}

std::uint32_t headless_checksum(std::uint32_t seed) {
    // Explicit uint32 arithmetic gives the same fixed-seed probe on every compiler.
    eastl::vector<std::uint32_t, eastl::allocator_malloc> commands{1, 2, 0, 4, 3};
    for (const auto command : commands) {
        seed = seed * 1664525U + 1013904223U + command;
    }
    return seed;
}

}
