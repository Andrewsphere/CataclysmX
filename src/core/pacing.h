#pragma once

#include "core/timing.h"
#include "core/world.h"

#include <EASTL/optional.h>

namespace cataclysmx {

struct PlayerReadiness {
    ActorHandle actor{};
    PersistentId id = 0;
    std::uint32_t speed = 100;
    bool ready = false;
    bool hasCommand = false;
};

// Session-owned transient state; elapsed time is supplied explicitly.
class TickPacer {
public:
    bool SetSettings(PacingSettings settings);
    PacingSettings GetSettings() const;
    void Synchronize(const Vector<PlayerReadiness>& players);
    bool IsIdle() const;
    void ElapseMicroseconds(std::uint64_t elapsed);
    Tick AvailableTicks() const;
    void ConsumeTicks(Tick ticks);
    eastl::optional<std::uint64_t> NextDeadlineMicroseconds() const;
    std::uint64_t MicrosecondsUntilTicks(Tick ticks) const;
    Vector<ActorHandle> ExpiredPlayers() const;
    void Reset();

private:
    struct Deadline {
        ActorHandle actor{};
        PersistentId id = 0;
        std::uint64_t remainingUnits = 0;
    };

    PacingSettings settings_{};
    Vector<Deadline> deadlines_;
    Tick creditedTicks_ = 0;
    std::uint32_t fractionalUnits_ = 0;
    bool hasActivity_ = false;
};

}
