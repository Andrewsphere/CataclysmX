#pragma once

#include "digital/digital_types.h"
#include "core/world_result.h"
#include <EASTL/optional.h>
#include <EASTL/span.h>

namespace cataclysmx {
class World;
struct InformationLibrary;

class InformationSystem {
public:
    explicit InformationSystem(World& world);
    WorldResult<InformationResult> Copy(ActorHandle actor, InformationEndpoint source,
        InformationEndpoint destination, InformationId package, ItemHandle host = {});
    WorldResult<InformationResult> Remove(ActorHandle actor, InformationEndpoint destination,
        InformationId package);
    WorldResult<InformationResult> Cancel(ActorHandle actor);
    void Collect(ActorHandle actor, Vector<InformationDeviceView>& output) const;
    void CollectPackages(InformationEndpoint source, Vector<InformationId>& output) const;
    bool HasPackage(InformationEndpoint source, InformationId package) const;
    bool HasBook(ItemHandle source, BookId book, eastl::span<const ItemHandle> accessible) const;
    bool Busy(ActorHandle actor) const;
    eastl::optional<Tick> ActorEventIn(ActorHandle actor) const;
    void Elapse(Tick elapsed);
    bool ProcessEvents();
    void Attach(ItemHandle item);
    void Detach(ItemHandle item);
private:
    World& world_;
    Vector<ItemHandle> active_;
    Vector<ItemHandle> due_;
    mutable Vector<ItemHandle> items_;
    ItemHandle Resolve(InformationEndpoint endpoint) const;
    InformationOutcome CheckEndpoint(ActorHandle actor, InformationEndpoint endpoint, bool writable) const;
    bool Powered(InformationEndpoint endpoint) const;
    InformationOutcome CheckTransfer(ActorHandle actor, InformationEndpoint source,
        InformationEndpoint destination, InformationId package, ItemHandle host) const;
};
}
