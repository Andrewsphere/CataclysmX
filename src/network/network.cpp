#include "network/network.h"
#include "network/wire.h"
#include "items/item_behavior.h"

#include <enet/enet.h>

#ifdef GetObject
#undef GetObject
#endif

#include <EASTL/utility.h>
#include <EASTL/sort.h>

#include <cstdlib>
#include <initializer_list>
#include <random>

namespace cataclysmx::network {
namespace {
std::uint64_t SaturatingAdd(std::uint64_t a, std::uint64_t b) {
    return b > UINT64_MAX - a ? UINT64_MAX : a + b;
}

std::uint64_t RandomToken() {
    std::random_device source;
    std::uint64_t token = 0;
    while (!token) {
        for (int i = 0; i < 8; ++i) token = (token << 8) | (source() & 255u);
    }
    return token;
}

void HashByte(std::uint64_t& hash, std::uint8_t byte) { hash = (hash ^ byte) * 1099511628211ull; }
void HashNumber(std::uint64_t& hash, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) { HashByte(hash, static_cast<std::uint8_t>(v)); v >>= 8; }
}
void HashString(std::uint64_t& hash, const String& s) {
    HashNumber(hash, s.size()); for (char c : s) HashByte(hash, static_cast<std::uint8_t>(c));
}
void HashText(std::uint64_t& hash, const ItemText& text) {
    HashString(hash, text.key); HashString(hash, text.fallback);
}
void HashTraits(std::uint64_t& hash, const StaticList<TraitRequirement, maxRecipeTraits>& traits) {
    HashNumber(hash, traits.Size());
    for (const auto& trait : traits) {
        HashNumber(hash, trait.trait.value); HashNumber(hash, trait.tier);
    }
}
void HashInputs(std::uint64_t& hash, const StaticList<RecipeInput, maxRecipeInputs>& inputs) {
    HashNumber(hash, inputs.Size());
    for (const auto& input : inputs) { HashNumber(hash, input.Packed()); }
}
void HashRequirements(std::uint64_t& hash, eastl::span<const SkillRequirement> values) {
    HashNumber(hash, values.size());
    for (const auto& v : values) { HashNumber(hash, v.skill.value); HashNumber(hash, v.level); }
}
void HashTraining(std::uint64_t& hash, eastl::span<const SkillTraining> values) {
    HashNumber(hash, values.size());
    for (const auto& v : values) {
        HashNumber(hash, v.skill.value); HashNumber(hash, v.level); HashNumber(hash, v.xpPerWork);
    }
}
void HashData(std::uint64_t& hash, const DataObject& data) {
    // Tags distinguish values such as signed 1, unsigned 1, and true.
    HashNumber(hash, static_cast<std::uint64_t>(data.Type()));
    switch (data.Type()) {
    case DataType::Boolean: HashNumber(hash, data.BooleanValue()); break;
    case DataType::Signed: HashNumber(hash, static_cast<std::uint64_t>(data.SignedValue())); break;
    case DataType::Unsigned: HashNumber(hash, data.UnsignedValue()); break;
    case DataType::String: HashString(hash, data.StringValue()); break;
    case DataType::List:
        HashNumber(hash, data.Size());
        for (std::size_t i = 0; i < data.Size(); ++i) HashData(hash, data.At(i));
        break;
    case DataType::Object: {
        Vector<std::size_t> order;
        for (std::size_t i = 0; i < data.Size(); ++i) order.push_back(i);
        eastl::sort(order.begin(), order.end(), [&data](std::size_t a, std::size_t b) { return data.Name(a) < data.Name(b); });
        HashNumber(hash, order.size());
        for (const auto i : order) { HashString(hash, data.Name(i)); HashData(hash, data.At(i)); }
        break;
    }
    }
}

void HashPosition(std::uint64_t& hash, TileCoord position) {
    HashNumber(hash, position.x); HashNumber(hash, position.z); HashNumber(hash, position.y);
}

void HashLayer(std::uint64_t& hash, const PatternLayer& layer) {
    HashNumber(hash, layer.y); HashNumber(hash, layer.tiles.size());
    for (TerrainId terrain : layer.tiles) { HashNumber(hash, terrain.value); }
}

void HashPopulation(std::uint64_t& hash, const PatternPopulation& population) {
    HashNumber(hash, population.furniture.size());
    for (const auto& furniture : population.furniture) {
        HashPosition(hash, furniture.position); HashNumber(hash, furniture.type.value);
        HashNumber(hash, static_cast<std::uint64_t>(furniture.facing));
    }
    HashNumber(hash, population.loot.size());
    for (const auto& loot : population.loot) {
        HashPosition(hash, loot.position); HashNumber(hash, loot.pool.value);
        HashNumber(hash, loot.chance); HashNumber(hash, loot.rolls);
        HashNumber(hash, static_cast<std::uint64_t>(loot.destination));
    }
    HashNumber(hash, population.monsters.size());
    for (const auto& monster : population.monsters) {
        HashPosition(hash, monster.position); HashNumber(hash, monster.group.value);
        HashNumber(hash, monster.chance);
    }
}

bool Send(ENetPeer* peer, enet_uint8 channel, const wire::Writer& message) {
    if (message.bytes.size() > wire::maxPacket || peer->state != ENET_PEER_STATE_CONNECTED) return false;
    ENetPacket* packet = enet_packet_create(message.bytes.data(), message.bytes.size(), ENET_PACKET_FLAG_RELIABLE);
    if (!packet) std::abort(); // ENet packet allocation failure is fatal, like EASTL OOM.
    if (enet_peer_send(peer, channel, packet) == 0) return true;
    enet_packet_destroy(packet); return false;
}
wire::Writer Message(wire::Type type) {
    wire::Writer w; w.U32(wire::protocol); w.U8(static_cast<std::uint8_t>(type)); return w;
}
bool Parse(const ENetPacket* packet, wire::Reader& r, wire::Type& type) {
    if (packet->dataLength > wire::maxPacket || packet->dataLength < 5) return false;
    r.bytes = {packet->data, packet->dataLength};
    std::uint32_t magic; std::uint8_t kind;
    if (!r.U32(magic) || magic != wire::protocol || !r.U8(kind) || kind < 1 ||
        kind > static_cast<std::uint8_t>(wire::Type::InspectionAck)) return false;
    type = static_cast<wire::Type>(kind); return true;
}
ENetSocket OpenDatagram(std::uint16_t port) {
    ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (socket == ENET_SOCKET_NULL) return socket;
    ENetAddress address{ENET_HOST_ANY, port};
    if (enet_socket_bind(socket, &address) < 0 ||
        enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1) < 0 ||
        enet_socket_set_option(socket, ENET_SOCKOPT_BROADCAST, 1) < 0) {
        enet_socket_destroy(socket); return ENET_SOCKET_NULL;
    }
    return socket;
}
bool Broadcast(ENetSocket socket, const ENetAddress& address, const wire::Writer& writer) {
    ENetBuffer buffer{};
    buffer.dataLength = writer.bytes.size();
    buffer.data = const_cast<std::uint8_t*>(writer.bytes.data());
    return enet_socket_send(socket, &address, &buffer, 1) == static_cast<int>(writer.bytes.size());
}
bool Receive(ENetSocket socket, ENetAddress& address, wire::Reader& r, std::uint8_t* data, std::size_t capacity) {
    ENetBuffer buffer{};
    buffer.dataLength = capacity;
    buffer.data = data;
    const int length = enet_socket_receive(socket, &address, &buffer, 1);
    if (length <= 0 || length > static_cast<int>(capacity)) return false;
    r.bytes = {data, static_cast<std::size_t>(length)}; return true;
}
}

