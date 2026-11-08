#include "core/diagnostics.h"

#include <EASTL/string.h>
#include <EASTL/utility.h>

#include <charconv>

namespace cataclysmx {
namespace {

void AppendNumber(String& output, std::uint64_t value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    output.append(buffer, static_cast<String::size_type>(result.ptr - buffer));
}

void AppendLocation(String& output, const DiagnosticLocation& location) {
    bool hasPrevious = false;
    if (!location.file.empty()) {
        output.append(location.file);
        hasPrevious = true;
    }
    if (!location.path.empty()) {
        if (hasPrevious) { output.append(" "); }
        output.append("at ");
        output.append(location.path);
        hasPrevious = true;
    }
    if (location.line) {
        if (hasPrevious) { output.append(":"); }
        output.append("line ");
        AppendNumber(output, *location.line);
        hasPrevious = true;
    }
    if (location.column) {
        if (location.line) { output.append(":"); }
        else if (hasPrevious) { output.append(" "); }
        output.append("column ");
        AppendNumber(output, *location.column);
    }
}

}

void Diagnostics::Add(eastl::string_view message) {
    DiagnosticFrame frame;
    frame.message.assign(message.empty() ? "" : message.data(), message.size());
    frames_.push_back(eastl::move(frame));
}

void Diagnostics::Add(DiagnosticFrame frame) {
    frames_.push_back(eastl::move(frame));
}

void Diagnostics::Clear() {
    frames_.clear();
}

bool Diagnostics::Empty() const {
    return frames_.empty();
}

const Vector<DiagnosticFrame>& Diagnostics::Frames() const {
    return frames_;
}

String FormatDiagnostics(const Diagnostics& diagnostics) {
    String output;
    const auto& frames = diagnostics.Frames();
    for (size_t index = frames.size(); index > 0; --index) {
        const size_t depth = frames.size() - index;
        for (size_t indent = 0; indent < depth; ++indent) {
            output.append("  ");
        }
        const DiagnosticFrame& frame = frames[index - 1];
        output.append(frame.message);
        if (frame.location) {
            output.append(" (");
            AppendLocation(output, *frame.location);
            output.append(")");
        }
        if (index > 1) { output.append("\n"); }
    }
    return output;
}

}
