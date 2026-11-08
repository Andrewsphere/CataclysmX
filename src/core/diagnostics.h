#pragma once

#include "core/containers.h"

#include <EASTL/optional.h>
#include <EASTL/string_view.h>

#include <cstdint>

namespace cataclysmx {

struct DiagnosticLocation {
    String file;
    String path;
    eastl::optional<std::uint64_t> line;
    eastl::optional<std::uint64_t> column;
};

struct DiagnosticFrame {
    String message;
    eastl::optional<DiagnosticLocation> location;
};

class Diagnostics {
public:
    void Add(eastl::string_view message);
    void Add(DiagnosticFrame frame);
    void Clear();
    bool Empty() const;
    const Vector<DiagnosticFrame>& Frames() const;

private:
    Vector<DiagnosticFrame> frames_;
};

String FormatDiagnostics(const Diagnostics& diagnostics);

}