std::uint64_t ContentFingerprint(const ContentCatalog& content) {
    std::uint64_t hash = 14695981039346656037ull;
    HashNumber(hash, content.FlagCount());
    for (std::size_t i = 0; i < content.FlagCount(); ++i) {
        const auto& flag = content.GetFlag({static_cast<std::uint32_t>(i + 1)});
        HashString(hash, flag.id); HashString(hash, flag.name); HashString(hash, flag.description);
        HashNumber(hash, static_cast<std::uint8_t>(flag.kind));
        for (const auto domain : {FlagDomain::Item, FlagDomain::Monster, FlagDomain::Terrain, FlagDomain::Object}) {
            HashNumber(hash, flag.domains.Contains(domain));
        }
    }
    HashNumber(hash, content.TerrainCount());
    for (std::size_t i = 0; i < content.TerrainCount(); ++i) {
        const auto& t = content.GetTerrain({static_cast<std::uint32_t>(i + 1)});
        HashString(hash, t.id); HashString(hash, t.name); HashString(hash, t.appearance.symbol);
        HashNumber(hash, t.appearance.foreground.red); HashNumber(hash, t.appearance.foreground.green);
        HashNumber(hash, t.appearance.foreground.blue); HashNumber(hash, t.appearance.background.red);
        HashNumber(hash, t.appearance.background.green); HashNumber(hash, t.appearance.background.blue);
        HashNumber(hash, t.passable); HashNumber(hash, t.blocksSight);
        HashNumber(hash, t.openSpace); HashNumber(hash, t.stairs);
        HashNumber(hash, t.light.intensity); HashNumber(hash, t.light.range);
        HashNumber(hash, t.opensTo.value); HashNumber(hash, t.closesTo.value);
        HashNumber(hash, t.locksTo.value); HashNumber(hash, t.unlocksTo.value);
    }
    HashNumber(hash, content.BuildingPatternCount());
    for (std::size_t i = 0; i < content.BuildingPatternCount(); ++i) {
        const auto& p = content.GetBuildingPattern({static_cast<std::uint32_t>(i + 1)});
        HashString(hash, p.id); HashNumber(hash, p.width); HashNumber(hash, p.height);
        HashNumber(hash, p.entrance.x); HashNumber(hash, p.entrance.z); HashNumber(hash, p.weight);
        HashNumber(hash, static_cast<std::uint8_t>(p.site));
        HashString(hash, p.family); HashNumber(hash, p.familyWeight); HashNumber(hash, static_cast<std::uint8_t>(p.use));
        HashNumber(hash, p.tiles.size()); for (TerrainId t : p.tiles) HashNumber(hash, t.value);
        HashNumber(hash, p.layers.size());
        for (const auto& layer : p.layers) { HashLayer(hash, layer); }
        HashPopulation(hash, p.population);
        HashNumber(hash, p.basements.size());
        for (const auto& basement : p.basements) {
            HashString(hash, basement.id); HashNumber(hash, basement.weight);
            HashLayer(hash, basement.layer); HashPopulation(hash, basement.population);
        }
    }
    HashNumber(hash, content.ItemCount());
    for (std::size_t i = 0; i < content.ItemCount(); ++i) {
        const auto& item = content.GetItem({static_cast<std::uint32_t>(i + 1)});
        HashString(hash, item.id); HashString(hash, item.name);
        HashString(hash, item.description);
        HashNumber(hash, item.flags.Ids().size());
        for (const auto flag : item.flags.Ids()) { HashString(hash, content.GetFlag(flag).id); }
        HashNumber(hash, item.art.width); HashNumber(hash, item.art.lines.size());
        for (const auto& line : item.art.lines) {
            HashNumber(hash, line.size());
            for (const auto& run : line) {
                HashString(hash, run.text); HashNumber(hash, run.color.useDefault);
                if (!run.color.useDefault) {
                    HashNumber(hash, run.color.red); HashNumber(hash, run.color.green); HashNumber(hash, run.color.blue);
                }
                HashNumber(hash, run.cells);
            }
        }
        const auto& melee = ItemMelee(item);
        HashNumber(hash, melee.attackCost); HashNumber(hash, melee.hands); HashNumber(hash, melee.damage.size());
        for (const auto& packet : melee.damage) {
            HashNumber(hash, static_cast<std::uint8_t>(packet.type)); HashNumber(hash, packet.amount); HashNumber(hash, packet.penetration);
        }
        HashNumber(hash, item.massGrams); HashNumber(hash, item.volumeMl);
        HashTraits(hash, item.toolTraits);
        HashNumber(hash, item.craftingPower.value); HashNumber(hash, item.craftingEnergyPerWork);
        HashNumber(hash, item.craftingGridPower);
        HashNumber(hash, item.wiringMaterial.value != 0);
        if (item.wiringMaterial.value) { HashString(hash, content.GetItem(item.wiringMaterial).id); }
        HashNumber(hash, item.wiringTilesPerMaterial);
        HashNumber(hash, item.placedObject.value != 0);
        if (item.placedObject.value) { HashString(hash, content.GetObject(item.placedObject).id); }
        HashNumber(hash, item.gridMeter);
        HashNumber(hash, item.recoveryAllowed); HashNumber(hash, item.unfinishedCraft);
        HashNumber(hash, item.books.size());
        for (const auto book : item.books) HashNumber(hash, book.value);
        HashNumber(hash, item.readingPower.value);
        HashNumber(hash, item.capabilities.size());
        for (std::size_t c = 0; c < item.capabilities.size(); ++c) {
            const auto& capability = item.capabilities[c];
            HashString(hash, capability.id); HashString(hash, capability.type); HashData(hash, capability.config);
            const auto* behavior = ItemBehaviorConfig(item, {static_cast<std::uint16_t>(c + 1)});
            HashNumber(hash, behavior != nullptr);
            if (!behavior) continue;
            const auto& schema = behavior->Schema();
            HashNumber(hash, schema.actions.size());
            for (const auto& action : schema.actions) {
                HashString(hash, action.id); HashNumber(hash, static_cast<std::uint64_t>(action.argument));
                HashNumber(hash, static_cast<std::uint64_t>(action.category)); HashNumber(hash, action.baseCost);
                HashText(hash, action.label);
            }
            HashNumber(hash, schema.hasActivity); HashNumber(hash, schema.countdown);
            HashNumber(hash, schema.maximumDelay); HashNumber(hash, schema.implementationVersion);
        }
    }
    // Keep fingerprints of catalogs without objects compatible with earlier content.
    if (content.ObjectCount() != 0) {
        HashNumber(hash, content.ObjectCount());
        for (std::size_t i = 0; i < content.ObjectCount(); ++i) {
            const auto& object = content.GetObject({static_cast<std::uint32_t>(i + 1)});
            HashString(hash, object.id); HashString(hash, object.name);
            HashNumber(hash, object.capacityVolumeMl.has_value());
            if (object.capacityVolumeMl) { HashNumber(hash, *object.capacityVolumeMl); }
            HashNumber(hash, static_cast<std::uint64_t>(object.kind));
            HashNumber(hash, object.blocksMovement); HashNumber(hash, object.blocksSight);
            HashNumber(hash, object.movable);
            HashNumber(hash, object.moveCost);
            HashTraits(hash, object.toolTraits);
            HashNumber(hash, object.packingItem.value != 0);
            if (object.packingItem.value) { HashString(hash, content.GetItem(object.packingItem).id); }
            HashNumber(hash, object.light.intensity); HashNumber(hash, object.light.range);
            HashNumber(hash, static_cast<std::uint64_t>(object.behavior));
            switch (object.behavior) {
            case ObjectBehaviorKind::None:
                HashNumber(hash, 0); // No behavior implementation.
                break;
            case ObjectBehaviorKind::CraftingMachine:
                HashNumber(hash, 1); // Crafting machine implementation/state version.
                HashString(hash, content.GetItem(object.machine.input).id);
                HashNumber(hash, object.machine.inputCount);
                HashString(hash, content.GetItem(object.machine.output).id);
                HashNumber(hash, object.machine.outputCount);
                HashNumber(hash, object.machine.duration);
                HashNumber(hash, object.machine.energyCapacity);
                HashNumber(hash, object.machine.energyPerTick);
                HashNumber(hash, object.machine.gridPowered);
                break;
            case ObjectBehaviorKind::Generator:
                HashNumber(hash, 1); // Generator implementation/state version.
                HashString(hash, content.GetItem(object.generator.fuelItem).id);
                HashNumber(hash, object.generator.fuelCapacity);
                HashNumber(hash, object.generator.energyPerFuel);
                HashNumber(hash, object.generator.outputPerTick);
                break;
            case ObjectBehaviorKind::GridBattery:
                HashNumber(hash, 1); // Grid battery registration/state version.
                break;
            }
        }
    }
    HashNumber(hash, content.ToolTraitCount());
    for (std::size_t i = 0; i < content.ToolTraitCount(); ++i) {
        const auto& trait = content.GetToolTrait({static_cast<std::uint8_t>(i + 1)});
        HashString(hash, trait.id); HashString(hash, trait.name);
    }
    HashNumber(hash, content.CraftingRecipeCount());
    for (std::size_t i = 0; i < content.CraftingRecipeCount(); ++i) {
        const auto& recipe = content.GetCraftingRecipe({static_cast<std::uint32_t>(i + 1)});
        HashString(hash, recipe.id); HashString(hash, recipe.name);
        HashInputs(hash, recipe.inputs); HashTraits(hash, recipe.toolTraits);
        HashNumber(hash, recipe.result.item.value); HashNumber(hash, recipe.result.count);
        HashNumber(hash, recipe.work); HashNumber(hash, recipe.enabled);
        HashRequirements(hash, {recipe.skills.data(), recipe.skills.size()});
        HashTraining(hash, {recipe.training.data(), recipe.training.size()});
        HashNumber(hash, recipe.knownByDefault); HashNumber(hash, recipe.autolearn);
        HashRequirements(hash, {recipe.autolearnSkills.data(), recipe.autolearnSkills.size()});
        HashNumber(hash, recipe.craftsToLearn); HashNumber(hash, recipe.disassembliesToLearn);
    }
    HashNumber(hash, content.RecyclingRecipeCount());
    for (std::size_t i = 0; i < content.RecyclingRecipeCount(); ++i) {
        const auto& recipe = content.GetRecyclingRecipe({static_cast<std::uint32_t>(i + 1)});
        HashString(hash, recipe.id); HashString(hash, recipe.name); HashNumber(hash, recipe.target.value);
        HashInputs(hash, recipe.inputs); HashTraits(hash, recipe.toolTraits);
        HashNumber(hash, recipe.returns.Size());
        for (const auto& output : recipe.returns) {
            HashNumber(hash, output.item.value); HashNumber(hash, output.count);
        }
        HashNumber(hash, recipe.work);
        HashRequirements(hash, {recipe.skills.data(), recipe.skills.size()});
        HashTraining(hash, {recipe.training.data(), recipe.training.size()});
        HashNumber(hash, recipe.teachesRecipes.size());
        for (const auto taught : recipe.teachesRecipes) HashNumber(hash, taught.value);
    }
    // Catalog IDs are assigned lexically after all sources have been loaded.
    // Preserve authored entry order: weighted draws and placement keys use it.
    HashNumber(hash, content.LootPoolCount());
    for (std::size_t i = 0; i < content.LootPoolCount(); ++i) {
        const auto& pool = content.GetLootPool({static_cast<std::uint32_t>(i + 1)});
        HashString(hash, pool.id); HashNumber(hash, pool.entries.size());
        for (const auto& entry : pool.entries) {
            HashNumber(hash, static_cast<std::uint64_t>(entry.kind));
            HashNumber(hash, entry.item.value); HashNumber(hash, entry.pool.value);
            HashNumber(hash, entry.weight); HashNumber(hash, entry.count.min); HashNumber(hash, entry.count.max);
        }
    }
    HashNumber(hash, content.MonsterCount());
    for (std::size_t i = 0; i < content.MonsterCount(); ++i) {
        const auto& monster = content.GetMonster({static_cast<std::uint32_t>(i + 1)});
        HashString(hash, monster.id); HashString(hash, monster.name);
        HashNumber(hash, monster.aiProfile.value); HashNumber(hash, static_cast<std::uint64_t>(monster.body));
        HashNumber(hash, static_cast<std::uint64_t>(monster.faction)); HashNumber(hash, monster.speed);
        HashNumber(hash, monster.attackDamage); HashNumber(hash, monster.accuracy);
    }
    HashNumber(hash, content.MonsterGroupCount());
    for (std::size_t i = 0; i < content.MonsterGroupCount(); ++i) {
        const auto& group = content.GetMonsterGroup({static_cast<std::uint32_t>(i + 1)});
        HashString(hash, group.id); HashNumber(hash, group.entries.size());
        for (const auto& entry : group.entries) {
            HashNumber(hash, entry.monster.value); HashNumber(hash, entry.weight);
        }
    }
    HashNumber(hash, content.AiProfileCount());
    for (std::size_t i = 0; i < content.AiProfileCount(); ++i) {
        const auto& profile = content.GetAiProfile({static_cast<std::uint32_t>(i + 1)});
        HashString(hash, profile.id); HashNumber(hash, profile.sightRange);
        HashNumber(hash, profile.dimRange); HashNumber(hash, profile.darkRange);
        HashNumber(hash, profile.scanIntervalTicks); HashNumber(hash, profile.memoryTicks);
        HashNumber(hash, profile.searchTicks); HashNumber(hash, profile.candidateBudget);
        HashNumber(hash, profile.bucketBudget); HashNumber(hash, profile.switchAdvantage);
    }
    HashNumber(hash, content.SkillCount());
    for (std::size_t i = 0; i < content.SkillCount(); ++i) {
        const auto& skill = content.GetSkill({static_cast<std::uint32_t>(i + 1)});
        HashString(hash, skill.id); HashString(hash, skill.name);
        HashNumber(hash, static_cast<std::uint8_t>(skill.category));
        for (const auto xp : skill.xpToNext) HashNumber(hash, xp);
    }
    HashNumber(hash, content.BookCount());
    for (std::size_t i = 0; i < content.BookCount(); ++i) {
        const auto& book = content.GetBook({static_cast<std::uint32_t>(i + 1)});
        HashString(hash, book.id); HashString(hash, book.name); HashNumber(hash, book.studyWork);
        HashNumber(hash, book.benefits.size());
        for (const auto& benefit : book.benefits) {
            HashNumber(hash, benefit.skill.value); HashNumber(hash, benefit.firstLevel); HashNumber(hash, benefit.lastLevel);
            HashNumber(hash, benefit.directXp); HashNumber(hash, benefit.theoryXp); HashNumber(hash, benefit.multiplier);
        }
        HashNumber(hash, book.recipes.size());
        for (const auto recipe : book.recipes) HashNumber(hash, recipe.value);
    }
    HashData(hash, WriteCombatContent(content));
    return hash;
}

