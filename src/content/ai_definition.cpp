#include "content/ai_definition.h"

namespace cataclysmx {

bool ValidAiProfile(const AiProfile& profile) {
    return profile.sightRange >= 1 && profile.sightRange <= 32
        && profile.darkRange <= profile.dimRange && profile.dimRange <= profile.sightRange
        && profile.scanIntervalTicks >= 1 && profile.scanIntervalTicks <= 60000
        && profile.memoryTicks >= 1 && profile.memoryTicks <= 60000
        && profile.searchTicks >= 1 && profile.searchTicks <= profile.memoryTicks
        && profile.candidateBudget >= 1 && profile.candidateBudget <= 32
        && profile.bucketBudget >= 1 && profile.bucketBudget <= 64
        && profile.switchAdvantage <= 32;
}

}
