#include "items/item_types.h"
#include <cstdlib>

namespace cataclysmx {
namespace {
bool ValidTextBytes(const String& text) {
    if (text.size() > maxItemText) { return false; }
    for (unsigned char byte : text) {
        if (byte < 0x20 || byte == 0x7f) { return false; }
    }
    return true;
}
}

bool ValidItemActionId(eastl::string_view id) {
    if (id.empty() || id.size() > maxItemActionName || id[0] < 'a' || id[0] > 'z') { return false; }
    for (char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) { return false; }
    }
    return true;
}

bool ValidItemText(const ItemText& text) {
    return ValidTextBytes(text.key) && ValidTextBytes(text.fallback);
}

bool ValidItemUseReason(ItemUseOutcome outcome, const ItemUseReason& reason) {
    if (outcome > ItemUseOutcome::Refused) { return false; }
    if (reason.code.empty() && reason.message.key.empty() && reason.message.fallback.empty()) { return true; }
    return outcome == ItemUseOutcome::Refused
        && ValidItemActionId({reason.code.data(), reason.code.size()}) && ValidItemText(reason.message);
}

bool ValidItemBehaviorSchema(const ItemBehaviorSchema& schema) {
    if (schema.implementationVersion == 0 || schema.actions.size() > maxItemActionsPerItem) { return false; }
    if (schema.countdown) {
        if (!schema.hasActivity || schema.maximumDelay <= 0) { return false; }
    } else if (schema.maximumDelay != 0) { return false; }
    for (std::size_t i = 0; i < schema.actions.size(); ++i) {
        const auto& action = schema.actions[i];
        if (!ValidItemActionId({action.id.data(), action.id.size()}) || action.baseCost == 0
            || action.argument > ItemActionArgument::Direction || action.category > ItemActionCategory::ManageContents
            || !ValidItemText(action.label)) { return false; }
        if (i > 0 && schema.actions[i - 1].id >= action.id) { return false; }
    }
    return true;
}

const char* ItemUseMessage(ItemUseOutcome outcome) {
    switch (outcome) {
    case ItemUseOutcome::Succeeded: return "Done.";
    case ItemUseOutcome::InvalidAction: return "Invalid item action.";
    case ItemUseOutcome::Inaccessible: return "That item is inaccessible.";
    case ItemUseOutcome::Refused: return "That action is unavailable.";
    }
    std::abort();
}
}