NetworkResult<eastl::unique_ptr<NetworkRuntime>> NetworkRuntime::Create() {
    // ENet 1.3.18's ordinary initialize/deinitialize pair uses balanced
    // WSAStartup/WSACleanup and timeBeginPeriod/timeEndPeriod on Windows;
    // Unix initialization is stateless. Independent runtimes may coexist.
    // Do not use initialize_with_callbacks or time_set: those mutate ENet globals.
    if (enet_initialize() != 0) return eastl::unexpected(NetworkError::Initialization);
    return eastl::unique_ptr<NetworkRuntime>(new NetworkRuntime());
}
NetworkRuntime::~NetworkRuntime() { enet_deinitialize(); }

const char* NetworkErrorMessage(NetworkError error) {
    switch (error) {
    case NetworkError::Initialization: return "Network initialization failed";
    case NetworkError::SocketFailure: return "Network socket unavailable";
    case NetworkError::InvalidSettings: return "Invalid network settings";
    case NetworkError::ConnectionFailed: return "Connection failed";
    case NetworkError::Disconnected: return "Disconnected from host";
    case NetworkError::Rejected: return "Host rejected the connection";
    case NetworkError::IncompatibleContent: return "Incompatible game content";
    case NetworkError::Protocol: return "Invalid network message";
    case NetworkError::Timeout: return "Connection timed out";
    case NetworkError::Full: return "Session is full";
    case NetworkError::UnknownToken: return "Unknown reconnect token";
    case NetworkError::AlreadyConnected: return "Player is already connected";
    case NetworkError::InvalidCommand: return "Invalid command";
    case NetworkError::SnapshotTooLarge: return "Snapshot exceeds network limit";
    case NetworkError::SlowPeer: return "Connection stopped acknowledging snapshots";
    case NetworkError::Busy: return "An inspection request or result is still pending";
    }
    return "Unknown network error";
}

