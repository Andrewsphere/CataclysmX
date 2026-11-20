#include "network/wire.h"
#include "core/ratio.h"
#include <catch2/catch_test_macros.hpp>

namespace {
using namespace cataclysmx;
namespace wire = cataclysmx::network::wire;

// Bypass the production writer's admission checks to exercise the structural
// reader independently. Protocol 28 ends snapshots with these digital lists.
void WriteRawDigital(wire::Writer& writer, const WorldView& view) {
    writer.U16(static_cast<std::uint16_t>(view.machines.size()));
    for (const auto& machine : view.machines) {
        writer.U64(machine.object); writer.U32(machine.recipe.value); writer.U16(machine.quantity);
        writer.U8(machine.hasJob); writer.U8(machine.setup); writer.U8(machine.running);
        writer.U8(machine.pauseRequested); writer.U8(machine.completionPending);
        writer.U64(machine.initiator); writer.U64(machine.worker);
        writer.U64(machine.setupWork); writer.U64(machine.setupCompleted);
        writer.U64(machine.remainingTicks); writer.U64(machine.fundedTicks);
        writer.U64(machine.reservedVolumeMl); writer.U64(machine.learningXp);
        writer.U32(static_cast<std::uint32_t>(machine.recipes.size()));
        for (const auto& recipe : machine.recipes) {
            writer.U32(recipe.recipe.value); writer.U8(static_cast<std::uint8_t>(recipe.availability));
            writer.U16(recipe.maximumQuantity);
        }
    }
    writer.U16(static_cast<std::uint16_t>(view.informationDevices.size()));
    for (const auto& device : view.informationDevices) {
        writer.U64(device.endpoint.item); writer.U64(device.endpoint.object); writer.U32(device.type.value);
        writer.U8(device.powered); writer.U8(device.writable); writer.U8(device.reader); writer.U8(device.transferring);
        writer.U64(device.remainingTicks); writer.U32(static_cast<std::uint32_t>(device.packages.size()));
        for (auto package : device.packages) writer.U32(package.value);
    }
}

void CheckDigitalRejected(const WorldView& emptyView, const WorldView& invalid) {
    wire::Writer guarded; CHECK_FALSE(guarded.View(invalid));
    REQUIRE(emptyView.machines.empty()); REQUIRE(emptyView.informationDevices.empty());
    wire::Writer raw; REQUIRE(raw.View(emptyView));
    REQUIRE(raw.bytes.size() >= 4);
    raw.bytes.resize(raw.bytes.size() - 4); // Replace the two empty u16 digital list counts.
    WriteRawDigital(raw, invalid);
    WorldView decoded;
    wire::Reader reader{{raw.bytes.data(), raw.bytes.size()}};
    CHECK_FALSE(reader.View(decoded));
}

void CheckDigitalAccepted(const WorldView& view) {
    wire::Writer writer; REQUIRE(writer.View(view));
    WorldView decoded; wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.View(decoded)); CHECK(reader.Done());
}

ItemTypeId DigitalType(const ContentCatalog& content, DigitalDeviceKind kind) {
    for (std::size_t i = 1; i <= content.ItemCount(); ++i) {
        const ItemTypeId type{static_cast<std::uint32_t>(i)};
        if (content.GetItem(type).digitalDevice == kind) return type;
    }
    return {};
}

TEST_CASE("Protocol 28 preserves digital command fields and rejects truncated payloads", "[digital][network]") {
    CHECK(wire::protocol == 0x43584e1c);
    Command original; original.kind = CommandKind::copy_information;
    original.informationSource.item = 42; original.informationDestination.object = 91;
    original.information = {7}; original.informationHost = 12;
    wire::Writer writer; REQUIRE(writer.CommandValue(original));
    Command decoded;
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.CommandValue(decoded)); CHECK(reader.Done());
    CHECK(decoded.informationSource == original.informationSource);
    CHECK(decoded.informationDestination == original.informationDestination);
    CHECK(decoded.information == original.information);
    CHECK(decoded.informationHost == original.informationHost);
    for (std::size_t size = 0; size < writer.bytes.size(); ++size) {
        wire::Reader truncated{{writer.bytes.data(), size}};
        CHECK_FALSE(truncated.CommandValue(decoded));
    }
}

