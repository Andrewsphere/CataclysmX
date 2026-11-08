#include "core/diagnostics.h"

#include <catch2/catch_test_macros.hpp>

#include <EASTL/utility.h>

namespace {
using namespace cataclysmx;

TEST_CASE("Diagnostics retain innermost-first frames and format outermost first", "[diagnostics]") {
    Diagnostics diagnostics;
    diagnostics.Add("failed to read value");
    diagnostics.Add("invalid field");
    diagnostics.Add("could not load document");

    REQUIRE(diagnostics.Frames().size() == 3);
    CHECK(diagnostics.Frames()[0].message == "failed to read value");
    CHECK(FormatDiagnostics(diagnostics) ==
        "could not load document\n  invalid field\n    failed to read value");
}

TEST_CASE("Diagnostics format structured location metadata", "[diagnostics]") {
    Diagnostics diagnostics;
    DiagnosticFrame frame;
    frame.message = "unexpected token";
    frame.location = DiagnosticLocation{"items.json", "items[2].weight", 17, 4};
    diagnostics.Add(eastl::move(frame));

    CHECK(FormatDiagnostics(diagnostics) ==
        "unexpected token (items.json at items[2].weight:line 17:column 4)");
}

TEST_CASE("Diagnostics clear all frames and format empty", "[diagnostics]") {
    Diagnostics diagnostics;
    CHECK(diagnostics.Empty());
    CHECK(FormatDiagnostics(diagnostics).empty());

    diagnostics.Add("problem");
    diagnostics.Clear();
    CHECK(diagnostics.Empty());
    CHECK(diagnostics.Frames().empty());
    CHECK(FormatDiagnostics(diagnostics).empty());
}

}