class LanHost::Impl {
public:
    Impl(NetworkRuntime&, Server& server, const ContentCatalog& content)
        : server_(server), fingerprint_(ContentFingerprint(content)) {}
    struct Guest {
        ENetPeer* peer = nullptr;
        enet_uint32 connectId = 0;
        std::uint64_t nonce = 0;
        PersistentId player = 0;
        bool accepted = false;
        bool active = false;
        bool closing = false;
        std::uint64_t age = 0;
        std::uint64_t outstandingRevision = 0;
        bool awaitingSnapshot = false;
        std::uint64_t snapshotSentAt = 0;
        std::uint64_t lastSnapshotAt = 0;
        std::uint64_t lastInspection = 0;
        bool inspectedThisPump = false;
        bool awaitingInspection = false;
        std::uint64_t inspectionSentAt = 0;
    };
    struct Input {
        ENetPeer* peer;
        enet_uint32 connectId;
        std::uint64_t nonce;
        PersistentId player;
        PlayerInput value;
    };
    Server& server_;
    std::uint64_t fingerprint_;
    ENetHost* host_ = nullptr;
    ENetSocket discovery_ = ENET_SOCKET_NULL;
    HostSettings settings_;
    std::uint64_t instance_ = 0;
    std::uint64_t clock_ = 0;
    std::uint64_t localSequence_ = 0;
    Vector<Guest> guests_;
    Vector<Input> inputs_;
    Vector<PlayerInput> localInputs_;
    struct Arrival {
        ENetPeer* peer;
        enet_uint32 connectId;
        ENetEventType type;
        Vector<std::uint8_t> bytes;
        enet_uint8 channel = 0;
    };
    Vector<Arrival> arrivals_;
    eastl::optional<NetworkError> networkError_;
    PlayerPresentation presentationScratch_;

    Guest* Find(ENetPeer* peer, enet_uint32 connectId) {
        for (auto& guest : guests_) if (guest.peer == peer && guest.connectId == connectId) return &guest;
        return nullptr;
    }
    void Fail(Guest& guest, NetworkError reason) {
        networkError_ = reason;
        guest.closing = true;
        Drop(guest);
        if (guest.peer && guest.peer->connectID == guest.connectId)
            enet_peer_disconnect_now(guest.peer, 0);
        guest.peer = nullptr;
    }
    void Reject(Guest& guest, NetworkError reason) {
        if (guest.closing) return;
        if (!guest.peer || guest.peer->connectID != guest.connectId) { Fail(guest, reason); return; }
        auto w = Message(wire::Type::Rejection); w.U8(static_cast<std::uint8_t>(reason));
        if (!Send(guest.peer, 0, w)) { Fail(guest, reason); return; }
        guest.closing = true;
        enet_peer_disconnect_later(guest.peer, 0);
        Drop(guest);
    }
    void Drop(Guest& guest) {
        if (guest.player) server_.DisconnectPlayer(guest.player);
        guest.player = 0; guest.active = false; guest.accepted = false;
        guest.awaitingSnapshot = false;
        guest.awaitingInspection = false;
    }
    void Snapshot(Guest& guest, bool initial = false, bool force = false) {
        if (!guest.player || !guest.peer || guest.peer->connectID != guest.connectId || guest.peer->state != ENET_PEER_STATE_CONNECTED) return;
        if (guest.awaitingSnapshot) {
            if (clock_ - guest.snapshotSentAt > 5000000) Fail(guest, NetworkError::SlowPeer);
            return;
        }
        if (!initial && !force && clock_ - guest.lastSnapshotAt < 50000) return;
        if (!initial && !force && guest.outstandingRevision == server_.GetRevision()) return;
        const auto view = server_.ViewForPlayer(guest.player, wire::maxViewRadius);
        auto& presentation = presentationScratch_;
        auto w = Message(wire::Type::Snapshot);
        w.U64(guest.nonce);
        if (!CapturePlayerPresentation(server_.GetWorld(), view, presentation) || !w.View(view) || !w.Presentation(view, presentation)) {
            networkError_ = NetworkError::SnapshotTooLarge;
            Reject(guest, NetworkError::SnapshotTooLarge);
            return;
        }
        if (!Send(guest.peer, initial ? 0 : 1, w)) { Fail(guest, NetworkError::SocketFailure); return; }
        guest.outstandingRevision = view.revision; guest.snapshotSentAt = clock_;
        guest.lastSnapshotAt = clock_; guest.awaitingSnapshot = true;
    }
    void Handle(Guest& guest, wire::Type type, wire::Reader& r) {
        if (guest.closing) return;
        if (type == wire::Type::Hello && !guest.accepted) {
            String name; std::uint64_t fingerprint, token;
            if (!r.U64(fingerprint) || !r.Text(name, 32) || !r.U64(token) || !r.Done()) {
                Reject(guest, NetworkError::Protocol); return;
            }
            if (fingerprint != fingerprint_) { Reject(guest, NetworkError::IncompatibleContent); return; }
            auto joined = server_.JoinPlayer(name, token, RandomToken());
            if (!joined) {
                NetworkError reason = NetworkError::Rejected;
                if (joined.error() == SessionError::Full) reason = NetworkError::Full;
                if (joined.error() == SessionError::UnknownToken) reason = NetworkError::UnknownToken;
                if (joined.error() == SessionError::AlreadyConnected) reason = NetworkError::AlreadyConnected;
                Reject(guest, reason);
                return;
            }
            guest.player = joined->player; guest.accepted = true; guest.age = 0;
            guest.nonce = RandomToken();
            auto response = Message(wire::Type::Accepted);
            response.U64(joined->player); response.U64(joined->reconnectToken); response.U64(guest.nonce);
            if (!Send(guest.peer, 0, response)) { Fail(guest, NetworkError::SocketFailure); return; }
            Snapshot(guest, true);
        } else if (type == wire::Type::SnapshotAck && guest.accepted) {
            std::uint64_t nonce, revision;
            if (!r.U64(nonce) || !r.U64(revision) || !r.Done() || nonce != guest.nonce ||
                !guest.awaitingSnapshot || revision != guest.outstandingRevision) {
                Reject(guest, NetworkError::Protocol); return;
            }
            guest.awaitingSnapshot = false;
        } else if (type == wire::Type::Ready && guest.accepted && !guest.active) {
            std::uint64_t nonce, id;
            if (!r.U64(nonce) || !r.U64(id) || !r.Done() ||
                nonce != guest.nonce || id != guest.player || guest.awaitingSnapshot) {
                Reject(guest, NetworkError::Protocol); return;
            }
            if (!server_.ActivatePlayer(id)) { Reject(guest, NetworkError::Rejected); return; }
            guest.active = true;
            Snapshot(guest, false, true);
        } else if (type == wire::Type::InspectionAck && guest.active) {
            std::uint64_t nonce, id;
            if (!r.U64(nonce) || nonce != guest.nonce || !r.U64(id) || id != guest.lastInspection ||
                !guest.awaitingInspection || !r.Done()) { Reject(guest, NetworkError::Protocol); return; }
            guest.awaitingInspection = false;
        } else if (type == wire::Type::Inspect && guest.active) {
            std::uint64_t nonce, id; TileCoord cursor; std::uint8_t radius;
            if (!r.U64(nonce) || nonce != guest.nonce || !r.U64(id) || id <= guest.lastInspection ||
                !r.I32(cursor.x) || !r.I32(cursor.z) || !r.I32(cursor.y) || !ValidPosition(cursor) ||
                !r.U8(radius) || radius > wire::maxViewRadius || !r.Done() || guest.inspectedThisPump || guest.awaitingInspection) {
                Reject(guest, NetworkError::Protocol); return;
            }
            guest.inspectedThisPump = true; guest.lastInspection = id;
            PlayerInspection inspection;
            CapturePlayerInspection(server_, guest.player, id, cursor, radius, inspection);
            auto response = Message(wire::Type::Inspection); response.U64(guest.nonce);
            if (!response.Inspection(inspection)) {
                inspection = {}; inspection.requestId = id; inspection.cursor = cursor; inspection.status = InspectionStatus::TooLarge;
                response = Message(wire::Type::Inspection); response.U64(guest.nonce);
                if (!response.Inspection(inspection)) { Fail(guest, NetworkError::Protocol); return; }
            }
            if (!Send(guest.peer, 0, response)) Fail(guest, NetworkError::SocketFailure);
            else { guest.awaitingInspection = true; guest.inspectionSentAt = clock_; }
        } else if (type == wire::Type::Input && guest.active) {
            std::uint64_t nonce;
            PlayerInput input;
            if (!r.U64(nonce) || nonce != guest.nonce || !r.U64(input.sequence) || !input.sequence ||
                !r.U64(input.opportunity) || !input.opportunity || !r.CommandValue(input.command) || !r.Done()) {
                Reject(guest, NetworkError::Protocol); return;
            }
            if (inputs_.size() >= 128) { Fail(guest, NetworkError::SlowPeer); return; }
            inputs_.push_back({guest.peer, guest.connectId, guest.nonce, guest.player, input});
        } else { Reject(guest, NetworkError::Protocol); }
    }
    void Discovery() {
        if (discovery_ == ENET_SOCKET_NULL) return;
        std::uint8_t buffer[512]; ENetAddress source{}; wire::Reader r;
        for (int i = 0; i < 32 && Receive(discovery_, source, r, buffer, sizeof buffer); ++i) {
            std::uint32_t magic; std::uint8_t type; std::uint64_t nonce;
            if (!r.U32(magic) || magic != wire::protocol || !r.U8(type) ||
                type != static_cast<std::uint8_t>(wire::Type::Query) || !r.U64(nonce) || !r.Done()) continue;
            auto w = Message(wire::Type::Advertisement);
            ENetAddress bound{};
            if (enet_socket_get_address(host_->socket, &bound) != 0) continue;
            w.U64(nonce); w.U64(instance_); w.U64(fingerprint_); w.U16(bound.port);
            w.Text({settings_.sessionName.data(), settings_.sessionName.size()}, 64); w.Text("Host", 48);
            std::uint8_t count = 0;
            for (const auto& p : server_.GetPlayers()) if (p.state == PlayerConnectionState::Active) ++count;
            w.U8(count); w.U8(4); Broadcast(discovery_, source, w);
        }
    }
};

