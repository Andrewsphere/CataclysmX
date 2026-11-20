#pragma once

#include "core/server.h"
#include <EASTL/utility.h>
#include <charconv>

namespace cataclysmx {

// Shared local/LAN script grammar. Endpoint tokens are item:ID or object:ID.
inline bool ParseDigitalCommand(eastl::span<const eastl::string_view> words, Command& output) {
    if (words.empty()) { return false; }
    Command command;
    const auto number = [](eastl::string_view text, auto& value) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return !text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size();
    };
    const auto endpoint = [&](eastl::string_view text, InformationEndpoint& value) {
        if (text.substr(0, 5) == "item:") { return number(text.substr(5), value.item) && value.item; }
        if (text.substr(0, 7) == "object:") { return number(text.substr(7), value.object) && value.object; }
        return false;
    };
    if (words[0] == "begin-machine" && words.size() == 4) {
        command.kind = CommandKind::begin_machine;
        if (!number(words[1], command.target) || !number(words[2], command.machineRecipe.value)
            || !number(words[3], command.quantity)) { return false; }
    } else if (words.size() == 2 && (words[0] == "stop-machine" || words[0] == "resume-machine"
        || words[0] == "cancel-machine" || words[0] == "collect-machine")) {
        command.kind = CommandKind::machine_action;
        command.machineAction = words[0] == "stop-machine" ? MachineAction::Stop : words[0] == "resume-machine"
            ? MachineAction::Resume : words[0] == "cancel-machine" ? MachineAction::Cancel : MachineAction::Collect;
        if (!number(words[1], command.target)) { return false; }
    } else if ((words[0] == "copy-information" || words[0] == "import-information" || words[0] == "export-information")
        && (words.size() == 4 || words.size() == 5)) {
        command.kind = CommandKind::copy_information;
        if (!endpoint(words[1], command.informationSource) || !endpoint(words[2], command.informationDestination)
            || !number(words[3], command.information.value)
            || (words.size() == 5 && !number(words[4], command.informationHost))) { return false; }
    } else if (words[0] == "delete-information" && words.size() == 3) {
        command.kind = CommandKind::remove_information;
        if (!endpoint(words[1], command.informationDestination) || !number(words[2], command.information.value)) { return false; }
    } else if (words[0] == "cancel-information" && words.size() == 1) {
        command.kind = CommandKind::cancel_information;
    } else { return false; }
    if (!ValidCommandShape(command)) { return false; }
    output = eastl::move(command);
    return true;
}

}
