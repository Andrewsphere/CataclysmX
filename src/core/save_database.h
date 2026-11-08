#pragma once

#include "core/world_archive.h"
#include <EASTL/optional.h>

namespace cataclysmx {

inline constexpr int save_format_version = 1;

// SQLite and JSON remain private to this subsystem. A connection is exclusively
// owned by one World; transactions are synchronous simulation-boundary calls.
class SaveDatabase {
public:
    SaveDatabase();
    ~SaveDatabase();
    SaveDatabase(const SaveDatabase&) = delete;
    SaveDatabase& operator=(const SaveDatabase&) = delete;

    SaveResult<void> Create(const char* path, Diagnostics* diagnostics = nullptr);
    SaveResult<SaveSnapshot> Open(const char* path, Diagnostics* diagnostics = nullptr);
    SaveResult<eastl::optional<ChunkArchive>> ReadChunk(ChunkCoord position,
        PersistentId nextId, Diagnostics* diagnostics = nullptr);
    SaveResult<void> Begin(Diagnostics* diagnostics = nullptr);
    SaveResult<void> WriteChunk(const ChunkArchive& chunk, Diagnostics* diagnostics = nullptr);
    SaveResult<void> WriteSnapshot(const SaveSnapshot& snapshot, Diagnostics* diagnostics = nullptr);
    SaveResult<void> Commit(Diagnostics* diagnostics = nullptr);
    void Rollback();

private:
    class Impl;
    eastl::unique_ptr<Impl> impl_;
};

}