LanHost::LanHost(NetworkRuntime& runtime, Server& server, const ContentCatalog& content)
    : impl_(new Impl(runtime, server, content)) {}
LanHost::~LanHost() { Close(); }
NetworkResult<void> LanHost::Open(HostSettings settings) {
    wire::Writer nameCheck;
    if (impl_->host_ || !nameCheck.Text({settings.sessionName.data(), settings.sessionName.size()}, 64))
        return eastl::unexpected(NetworkError::InvalidSettings);
    impl_->arrivals_.clear(); impl_->inputs_.clear(); impl_->localInputs_.clear();
    impl_->networkError_.reset();
    ENetAddress address{ENET_HOST_ANY, settings.port};
    impl_->host_ = enet_host_create(&address, 5, 2, 0, 0);
    if (!impl_->host_) return eastl::unexpected(NetworkError::SocketFailure);
    impl_->host_->maximumPacketSize = wire::maxPacket;
    impl_->host_->maximumWaitingData = 2 * wire::maxPacket;
    impl_->discovery_ = OpenDatagram(settings.discoveryPort);
    if (impl_->discovery_ == ENET_SOCKET_NULL) { Close(); return eastl::unexpected(NetworkError::SocketFailure); }
    impl_->settings_ = eastl::move(settings); impl_->instance_ = RandomToken(); return {};
}
void LanHost::Close() {
    if (!impl_->host_) {
        impl_->arrivals_.clear(); impl_->inputs_.clear(); impl_->localInputs_.clear();
        return;
    }
    for (auto& guest : impl_->guests_) {
        if (guest.peer && guest.peer->state == ENET_PEER_STATE_CONNECTED) {
            // Closing must not drain obsolete snapshots first. A fragmented
            // snapshot exceeds ENet's reliable window, and clients may not be
            // pumped while this synchronous Close runs. disconnect_later would
            // wait for their ACKs and strand the notification behind that data.
            // disconnect cancels queued traffic and sends an acknowledged
            // transport control command independently of the data channels.
            enet_peer_disconnect(guest.peer, 0);
        }
        impl_->Drop(guest);
    }
    enet_host_flush(impl_->host_);
    // Service disconnect acknowledgements for at most 200 ms, then release transport.
    for (int attempt = 0; attempt < 20; ++attempt) {
        bool pending = false;
        for (const auto& guest : impl_->guests_)
            if (guest.peer && guest.peer->state != ENET_PEER_STATE_DISCONNECTED) pending = true;
        if (!pending) break;
        ENetEvent event{};
        if (enet_host_service(impl_->host_, &event, 10) > 0 && event.type == ENET_EVENT_TYPE_RECEIVE)
            enet_packet_destroy(event.packet);
    }
    enet_host_destroy(impl_->host_); impl_->host_ = nullptr; impl_->guests_.clear();
    impl_->arrivals_.clear(); impl_->inputs_.clear(); impl_->localInputs_.clear();
    if (impl_->discovery_ != ENET_SOCKET_NULL) enet_socket_destroy(impl_->discovery_);
    impl_->discovery_ = ENET_SOCKET_NULL;
}
bool LanHost::IsOpen() const { return impl_->host_ != nullptr; }
std::uint16_t LanHost::GetPort() const {
    if (!impl_->host_) return 0;
    ENetAddress bound{};
    return enet_socket_get_address(impl_->host_->socket, &bound) == 0 ? bound.port : 0;
}
std::uint16_t LanHost::GetDiscoveryPort() const {
    if (impl_->discovery_ == ENET_SOCKET_NULL) return 0;
    ENetAddress address{};
    return enet_socket_get_address(impl_->discovery_, &address) == 0 ? address.port : 0;
}
std::uint64_t LanHost::QueueLocal(Command command) {
    return QueueLocal(eastl::move(command), impl_->server_.GetInputContext(impl_->server_.GetHostPlayerId()).opportunity);
}
std::uint64_t LanHost::QueueLocal(Command command, std::uint64_t opportunity) {
    if (!opportunity) return 0;
    const auto view = impl_->server_.GetInputContext(impl_->server_.GetHostPlayerId());
    if (impl_->localSequence_ < view.lastInputSequence) impl_->localSequence_ = view.lastInputSequence;
    if (impl_->localSequence_ == UINT64_MAX || impl_->localInputs_.size() >= 128) return 0;
    const auto sequence = ++impl_->localSequence_;
    impl_->localInputs_.push_back({sequence, opportunity, eastl::move(command)}); return sequence;
}
HostUpdate LanHost::Pump(std::uint64_t elapsed) {
    HostUpdate result;
    // Elapsed time belongs to the preceding frame: commands observed now cannot
    // retroactively beat deadlines already crossed during this interval.
    auto before = impl_->server_.Update(elapsed);
    result.commandResults = eastl::move(before.commandResults);
    result.storageError = before.storageError;
    if (impl_->host_) {
        impl_->clock_ = SaturatingAdd(impl_->clock_, elapsed);
        for (auto& guest : impl_->guests_) guest.inspectedThisPump = false;
        ENetEvent event{};
        for (int i = 0; i < 256 && enet_host_service(impl_->host_, &event, 0) > 0; ++i) {
            if (event.type == ENET_EVENT_TYPE_CONNECT || event.type == ENET_EVENT_TYPE_DISCONNECT) {
                impl_->arrivals_.push_back({event.peer, event.peer->connectID, event.type, {}});
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                Impl::Arrival arrival{event.peer, event.peer->connectID, event.type, {}};
                arrival.channel = event.channelID;
                if (event.packet->dataLength <= wire::maxPacket)
                    for (std::size_t j = 0; j < event.packet->dataLength; ++j)
                        arrival.bytes.push_back(event.packet->data[j]);
                impl_->arrivals_.push_back(eastl::move(arrival));
                enet_packet_destroy(event.packet);
            }
        }
        for (const auto& arrival : impl_->arrivals_) {
            if (arrival.type == ENET_EVENT_TYPE_DISCONNECT) {
                // ENet resets connectID before returning a disconnect event.
                // Lifecycle events are applied in service order, so the current
                // record for this peer is the retiring connection even if ENet
                // reused its storage later in the same poll.
                for (auto& guest : impl_->guests_) {
                    if (guest.peer == arrival.peer) {
                        impl_->Drop(guest);
                        guest.peer = nullptr;
                        break;
                    }
                }
                continue;
            }
            if (arrival.type == ENET_EVENT_TYPE_CONNECT) {
                impl_->guests_.push_back({arrival.peer, arrival.connectId});
                continue;
            }
            auto* guest = impl_->Find(arrival.peer, arrival.connectId);
            if (!guest) continue;
            if (arrival.peer->connectID != arrival.connectId) continue;
            ENetPacket packet{};
            packet.data = const_cast<enet_uint8*>(arrival.bytes.data());
            packet.dataLength = arrival.bytes.size();
            wire::Reader reader; wire::Type type{};
            if (arrival.channel == 0 && Parse(&packet, reader, type)) impl_->Handle(*guest, type, reader);
            else impl_->Reject(*guest, NetworkError::Protocol);
        }
        impl_->arrivals_.clear();
        for (std::size_t i = 0; i < impl_->guests_.size();) {
            if (!impl_->guests_[i].peer) impl_->guests_.erase(impl_->guests_.begin() + i);
            else ++i;
        }
        for (auto& guest : impl_->guests_) {
            if (!guest.peer || guest.active) continue;
            // New connections began at this poll boundary, not at the start of elapsed.
            if (guest.age == 0) { guest.age = 1; continue; }
            guest.age = SaturatingAdd(guest.age, elapsed);
            if (guest.age > 5000000) { impl_->Drop(guest); enet_peer_disconnect(guest.peer, 0); }
        }
        impl_->Discovery();
    }
    for (const auto& input : impl_->localInputs_) {
        auto outcome = impl_->server_.Submit(impl_->server_.GetHostPlayerId(), input);
        result.commandResults.push_back({impl_->server_.GetHostPlayerId(), input.sequence, input.opportunity, outcome});
    }
    impl_->localInputs_.clear();
    for (const auto& input : impl_->inputs_) {
        auto* guest = impl_->Find(input.peer, input.connectId);
        if (!guest || !guest->active || guest->nonce != input.nonce ||
            guest->player != input.player || guest->peer->connectID != input.connectId) continue;
        auto outcome = impl_->server_.Submit(guest->player, input.value);
        PlayerCommandResult receipt{guest->player, input.value.sequence, input.value.opportunity, outcome};
        result.commandResults.push_back(receipt);
    }
    impl_->inputs_.clear();
    auto after = impl_->server_.Update(0);
    for (const auto& receipt : after.commandResults) result.commandResults.push_back(receipt);
    if (after.storageError) result.storageError = after.storageError;
    for (const auto& receipt : result.commandResults) {
        for (auto& guest : impl_->guests_) if (guest.active && guest.player == receipt.player) {
            auto w = Message(wire::Type::Receipt);
            const auto state = impl_->server_.GetInputContext(guest.player);
            w.U64(guest.nonce);
            if (!w.Result(receipt)) { impl_->Fail(guest, NetworkError::Protocol); continue; }
            w.U64(state.opportunity); w.U64(state.revision); w.U64(state.lastInputSequence);
            if (!Send(guest.peer, 0, w)) impl_->Fail(guest, NetworkError::SocketFailure);
        }
    }
    for (auto& guest : impl_->guests_) if (guest.accepted) {
        if (guest.awaitingInspection && impl_->clock_ - guest.inspectionSentAt > 5000000) impl_->Fail(guest, NetworkError::SlowPeer);
        else impl_->Snapshot(guest);
    }
    if (impl_->host_) enet_host_flush(impl_->host_);
    result.networkError = impl_->networkError_; impl_->networkError_.reset();
    return result;
}

