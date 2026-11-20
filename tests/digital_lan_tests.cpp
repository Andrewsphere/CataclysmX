#include "network/network.h"
#include "network/wire.h"
#include "power/power_system.h"
#include "skills/skill_state.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

namespace {
using namespace cataclysmx;
using namespace cataclysmx::network;

template<class T, class E>
T Take(eastl::expected<T, E> result) {
    REQUIRE(result);
    return eastl::move(*result);
}

// Polls advance the network snapshot clock and explicit simulation time. Small
// default steps retain observable paid-slice boundaries between receipts/views.
struct DigitalLan {
    ContentCatalog content = Take(LoadContentPack(CATACLYSMX_CONTENT_PACK));
    Server server{content, 73};
    eastl::unique_ptr<NetworkRuntime> runtime = Take(NetworkRuntime::Create());
    LanHost host{*runtime, server, content};
    LanClient guest{*runtime, content};
    ActorHandle actor{};
    ObjectHandle printer{};
    ItemHandle battery{};
    PersistentId printerId = 0;
    PersistentId stickId = 0;
    ItemCapabilityId batteryStore{};

    template<class Predicate>
    bool Until(Predicate predicate, std::uint64_t elapsed = 1000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do {
            if (predicate()) return true;
            for (const auto& player : server.GetPlayers()) {
                if (player.id == server.GetHostPlayerId() && player.ready && !player.commandQueued) {
                    host.QueueLocal({CommandKind::wait});
                }
            }
            const auto update = host.Pump(elapsed);
            REQUIRE_FALSE(update.networkError);
            REQUIRE_FALSE(update.storageError);
            guest.Pump(elapsed);
            REQUIRE(guest.GetState() != ConnectionState::Rejected);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        return predicate();
    }

    DigitalLan() {
        auto pacing = server.GetPacing();
        pacing.rateLimited = true;
        pacing.ticksPerSecond = 100;
        pacing.pauseWhenIdle = false;
        REQUIRE(server.SetPacing(pacing));
        REQUIRE(host.Open({0, 0, "Digital integration"}));
        REQUIRE(guest.Connect({"127.0.0.1", host.GetPort(), "Printer operator", 0}));
        REQUIRE(Until([&] { return guest.HasView() && guest.GetState() == ConnectionState::Active; }));
        auto& world = server.GetWorld();
        actor = world.FindActor(guest.View().characterId);
        REQUIRE(actor.value);
        const auto position = world.GetActor(actor)->position;
        TileCoord machinePosition = position;
        ++machinePosition.x;
        TileCoord batteryPosition = machinePosition;
        ++batteryPosition.x;
        for (const auto tile : {position, machinePosition, batteryPosition}) {
            if (auto object = world.ObjectAt(tile); object.value) REQUIRE(world.DestroyObject(object));
            REQUIRE(world.SetTile(tile, {content.FindTerrain("floor"), 0}));
        }
        REQUIRE_FALSE(world.ActorAt(machinePosition).value);
        REQUIRE_FALSE(world.ActorAt(batteryPosition).value);
        printer = Take(world.CreateObject(content.FindObject("home_polymer_printer_station"), machinePosition));
        printerId = world.GetObject(printer)->id;
        const auto batteryType = content.FindItem("workshop_battery_pack");
        const auto& definition = content.GetItem(batteryType);
        const auto box = Take(world.CreateObject(definition.placedObject, batteryPosition));
        battery = Take(world.CreateItem(batteryType, ItemLocation::Stored(box)));
        batteryStore = definition.energyStores.front().capability;
        REQUIRE(world.SetItemEnergy(battery, batteryStore, 4000));
        Take(world.Power().ExtendBounds({machinePosition, batteryPosition}));
        for (int i = 0; i < 50; ++i) {
            Take(world.CreateItem(content.FindItem("polymer_filament"), ItemLocation::Carried(actor)));
        }
        const auto stick = Take(world.CreateItem(content.FindItem("usb_data_stick"), ItemLocation::Carried(actor)));
        stickId = world.GetItem(stick)->id;
    }

    CommandResult Send(Command command, std::uint64_t elapsed = 1000) {
        const auto sequence = Take(guest.Submit(eastl::move(command)));
        eastl::optional<CommandResult> result;
        const bool received = Until([&] {
            for (const auto& receipt : guest.TakeResults()) {
                CHECK(receipt.player == guest.View().playerId);
                REQUIRE(receipt.sequence == sequence);
                if (receipt.result.outcome != Outcome::queued) result = receipt.result;
            }
            return result.has_value();
        }, elapsed);
        CAPTURE(sequence, server.GetTick(), server.GetWorld().GetActorRemainingTicks(actor));
        REQUIRE(received);
        return *result;
    }

    const MachineView* Machine() const {
        for (const auto& machine : guest.View().machines) {
            if (machine.object == printerId) return &machine;
        }
        return nullptr;
    }

    const InformationDeviceView* Device(InformationEndpoint endpoint) const {
        for (const auto& device : guest.View().informationDevices) {
            if (device.endpoint == endpoint) return &device;
        }
        return nullptr;
    }

    bool HasPackage(InformationEndpoint endpoint, InformationId package) const {
        const auto* device = Device(endpoint);
        if (!device) return false;
        for (auto value : device->packages) if (value == package) return true;
        return false;
    }

    bool Ready() const {
        for (const auto& player : guest.View().players) {
            if (player.id == guest.View().playerId) return player.ready && !player.commandQueued;
        }
        return false;
    }
};

TEST_CASE("Shipped printer completes remote paid setup stop resume and awards its initiator",
    "[digital][network][loopback]") {
    DigitalLan r;
    Command command;
    command.kind = CommandKind::begin_machine;
    command.target = r.printerId;
    command.machineRecipe = r.content.FindMachineRecipe("print_plastic_cup");
    command.quantity = 1;
    const auto started = r.Send(command);
    REQUIRE(started.outcome == Outcome::machine_changed);
    REQUIRE(started.machineOutcome == MachineOutcome::Started);
    CHECK(started.advanced);
    CHECK(started.cost > 0);
    REQUIRE(r.Until([&] { return r.Machine() && r.Machine()->hasJob && r.Machine()->setup; }));
    const auto owned = r.guest.View(); // A client-owned decoded value, not a host projection.
    const auto paid = r.server.GetWorld().GetActorRemainingTicks(r.actor);
    REQUIRE(paid > 0);
    const auto tick = r.server.GetTick();
    const auto turns = r.server.GetWorld().GetActor(r.actor)->turns;

    command = {};
    command.kind = CommandKind::machine_action;
    command.target = r.printerId;
    command.machineAction = MachineAction::Stop;
    const auto stopped = r.Send(command);
    REQUIRE(stopped.outcome == Outcome::machine_changed);
    REQUIRE(stopped.machineOutcome == MachineOutcome::PauseRequested);
    CHECK_FALSE(stopped.advanced);
    CHECK(stopped.cost == 0);
    const auto elapsed = r.server.GetTick() - tick;
    REQUIRE(elapsed < paid);
    CHECK(r.server.GetWorld().GetActorRemainingTicks(r.actor) == paid - elapsed);
    CHECK(r.server.GetWorld().GetActor(r.actor)->turns == turns);
    REQUIRE(r.Until([&] { return r.Machine() && r.Machine()->pauseRequested; }));
    REQUIRE(r.Until([&] {
        return r.Machine() && r.Machine()->setup && !r.Machine()->running && r.Ready();
    }, 100000));
    const auto partial = *r.Machine();
    CHECK(partial.hasJob);
    CHECK(partial.worker == 0);
    CHECK_FALSE(partial.pauseRequested);
    CHECK(partial.setupCompleted > 0);
    CHECK(partial.setupCompleted < partial.setupWork);
    CHECK(r.server.GetWorld().GetItemEnergy(r.battery, r.batteryStore)->energy == 4000);

    command.machineAction = MachineAction::Resume;
    const auto resumed = r.Send(command);
    REQUIRE(resumed.outcome == Outcome::machine_changed);
    REQUIRE(resumed.machineOutcome == MachineOutcome::Resumed);
    CHECK(resumed.advanced);
    REQUIRE(r.Until([&] {
        return r.Machine() && r.Machine()->hasJob && !r.Machine()->setup;
    }, 100000));
    CHECK(r.Machine()->running);
    CHECK(r.Machine()->worker == 0);
    CHECK(r.Machine()->initiator == owned.characterId);
    CHECK(r.Machine()->setupCompleted == r.Machine()->setupWork);
    CHECK(r.Machine()->remainingTicks > 0);
    CHECK(r.Machine()->reservedVolumeMl == 400);
    CHECK(r.Machine()->learningXp == 10 * ratioScale);
    for (int turn = 0; turn < 20 && r.Machine()->hasJob; ++turn) {
        REQUIRE(r.Until([&] { return r.Ready(); }, 100000));
        REQUIRE(r.Send(Command{CommandKind::wait}, 100000).advanced);
        REQUIRE(r.Until([&] { return r.Ready(); }, 100000));
    }
    CAPTURE(r.server.GetTick(), r.Machine()->remainingTicks,
        r.server.GetWorld().GetItemEnergy(r.battery, r.batteryStore)->energy);
    REQUIRE_FALSE(r.Machine()->hasJob);
    CHECK(r.server.GetWorld().GetItemEnergy(r.battery, r.batteryStore)->energy == 2800);

    const auto skill = r.content.FindSkill("computers");
    const auto* progress = FindSkillProgress(r.server.GetWorld().GetActor(r.actor)->learning, skill);
    REQUIRE(progress);
    CHECK(progress->xp == 10 * ratioScale);
    REQUIRE(r.Until([&] {
        for (const auto& entry : r.guest.View().learning.skills) {
            if (entry.progress.skill == skill) return entry.progress == *progress;
        }
        return false;
    }));
    command.machineAction = MachineAction::Collect;
    const auto collected = r.Send(command, 100000);
    REQUIRE(collected.outcome == Outcome::machine_changed);
    REQUIRE(collected.machineOutcome == MachineOutcome::Collected);
    REQUIRE(r.Until([&] {
        unsigned cups = 0, filament = 0;
        for (const auto& item : r.guest.View().inventory) {
            cups += item.type == r.content.FindItem("plastic_cup");
            filament += item.type == r.content.FindItem("polymer_filament");
        }
        return cups == 1 && filament == 45;
    }));
    REQUIRE_FALSE(owned.machines.empty());
    CHECK(owned.machines.front().hasJob);
    CHECK(owned.machines.front().setup);
    CHECK(owned.machines.front().initiator == owned.characterId);
    REQUIRE(r.server.GetWorld().CheckInvariants());
}

TEST_CASE("Remote controller copies a design package to USB and deletes only the copy",
    "[digital][network][loopback]") {
    DigitalLan r;
    const InformationEndpoint controller{0, r.printerId}, usb{r.stickId, 0};
    const auto package = r.content.FindInformation("home_printer_designs");
    Command command;
    command.kind = CommandKind::copy_information;
    command.informationSource = controller;
    command.informationDestination = usb;
    command.information = package;
    const auto started = r.Send(command);
    REQUIRE(started.outcome == Outcome::information_changed);
    REQUIRE(started.informationOutcome == InformationOutcome::Started);
    REQUIRE(r.Until([&] { return r.Device(usb) && r.Device(usb)->transferring; }));
    bool separateDevice = false;
    for (const auto& item : r.guest.View().inventory) {
        if (item.id == r.stickId) { separateDevice = !item.stackable; }
    }
    CHECK(separateDevice);
    auto forged = r.guest.View();
    for (auto& item : forged.inventory) {
        if (item.id == r.stickId) { item.stackable = true; }
    }
    CHECK_FALSE(wire::ValidItemDefinitions(forged, r.content));
    CHECK_FALSE(r.HasPackage(usb, package));
    CHECK(r.HasPackage(controller, package));
    const auto owned = r.guest.View();
    REQUIRE(r.Until([&] { return r.HasPackage(usb, package) && r.Ready(); }, 100000));
    CHECK_FALSE(r.Device(usb)->transferring);
    const auto duplicate = r.Send(command);
    CHECK(duplicate.informationOutcome == InformationOutcome::AlreadyPresent);
    CHECK_FALSE(duplicate.advanced);
    CHECK(duplicate.cost == 0);
    command = {};
    command.kind = CommandKind::remove_information;
    command.informationDestination = usb;
    command.information = package;
    const auto removed = r.Send(command);
    REQUIRE(removed.outcome == Outcome::information_changed);
    REQUIRE(removed.informationOutcome == InformationOutcome::Removed);
    REQUIRE(r.Until([&] { return !r.HasPackage(usb, package); }));
    CHECK(r.HasPackage(controller, package));
    bool retainedTransfer = false;
    for (const auto& device : owned.informationDevices) {
        if (device.endpoint == usb) retainedTransfer = device.transferring && device.packages.empty();
    }
    CHECK(retainedTransfer);
    REQUIRE(r.server.GetWorld().CheckInvariants());
}
}
