#pragma once

#include "actors/ai_controller.h"

namespace cataclysmx {

// Controller factory implementation; callers normally use MakeAiController.
StateResult<void> ValidateSightedAiState(const DataObject& state, Diagnostics* diagnostics);
StateResult<eastl::unique_ptr<AiController>> RestoreSightedAiController(
    const DataObject* state, Diagnostics* diagnostics);

}