class LanClient::Impl {
public:
    Impl(NetworkRuntime&, const ContentCatalog& content)
        : fingerprint_(ContentFingerprint(content)), content_(content) {}
    std::uint64_t fingerprint_;
    const ContentCatalog& content_;
    ENetHost* host_ = nullptr;
    ENetPeer* peer_ = nullptr;
    ConnectionSettings settings_;
    ConnectionState state_ = ConnectionState::Disconnected;
    NetworkError error_ = NetworkError::Disconnected;
    WorldView view_;
    PlayerPresentation presentation_;
    PlayerPresentation incomingPresentation_;
    eastl::optional<PlayerInspection> inspection_;
    std::uint64_t pendingInspection_ = 0;
    std::uint64_t lastInspection_ = 0;
    std::uint64_t inspectionAge_ = 0;
    TileCoord inspectionCursor_{};
    int inspectionRadius_ = 0;
    std::uint64_t authoritativeSequence_ = 0;
    bool hasView_ = false;
    std::uint64_t token_ = 0;
    std::uint64_t player_ = 0;
    std::uint64_t nonce_ = 0;
    std::uint64_t sequence_ = 0;
    std::uint64_t submitOpportunity_ = 0;
    std::uint64_t contextRevision_ = 0;
    std::uint64_t age_ = 0;
    bool readySent_ = false;
    Vector<PlayerCommandResult> results_;
    Vector<std::uint64_t> pendingSends_;
    void EndInspection() {
        presentation_.mapItems.clear(); presentation_.interactions.clear();
        if (!pendingInspection_) return;
        PlayerInspection result; result.requestId = pendingInspection_; result.cursor = inspectionCursor_;
        result.status = InspectionStatus::Disconnected;
        inspection_ = eastl::move(result); pendingInspection_ = 0;
    }
    void Fail(NetworkError error) {
        error_ = error;
        state_ = ConnectionState::Rejected;
        hasView_ = false;
        view_ = {};
        EndInspection();
    }
    void ProtocolFailure() { Fail(NetworkError::Protocol); }
};
LanClient::LanClient(NetworkRuntime& runtime, const ContentCatalog& content) : impl_(new Impl(runtime, content)) {}
LanClient::~LanClient() { Disconnect(); }
NetworkResult<void> LanClient::Connect(ConnectionSettings settings) {
    wire::Writer nameCheck;
    if (!settings.port || settings.playerName.empty() ||
        !nameCheck.Text({settings.playerName.data(), settings.playerName.size()}, 32))
        return eastl::unexpected(NetworkError::InvalidSettings);
    ENetAddress address{}; address.port = settings.port;
    if (enet_address_set_host(&address, settings.address.c_str()) != 0)
        return eastl::unexpected(NetworkError::InvalidSettings);
    Disconnect();
    impl_->host_ = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!impl_->host_) return eastl::unexpected(NetworkError::SocketFailure);
    impl_->host_->maximumPacketSize = wire::maxPacket;
    // One snapshot and one inspection may arrive on independent channels.
    // Leave bounded headroom for receipts/control while both are fragmented.
    impl_->host_->maximumWaitingData = 3 * wire::maxPacket;
    impl_->peer_ = enet_host_connect(impl_->host_, &address, 2, 0);
    if (!impl_->peer_) { Disconnect(); return eastl::unexpected(NetworkError::ConnectionFailed); }
    impl_->settings_ = eastl::move(settings); impl_->state_ = ConnectionState::Connecting;
    impl_->age_ = 0; impl_->hasView_ = false; impl_->player_ = 0;
    impl_->token_ = 0; impl_->nonce_ = 0; impl_->sequence_ = 0;
    impl_->submitOpportunity_ = 0; impl_->contextRevision_ = 0; impl_->readySent_ = false;
    impl_->inspection_.reset(); impl_->pendingInspection_ = 0; impl_->lastInspection_ = 0; impl_->authoritativeSequence_ = 0;
    impl_->results_.clear(); impl_->pendingSends_.clear(); return {};
}
void LanClient::Disconnect() {
    if (impl_->peer_) { enet_peer_disconnect_now(impl_->peer_, 0); impl_->peer_ = nullptr; }
    if (impl_->host_) { enet_host_destroy(impl_->host_); impl_->host_ = nullptr; }
    impl_->state_ = ConnectionState::Disconnected; impl_->hasView_ = false;
    impl_->error_ = NetworkError::Disconnected;
    impl_->view_ = {};
    impl_->EndInspection();
}
void LanClient::Pump(std::uint64_t elapsed) {
    if (!impl_->host_) return;
    impl_->age_ = SaturatingAdd(impl_->age_, elapsed);
    if (impl_->pendingInspection_) impl_->inspectionAge_ = SaturatingAdd(impl_->inspectionAge_, elapsed);
    ENetEvent event{};
    for (int i = 0; i < 256 && enet_host_service(impl_->host_, &event, 0) > 0; ++i) {
        // Terminal state is sticky until Connect. In particular, queued packets
        // on the other reliable channel must not replace the original failure.
        if (event.peer != impl_->peer_ || impl_->state_ == ConnectionState::Rejected ||
            impl_->state_ == ConnectionState::Disconnected) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(event.packet);
            continue;
        }
        if (event.type == ENET_EVENT_TYPE_CONNECT) {
            if (impl_->state_ != ConnectionState::Connecting) { impl_->ProtocolFailure(); continue; }
            auto w = Message(wire::Type::Hello);
            w.U64(impl_->fingerprint_);
            w.Text({impl_->settings_.playerName.data(), impl_->settings_.playerName.size()}, 32);
            w.U64(impl_->settings_.reconnectToken);
            if (!Send(impl_->peer_, 0, w)) impl_->ProtocolFailure();
            else { impl_->state_ = ConnectionState::Joining; impl_->age_ = 0; }
        } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            if (impl_->state_ != ConnectionState::Rejected) {
                impl_->error_ = NetworkError::Disconnected; impl_->state_ = ConnectionState::Disconnected;
            }
            impl_->hasView_ = false; impl_->view_ = {};
        } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            wire::Reader r; wire::Type type{};
            if (!Parse(event.packet, r, type) || (event.channelID != 0 && type != wire::Type::Snapshot)) {
                impl_->ProtocolFailure();
            } else if (type == wire::Type::Accepted && impl_->state_ == ConnectionState::Joining &&
                !impl_->player_) {
                if (!r.U64(impl_->player_) || !r.U64(impl_->token_) ||
                    !r.U64(impl_->nonce_) || !r.Done() ||
                    !impl_->player_ || !impl_->token_ || !impl_->nonce_) impl_->ProtocolFailure();
            } else if (type == wire::Type::Snapshot && impl_->player_ &&
                (impl_->state_ == ConnectionState::Joining || impl_->state_ == ConnectionState::Active)) {
                std::uint64_t nonce = 0;
                WorldView view;
                auto& presentation = impl_->incomingPresentation_;
                bool valid = r.U64(nonce) && nonce == impl_->nonce_ && r.View(view) &&
                    r.Presentation(view, presentation) && wire::ValidPresentationDefinitions(presentation, impl_->content_) &&
                    r.Done() && view.playerId == impl_->player_ && wire::ValidItemDefinitions(view, impl_->content_) &&
                    wire::ValidTerrainDefinitions(view, impl_->content_) && wire::ValidMonsterDefinitions(view, impl_->content_)
                    && wire::ValidObjectDefinitions(view, impl_->content_);
                if (!valid) {
                    impl_->ProtocolFailure();
                } else {
                    auto ack = Message(wire::Type::SnapshotAck);
                    ack.U64(impl_->nonce_); ack.U64(view.revision);
                    if (!Send(impl_->peer_, 0, ack)) impl_->ProtocolFailure();
                    else {
                        bool active = false;
                        bool found = false;
                        for (const auto& p : view.players) if (p.id == impl_->player_) {
                            found = true;
                            active = p.state == PlayerConnectionState::Active;
                        }
                        if (!found || (impl_->state_ == ConnectionState::Active && !active))
                            impl_->ProtocolFailure();
                        if (impl_->state_ == ConnectionState::Joining && !impl_->readySent_) {
                            auto ready = Message(wire::Type::Ready);
                            ready.U64(impl_->nonce_); ready.U64(impl_->player_);
                            if (!Send(impl_->peer_, 0, ready)) impl_->ProtocolFailure();
                            else impl_->readySent_ = true;
                        }
                        if (active && impl_->state_ != ConnectionState::Rejected &&
                            (!impl_->hasView_ || view.revision >= impl_->view_.revision)) {
                            impl_->state_ = ConnectionState::Active;
                            impl_->hasView_ = true;
                            impl_->view_ = eastl::move(view);
                            impl_->presentation_.mapItems.swap(presentation.mapItems);
                            impl_->presentation_.interactions.swap(presentation.interactions);
                            if (impl_->sequence_ < impl_->view_.lastInputSequence)
                                impl_->sequence_ = impl_->view_.lastInputSequence;
                            if (impl_->contextRevision_ <= impl_->view_.revision) {
                                impl_->contextRevision_ = impl_->view_.revision;
                                impl_->submitOpportunity_ = impl_->view_.opportunity;
                                impl_->authoritativeSequence_ = impl_->view_.lastInputSequence;
                            }
                        }
                    }
                }
            } else if (type == wire::Type::Inspection && impl_->state_ == ConnectionState::Active) {
                std::uint64_t nonce; PlayerInspection inspection;
                if (!r.U64(nonce) || nonce != impl_->nonce_ || !r.Inspection(inspection) || !r.Done() ||
                    !wire::ValidInspectionDefinitions(inspection, impl_->content_) ||
                    (inspection.status == InspectionStatus::Complete && (inspection.view.playerId != impl_->player_ ||
                        inspection.view.width != 2 * impl_->inspectionRadius_ + 1))) impl_->ProtocolFailure();
                else if (inspection.requestId == impl_->pendingInspection_ && inspection.cursor == impl_->inspectionCursor_) {
                    auto ack = Message(wire::Type::InspectionAck); ack.U64(impl_->nonce_); ack.U64(inspection.requestId);
                    if (!Send(impl_->peer_, 0, ack)) impl_->Fail(NetworkError::SocketFailure);
                    else { impl_->inspection_ = eastl::move(inspection); impl_->pendingInspection_ = 0; }
                } else impl_->ProtocolFailure();
            } else if (type == wire::Type::Receipt && impl_->state_ == ConnectionState::Active) {
                std::uint64_t nonce, opportunity, revision, lastInputSequence;
                PlayerCommandResult receipt;
                if (!r.U64(nonce) || nonce != impl_->nonce_ || !r.Result(receipt) ||
                    !r.U64(opportunity) || !opportunity || !r.U64(revision) || !r.U64(lastInputSequence) || !r.Done() ||
                    receipt.player != impl_->player_)
                    impl_->ProtocolFailure();
                else if (impl_->results_.size() >= 512) impl_->Fail(NetworkError::SlowPeer);
                else {
                    if (revision >= impl_->contextRevision_) {
                        impl_->contextRevision_ = revision;
                        impl_->submitOpportunity_ = opportunity;
                        impl_->authoritativeSequence_ = lastInputSequence;
                    }
                    for (std::size_t pendingIndex = 0; pendingIndex < impl_->pendingSends_.size(); ++pendingIndex)
                        if (impl_->pendingSends_[pendingIndex] == receipt.sequence) {
                            impl_->pendingSends_.erase(impl_->pendingSends_.begin() + pendingIndex);
                            break;
                        }
                    impl_->results_.push_back(receipt);
                }
            } else if (type == wire::Type::Rejection &&
                (impl_->state_ == ConnectionState::Joining || impl_->state_ == ConnectionState::Active)) {
                std::uint8_t reason;
                if (r.U8(reason) && r.Done() && reason <= static_cast<std::uint8_t>(NetworkError::Busy))
                    impl_->error_ = static_cast<NetworkError>(reason);
                else impl_->error_ = NetworkError::Protocol;
                impl_->state_ = ConnectionState::Rejected; impl_->hasView_ = false; impl_->view_ = {};
            } else if (type == wire::Type::Closing) {
                if (!r.Done()) impl_->ProtocolFailure();
                else {
                    impl_->error_ = NetworkError::Disconnected; impl_->state_ = ConnectionState::Disconnected;
                    impl_->hasView_ = false; impl_->view_ = {};
                }
            } else if (impl_->state_ != ConnectionState::Rejected &&
                impl_->state_ != ConnectionState::Disconnected) impl_->ProtocolFailure();
            enet_packet_destroy(event.packet);
        }
    }
    if ((impl_->state_ == ConnectionState::Connecting || impl_->state_ == ConnectionState::Joining) &&
        impl_->age_ > 5000000) {
        impl_->error_ = NetworkError::Timeout; impl_->state_ = ConnectionState::Rejected;
        impl_->hasView_ = false; impl_->view_ = {};
    }
    if (impl_->state_ == ConnectionState::Active && impl_->pendingInspection_ && impl_->inspectionAge_ > 5000000)
        impl_->Fail(NetworkError::Timeout);
    if (impl_->state_ == ConnectionState::Rejected || impl_->state_ == ConnectionState::Disconnected) {
        impl_->EndInspection();
        // Cancel queued traffic instead of waiting behind an unresponsive peer's
        // reliable window. Teardown occurs in the same pump as the terminal result.
        if (impl_->peer_) { enet_peer_disconnect_now(impl_->peer_, 0); impl_->peer_ = nullptr; }
    }
    enet_host_flush(impl_->host_);
}
NetworkResult<std::uint64_t> LanClient::Submit(Command command) {
    return Submit(eastl::move(command), impl_->submitOpportunity_);
}
NetworkResult<std::uint64_t> LanClient::Submit(Command command, std::uint64_t opportunity) {
    if (impl_->state_ != ConnectionState::Active || !impl_->hasView_)
        return eastl::unexpected(NetworkError::Disconnected);
    if (!opportunity || !ValidCommandShape(command)) return eastl::unexpected(NetworkError::InvalidCommand);
    if (impl_->sequence_ == UINT64_MAX) return eastl::unexpected(NetworkError::Protocol);
    if (impl_->pendingSends_.size() >= 64) return eastl::unexpected(NetworkError::SlowPeer);
    auto w = Message(wire::Type::Input);
    const std::uint64_t sequence = impl_->sequence_ + 1;
    w.U64(impl_->nonce_); w.U64(sequence); w.U64(opportunity);
    if (!w.CommandValue(command)) return eastl::unexpected(NetworkError::InvalidCommand);
    if (!Send(impl_->peer_, 0, w)) return eastl::unexpected(NetworkError::SocketFailure);
    impl_->sequence_ = sequence;
    impl_->pendingSends_.push_back(sequence);
    return sequence;
}
ConnectionState LanClient::GetState() const { return impl_->state_; }
NetworkError LanClient::GetError() const { return impl_->error_; }
bool LanClient::HasView() const { return impl_->hasView_; }
const WorldView& LanClient::View() const { return impl_->view_; }
const PlayerPresentation& LanClient::Presentation() const { return impl_->presentation_; }
PlayerInputContext LanClient::GetInputContext() const {
    if (impl_->state_ != ConnectionState::Active) return {};
    return {impl_->submitOpportunity_, impl_->authoritativeSequence_, impl_->contextRevision_};
}
NetworkResult<void> LanClient::Inspect(std::uint64_t requestId, TileCoord cursor, int radius) {
    if (impl_->state_ != ConnectionState::Active || !impl_->hasView_) return eastl::unexpected(NetworkError::Disconnected);
    if (impl_->pendingInspection_ || impl_->inspection_) return eastl::unexpected(NetworkError::Busy);
    if (!requestId || requestId <= impl_->lastInspection_ || !ValidPosition(cursor) || radius < 0 || radius > wire::maxViewRadius)
        return eastl::unexpected(NetworkError::InvalidSettings);
    const auto dx = static_cast<std::int64_t>(cursor.x) - impl_->view_.player_position.x;
    const auto dz = static_cast<std::int64_t>(cursor.z) - impl_->view_.player_position.z;
    if (dx < -radius || dx > radius || dz < -radius || dz > radius) return eastl::unexpected(NetworkError::InvalidSettings);
    auto w = Message(wire::Type::Inspect);
    w.U64(impl_->nonce_); w.U64(requestId); w.I32(cursor.x); w.I32(cursor.z); w.I32(cursor.y); w.U8(static_cast<std::uint8_t>(radius));
    if (!Send(impl_->peer_, 0, w)) return eastl::unexpected(NetworkError::SocketFailure);
    impl_->pendingInspection_ = requestId; impl_->lastInspection_ = requestId; impl_->inspectionCursor_ = cursor;
    impl_->inspectionRadius_ = radius;
    impl_->inspectionAge_ = 0;
    return {};
}
bool LanClient::TryTakeInspection(PlayerInspection& output) {
    if (!impl_->inspection_) return false;
    output = eastl::move(*impl_->inspection_); impl_->inspection_.reset(); return true;
}
Vector<PlayerCommandResult> LanClient::TakeResults() {
    Vector<PlayerCommandResult> result; result.swap(impl_->results_); return result;
}
std::uint64_t LanClient::GetReconnectToken() const { return impl_->token_; }

