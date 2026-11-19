#pragma once

#include "client/localization.h"
#include "client/input.h"

namespace cataclysmx {

const char* StationAvailabilityText(const StationView& station, const ObjectDefinition& definition);
// Catalog rate and meter-gated station coverage from the snapshot only.
// Passive stations return an empty string.
String StationPowerText(const WorldView& view, const StationView& station, const ObjectDefinition& definition);

// Value-only workshop state; the same commands work locally and over LAN.
class PowerWorkshop {
public:
    void Open(const WorldView& view);
    eastl::optional<Command> Handle(const KeyChord& key, const WorldView& view, const ContentCatalog& content);
    void Describe(const WorldView& view, const ContentCatalog& content, Vector<String>& lines);
private:
    static void Cycle(const Vector<PersistentId>& items, PersistentId& selected);
    void Refresh(const WorldView& view, const ContentCatalog& content);
    void ClampCursor(const WorldView& view);
    static String Name(const WorldView& view, const ContentCatalog& content, PersistentId id);
    TileCoord cursor_{};
    PersistentId player_ = 0;
    PersistentId kit_ = 0;
    PersistentId equipment_ = 0;
    int width_ = 16;
    int depth_ = 16;
    bool placing_ = false;
    Vector<PersistentId> kits_;
    Vector<PersistentId> equipmentItems_;
    PersistentId fuel_ = 0;
    Vector<PersistentId> fuelItems_;
};

struct PowerDebugSnapshot {
    Vector<PowerGridDebug> grids;
    Vector<ChunkResidencyDebug> columns;
    ChunkResidencyCounters residency;
    PowerCounters workload;
};
void CapturePowerDebug(const World& world, PowerDebugSnapshot& output);
void DescribePowerDebug(const PowerDebugSnapshot& snapshot, TileCoord center, Vector<String>& lines);
}