TEST_CASE("Digital receipts enforce typed refusal and free no-op semantics", "[digital][network]") {
    PlayerCommandResult result; result.player = 1; result.sequence = 1; result.opportunity = 1;
    result.result.outcome = Outcome::information_changed;
    result.result.informationOutcome = InformationOutcome::AlreadyPresent;
    wire::Writer writer; REQUIRE(writer.Result(result));
    PlayerCommandResult decoded;
    wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.Result(decoded)); CHECK(reader.Done());
    CHECK(decoded.result.informationOutcome == InformationOutcome::AlreadyPresent);
    result.result.cost = 100;
    wire::Writer invalid; CHECK_FALSE(invalid.Result(result));
    result.result = {}; result.result.outcome = Outcome::machine_refused;
    result.result.machineOutcome = MachineOutcome::MissingInputs;
    wire::Writer refusal; REQUIRE(refusal.Result(result));
    result.result.machineOutcome = MachineOutcome::Started;
    wire::Writer wrong; CHECK_FALSE(wrong.Result(result));
}

TEST_CASE("Digital snapshots round trip machine progress reservations and device libraries", "[digital][network]") {
    auto content = LoadContentPack(CATACLYSMX_CONTENT_PACK); REQUIRE(content);
    Server server(*content); auto view = server.View(0);
    MachineView machine; machine.object = 1001; machine.hasJob = true; machine.recipe = {1};
    machine.quantity = 10; machine.setup = true; machine.setupWork = 400; machine.setupCompleted = 100;
    machine.initiator = view.characterId; machine.worker = view.characterId; machine.running = true;
    machine.reservedVolumeMl = 4000; machine.learningXp = 100 * ratioScale;
    machine.recipes.push_back({{1}, MachineOutcome::Busy, 0});
    view.machines.push_back(machine);
    InformationDeviceView device; device.endpoint.item = 1002;
    device.type = DigitalType(*content, DigitalDeviceKind::Storage); REQUIRE(device.type.value);
    device.powered = true; device.writable = true; device.packages.push_back({1});
    view.informationDevices.push_back(device);
    CHECK(wire::ValidItemDefinitions(view, *content));
    wire::Writer writer; REQUIRE(writer.View(view));
    WorldView decoded; wire::Reader reader{{writer.bytes.data(), writer.bytes.size()}};
    REQUIRE(reader.View(decoded)); CHECK(reader.Done());
    REQUIRE(decoded.machines.size() == 1);
    CHECK(decoded.machines[0].quantity == 10); CHECK(decoded.machines[0].reservedVolumeMl == 4000);
    CHECK(decoded.machines[0].setupCompleted == 100); CHECK(decoded.machines[0].learningXp == 100 * ratioScale);
    REQUIRE(decoded.informationDevices.size() == 1);
    CHECK(decoded.informationDevices[0].packages[0] == InformationId{1});
    view.informationDevices[0].endpoint.object = 5;
    wire::Writer invalid; CHECK_FALSE(invalid.View(view));
}

