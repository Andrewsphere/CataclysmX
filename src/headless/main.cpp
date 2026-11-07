#include "core/integration.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--fail-log") == 0) {
        cataclysmx::logging_failure_probe();
    }
    if (!cataclysmx::core_smoke() || !cataclysmx::json_smoke("{\"seed\":42}")) {
        return 1;
    }
    std::printf("seed=42 checksum=%u\n", cataclysmx::headless_checksum(42));
    return 0;
}