class LanDiscovery::Impl {
public:
    Impl(NetworkRuntime&, const ContentCatalog& content) : fingerprint_(ContentFingerprint(content)) {}
    std::uint64_t fingerprint_;
    ENetSocket socket_ = ENET_SOCKET_NULL;
    std::uint64_t clock_ = 0;
    std::uint64_t nonce_ = 0;
    std::uint64_t lastQuery_ = 0;
    std::uint16_t port_ = 24681;
    String address_;
    struct Entry { DiscoveredSession session; std::uint64_t seen; };
    Vector<Entry> entries_;
    Vector<DiscoveredSession> sessions_;
};
LanDiscovery::LanDiscovery(NetworkRuntime& runtime, const ContentCatalog& content) : impl_(new Impl(runtime, content)) {}
LanDiscovery::~LanDiscovery() { Close(); }
NetworkResult<void> LanDiscovery::Open(std::uint16_t localPort) {
    Close(); impl_->socket_ = OpenDatagram(localPort);
    if (impl_->socket_ == ENET_SOCKET_NULL) return eastl::unexpected(NetworkError::SocketFailure);
    return {};
}
void LanDiscovery::Close() {
    if (impl_->socket_ != ENET_SOCKET_NULL) enet_socket_destroy(impl_->socket_);
    impl_->socket_ = ENET_SOCKET_NULL; impl_->entries_.clear(); impl_->sessions_.clear();
}
NetworkResult<void> LanDiscovery::Refresh(std::uint16_t port, const char* address) {
    if (impl_->socket_ == ENET_SOCKET_NULL || !port) return eastl::unexpected(NetworkError::InvalidSettings);
    ENetAddress target{}; target.port = port;
    if (address && *address) {
        if (enet_address_set_host(&target, address) != 0) return eastl::unexpected(NetworkError::InvalidSettings);
        impl_->address_ = address;
    } else { target.host = ENET_HOST_BROADCAST; impl_->address_.clear(); }
    impl_->port_ = port; impl_->nonce_ = RandomToken(); impl_->lastQuery_ = impl_->clock_;
    auto w = Message(wire::Type::Query); w.U64(impl_->nonce_);
    if (!Broadcast(impl_->socket_, target, w)) return eastl::unexpected(NetworkError::SocketFailure);
    return {};
}
void LanDiscovery::Pump(std::uint64_t elapsed) {
    if (impl_->socket_ == ENET_SOCKET_NULL) return;
    impl_->clock_ = SaturatingAdd(impl_->clock_, elapsed);
    if (impl_->nonce_ && impl_->clock_ - impl_->lastQuery_ >= 1000000)
        Refresh(impl_->port_, impl_->address_.empty() ? nullptr : impl_->address_.c_str());
    std::uint8_t buffer[512]; ENetAddress source{}; wire::Reader r;
    for (int i = 0; i < 64 && Receive(impl_->socket_, source, r, buffer, sizeof buffer); ++i) {
        std::uint32_t magic; std::uint8_t type; std::uint64_t nonce, instance, fingerprint;
        std::uint16_t port; DiscoveredSession session;
        if (!r.U32(magic) || magic != wire::protocol || !r.U8(type) ||
            type != static_cast<std::uint8_t>(wire::Type::Advertisement) ||
            !r.U64(nonce) || nonce != impl_->nonce_ || !r.U64(instance) || !instance ||
            !r.U64(fingerprint) || !r.U16(port) || !port ||
            !r.Text(session.sessionName, 64) || !r.Text(session.hostName, 48) ||
            !r.U8(session.playerCount) || !r.U8(session.maxPlayers) ||
            session.maxPlayers > 4 || session.playerCount > session.maxPlayers || !r.Done()) continue;
        char ip[64]{};
        if (enet_address_get_host_ip(&source, ip, sizeof ip) != 0) continue;
        session.address = ip; session.instanceId = instance; session.gamePort = port;
        session.compatible = fingerprint == impl_->fingerprint_;
        bool found = false;
        for (auto& entry : impl_->entries_) if (entry.session.instanceId == instance) {
            entry.session = session; entry.seen = impl_->clock_; found = true; break;
        }
        if (!found && impl_->entries_.size() < 64) impl_->entries_.push_back({session, impl_->clock_});
    }
    for (std::size_t i = 0; i < impl_->entries_.size();) {
        if (impl_->clock_ - impl_->entries_[i].seen > 3000000) impl_->entries_.erase(impl_->entries_.begin() + i);
        else ++i;
    }
    impl_->sessions_.clear();
    for (const auto& entry : impl_->entries_) impl_->sessions_.push_back(entry.session);
}
const Vector<DiscoveredSession>& LanDiscovery::Sessions() const { return impl_->sessions_; }

}