TEST_CASE("Digital wire rejects malformed setup processing and pending completion phases", "[digital][network]") {
    auto content = LoadContentPack(CATACLYSMX_CONTENT_PACK); REQUIRE(content);
    Server server(*content); auto emptyView = server.View(0);
    REQUIRE(emptyView.machines.empty()); REQUIRE(emptyView.informationDevices.empty());
    auto view = emptyView;
    MachineView setup; setup.object = 1001; setup.hasJob = true; setup.recipe = {1}; setup.quantity = 2;
    setup.initiator = 1002; setup.worker = 1003; setup.setup = true; setup.running = true;
    setup.setupWork = 900 * ratioScale; setup.setupCompleted = 30 * ratioScale;
    setup.remainingTicks = 100; setup.reservedVolumeMl = 1234; setup.learningXp = 91 * ratioScale;
    setup.recipes.push_back({{1}, MachineOutcome::Busy, 0});
    view.machines.push_back(setup);
    CheckDigitalAccepted(view);
    CHECK(wire::ValidItemDefinitions(view, *content)); // Captured budgets need not match current recipe tuning.
    const auto reject = [&](const MachineView& invalid) {
        auto bad = emptyView; bad.machines.push_back(invalid); CheckDigitalRejected(emptyView, bad);
    };
    auto invalid = setup; invalid.initiator = 0; reject(invalid);
    invalid = setup; invalid.fundedTicks = 1; reject(invalid);
    invalid = setup; invalid.remainingTicks = 101; reject(invalid);
    invalid = setup; invalid.completionPending = true; reject(invalid);
    invalid = setup; invalid.running = false; reject(invalid);
    invalid = setup; invalid.worker = 0; reject(invalid);
    auto stoppedSetup = setup; stoppedSetup.worker = 0; stoppedSetup.running = false;
    view.machines[0] = stoppedSetup; CheckDigitalAccepted(view); // Retained paid setup is legal.

    auto processing = setup; processing.setup = false; processing.worker = 0;
    processing.setupCompleted = processing.setupWork;
    processing.remainingTicks = 1200; processing.fundedTicks = 100;
    view.machines[0] = processing; CheckDigitalAccepted(view);
    invalid = processing; invalid.worker = 1003; reject(invalid);
    invalid = processing; invalid.pauseRequested = true; reject(invalid);
    invalid = processing; --invalid.setupCompleted; reject(invalid);
    invalid = processing; invalid.fundedTicks = invalid.remainingTicks + 1; reject(invalid);
    invalid = processing; invalid.completionPending = true; reject(invalid);
    auto pending = processing; pending.completionPending = true; pending.remainingTicks = 0; pending.fundedTicks = 0;
    view.machines[0] = pending; CheckDigitalAccepted(view);
    pending.running = false; view.machines[0] = pending; CheckDigitalAccepted(view);
    invalid = pending; invalid.fundedTicks = 1; reject(invalid);
    invalid = pending; invalid.remainingTicks = 1; reject(invalid);
    MachineView idle; idle.object = 1001; view.machines[0] = idle; CheckDigitalAccepted(view);
    invalid = idle; invalid.initiator = 1002; reject(invalid);
    invalid = idle; invalid.reservedVolumeMl = 1; reject(invalid);
    invalid = idle; invalid.learningXp = 1; reject(invalid);
}

TEST_CASE("Digital recipe availability and transfer duration require canonical wire values", "[digital][network]") {
    auto content = LoadContentPack(CATACLYSMX_CONTENT_PACK); REQUIRE(content);
    Server server(*content); auto emptyView = server.View(0);
    REQUIRE(emptyView.machines.empty()); REQUIRE(emptyView.informationDevices.empty());
    auto view = emptyView;
    MachineView machine; machine.object = 1001;
    machine.recipes.push_back({{1}, MachineOutcome::Started, 1}); view.machines.push_back(machine);
    CheckDigitalAccepted(view);
    view.machines[0].recipes[0].maximumQuantity = 255; CheckDigitalAccepted(view);
    view.machines[0].recipes[0].maximumQuantity = 0; CheckDigitalRejected(emptyView, view);
    view.machines[0].recipes[0].availability = MachineOutcome::MissingInputs; CheckDigitalAccepted(view);
    view.machines[0].recipes[0].maximumQuantity = 1; CheckDigitalRejected(emptyView, view);
    view = emptyView;
    InformationDeviceView device; device.endpoint.item = 1002;
    device.type = DigitalType(*content, DigitalDeviceKind::Storage); REQUIRE(device.type.value);
    device.powered = true; device.writable = true;
    device.transferring = true; device.remainingTicks = UINT32_MAX;
    view.informationDevices.push_back(device); CheckDigitalAccepted(view);
    ++view.informationDevices[0].remainingTicks; CheckDigitalRejected(emptyView, view);
    view.informationDevices[0].remainingTicks = 0; CheckDigitalAccepted(view);
    view.informationDevices[0].transferring = false;
    view.informationDevices[0].remainingTicks = 1; CheckDigitalRejected(emptyView, view);
}

TEST_CASE("Catalog-aware digital validation requires object endpoints to be controllers", "[digital][network]") {
    auto content = LoadContentPack(CATACLYSMX_CONTENT_PACK); REQUIRE(content);
    Server server(*content); auto view = server.View(0);
    const auto storage = DigitalType(*content, DigitalDeviceKind::Storage);
    const auto controller = DigitalType(*content, DigitalDeviceKind::Controller);
    REQUIRE(storage.value); REQUIRE(controller.value);
    InformationDeviceView device; device.endpoint.object = 1001; device.type = controller; device.writable = true;
    view.informationDevices.push_back(device);
    REQUIRE(wire::ValidItemDefinitions(view, *content));
    view.informationDevices[0].type = storage;
    CHECK_FALSE(wire::ValidItemDefinitions(view, *content));
    view.informationDevices[0].endpoint = {1001, 0};
    CHECK(wire::ValidItemDefinitions(view, *content));
}
}
