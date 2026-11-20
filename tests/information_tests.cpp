#include "core/world.h"
#include "digital/information_system.h"
#include "power/power_system.h"
#include "skills/learning_system.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
using namespace cataclysmx;
template<class T, class E> T Take(eastl::expected<T, E> value) {
    REQUIRE(value); return eastl::move(*value);
}
ContentCatalog InformationContent() {
    Diagnostics diagnostics;
    auto loaded = LoadContentPack(CATACLYSMX_CONTENT_PACK, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(loaded);
    return eastl::move(*loaded);
}
struct Rig {
    ContentCatalog content = InformationContent();
    World world{content};
    ActorHandle actor{};
    ItemHandle phone{};
    ItemHandle stick{};
    InformationId package{};
    Rig() {
        Take(world.RequestChunks({{0, 0}, 0}, LoadMode::active));
        REQUIRE(world.ReconcileChunks());
        REQUIRE(world.SetTile({2, 2}, {content.FindTerrain("floor"), 0}));
        actor = Take(world.CreateActor({2, 2}, true));
        phone = Create("phone"); stick = Create("usb_data_stick");
        package = content.FindInformation("digital_workshop_primer");
        REQUIRE(package.value);
    }
    ItemHandle Create(const char* type) {
        const auto id = content.FindItem(type); REQUIRE(id.value);
        return Take(world.CreateItem(id, ItemLocation::Carried(actor)));
    }
    InformationEndpoint At(ItemHandle handle) const { return {world.GetItem(handle)->id, 0}; }
    InformationOutcome Copy(ItemHandle source, ItemHandle destination, ItemHandle host = {}) {
        return Take(world.Information().Copy(actor, At(source), At(destination), package, host)).outcome;
    }
    void Finish() { world.ElapseTicks(100); world.Information().ProcessEvents(); }
};

TEST_CASE("Information copies publish once at the paid completion boundary", "[digital][information]") {
    Rig r;
    CHECK(r.Copy(r.phone, r.stick) == InformationOutcome::Started);
    CHECK_FALSE(r.world.Information().HasPackage(r.At(r.stick), r.package));
    CHECK(r.world.Information().Busy(r.actor));
    r.world.ElapseTicks(100);
    CHECK((r.world.Information().ActorEventIn(r.actor) == Tick{0}));
    CHECK(r.world.Information().Busy(r.actor));
    CHECK_FALSE(r.world.Information().HasPackage(r.At(r.stick), r.package));
    CHECK(r.world.Information().ProcessEvents());
    CHECK_FALSE(r.world.Information().Busy(r.actor));
    CHECK(r.world.Information().HasPackage(r.At(r.stick), r.package));
    CHECK(r.world.Information().HasPackage(r.At(r.phone), r.package));
    const auto turns = r.world.GetActor(r.actor)->turns;
    CHECK(r.Copy(r.phone, r.stick) == InformationOutcome::AlreadyPresent);
    CHECK(r.world.GetActor(r.actor)->turns == turns);
    Vector<InformationId> packages;
    r.world.Information().CollectPackages(r.At(r.stick), packages);
    REQUIRE(packages.size() == 1); CHECK(packages.front() == r.package);
    CHECK_FALSE(r.world.Information().ProcessEvents());
}

TEST_CASE("Information transfer interruption keeps source and spends its scheduled time", "[digital][information]") {
    Rig r;
    REQUIRE(r.Copy(r.phone, r.stick) == InformationOutcome::Started);
    r.world.ElapseTicks(20);
    REQUIRE(Take(r.world.Information().Cancel(r.actor)).outcome == InformationOutcome::Cancelled);
    CHECK_FALSE(r.world.Information().Busy(r.actor));
    CHECK(r.world.GetActor(r.actor)->actionDeadline.Remaining(r.world.GetTick()) == 80);
    r.Finish();
    CHECK_FALSE(r.world.Information().HasPackage(r.At(r.stick), r.package));
    CHECK(r.world.Information().HasPackage(r.At(r.phone), r.package));
}

TEST_CASE("Information transfer revalidates power and carried access", "[digital][information]") {
    Rig r;
    REQUIRE(r.Copy(r.phone, r.stick) == InformationOutcome::Started);
    SECTION("power loss aborts") {
        const auto& definition = r.content.GetItem(r.world.GetItem(r.phone)->type);
        REQUIRE(r.world.SetItemEnergy(r.phone, definition.readingPower, 0));
    }
    SECTION("ground devices cannot participate") {
        REQUIRE(r.world.TransferItem(r.phone, ItemLocation::Ground({2, 2})));
    }
    r.Finish();
    CHECK_FALSE(r.world.Information().Busy(r.actor));
    CHECK_FALSE(r.world.Information().HasPackage(r.At(r.stick), r.package));
    CHECK(r.world.Information().HasPackage(r.At(r.phone), r.package));
}

TEST_CASE("Information completion revalidates after all time has been paid", "[digital][information]") {
    Rig r;
    REQUIRE(r.Copy(r.phone, r.stick) == InformationOutcome::Started);
    r.world.ElapseTicks(100);
    const auto& definition = r.content.GetItem(r.world.GetItem(r.phone)->type);
    REQUIRE(r.world.SetItemEnergy(r.phone, definition.readingPower, 0));
    CHECK(r.world.Information().ProcessEvents());
    CHECK_FALSE(r.world.Information().HasPackage(r.At(r.stick), r.package));
    CHECK_FALSE(r.world.Information().Busy(r.actor));
}

TEST_CASE("Information work follows actor speed without consuming charge", "[digital][information]") {
    Rig r;
    const_cast<Actor*>(r.world.GetActor(r.actor))->speed = 200;
    const auto& definition = r.content.GetItem(r.world.GetItem(r.phone)->type);
    const auto energy = r.world.GetItemEnergy(r.phone, definition.readingPower)->energy;
    REQUIRE(r.Copy(r.phone, r.stick) == InformationOutcome::Started);
    CHECK(r.world.Information().ActorEventIn(r.actor) == ActionTicks(100, 200));
    r.world.ElapseTicks(ActionTicks(100, 200));
    CHECK(r.world.Information().ProcessEvents());
    CHECK(r.world.Information().HasPackage(r.At(r.stick), r.package));
    CHECK(r.world.GetItemEnergy(r.phone, definition.readingPower)->energy == energy);
}

TEST_CASE("Legacy electronic books export human packages but paper cannot be scanned", "[digital][information]") {
    Rig r;
    const auto reader = r.Create("workshop_ebook_reader");
    const auto paper = r.Create("workshop_primer_print");
    CHECK(r.Copy(paper, r.stick) == InformationOutcome::InvalidDevice);
    REQUIRE(r.Copy(reader, r.stick) == InformationOutcome::Started); r.Finish();
    CHECK(r.world.Information().HasPackage(r.At(r.stick), r.package));
    CHECK(r.Copy(r.phone, reader) == InformationOutcome::InvalidDevice);
    CHECK(Take(r.world.Information().Remove(r.actor, r.At(reader), r.package)).outcome
        == InformationOutcome::InvalidDevice);
}

TEST_CASE("USB copying and human references require a powered reader", "[digital][information]") {
    Rig r;
    REQUIRE(r.Copy(r.phone, r.stick) == InformationOutcome::Started); r.Finish();
    const auto second = r.Create("usb_data_stick");
    CHECK(r.Copy(r.stick, second) == InformationOutcome::MissingHost);
    CHECK(r.Copy(r.stick, second, r.phone) == InformationOutcome::Started); r.Finish();
    CHECK(r.world.Information().HasPackage(r.At(second), r.package));
    const auto& books = r.content.GetInformation(r.package).books;
    REQUIRE_FALSE(books.empty());
    const auto book = books.front();
    CHECK(r.world.Learning().HasBookReference(r.actor, book));
    REQUIRE(Take(r.world.Information().Remove(r.actor, r.At(r.phone), r.package)).outcome == InformationOutcome::Removed);
    CHECK(r.world.Learning().HasBookReference(r.actor, book));
    const auto& definition = r.content.GetItem(r.world.GetItem(r.phone)->type);
    REQUIRE(r.world.SetItemEnergy(r.phone, definition.readingPower, 0));
    CHECK_FALSE(r.world.Learning().HasBookReference(r.actor, book));
    CHECK(r.Copy(r.stick, r.phone) == InformationOutcome::NoPower);
}

TEST_CASE("Copied books preserve character study identity", "[digital][information]") {
    Rig r;
    const auto& books = r.content.GetInformation(r.package).books;
    REQUIRE_FALSE(books.empty());
    const auto book = books.front();
    const auto laptop = r.Create("legacy_laptop");
    REQUIRE(r.Copy(r.phone, laptop) == InformationOutcome::Started); r.Finish();
    CHECK(r.world.Information().HasBook(r.phone, book, {}));
    CHECK(r.world.Information().HasBook(laptop, book, {}));
    auto& learning = const_cast<Actor*>(r.world.GetActor(r.actor))->learning;
    EnsureBookProgress(learning, book).used = true;
    CHECK(Take(r.world.Learning().Study(r.actor, book)).outcome == StudyOutcome::AlreadyUsed);
    REQUIRE(Take(r.world.Information().Remove(r.actor, r.At(r.phone), r.package)).outcome == InformationOutcome::Removed);
    r.Finish();
    CHECK(r.world.Learning().HasBookReference(r.actor, book));
    CHECK(Take(r.world.Learning().Study(r.actor, book)).outcome == StudyOutcome::AlreadyUsed);
}

TEST_CASE("Information discovery includes nested carried storage and excludes ground media", "[digital][information]") {
    Rig r;
    const auto container = r.Create("small_parts_case");
    REQUIRE(r.world.TransferItem(r.stick, ItemLocation::Inside(container)));
    Vector<InformationDeviceView> devices;
    r.world.Information().Collect(r.actor, devices);
    bool found = false;
    for (const auto& device : devices) { found |= device.endpoint == r.At(r.stick); }
    CHECK(found);
    REQUIRE(r.world.TransferItem(container, ItemLocation::Ground({2, 2})));
    r.world.Information().Collect(r.actor, devices);
    for (const auto& device : devices) { CHECK_FALSE(device.endpoint == r.At(r.stick)); }
    CHECK(r.Copy(r.phone, r.stick) == InformationOutcome::OutOfReach);
}

TEST_CASE("Controller endpoints directly import and export USB information", "[digital][information]") {
    Rig r;
    REQUIRE(r.world.SetTile({3, 2}, {r.content.FindTerrain("floor"), 0}));
    if (!r.world.Power().GridAt({3, 2})) {
        Take(r.world.Power().RegisterBuilding({3, 2}, PowerBounds{{3, 2}, {3, 2}}));
    }
    const auto machine = Take(r.world.CreateObject(r.content.FindObject("home_polymer_printer_station"), {3, 2}));
    const auto* object = r.world.GetObject(machine);
    const auto backing = object->equipment;
    const InformationEndpoint controller{0, object->id};
    REQUIRE(backing != ItemHandle{});
    CHECK(r.world.IsReservedItem(backing));
    REQUIRE(r.Copy(r.phone, r.stick) == InformationOutcome::Started); r.Finish();
    CHECK(Take(r.world.Information().Copy(r.actor, r.At(r.stick), r.At(backing), r.package)).outcome
        == InformationOutcome::InvalidDevice);
    REQUIRE(Take(r.world.Information().Copy(r.actor, r.At(r.stick), controller, r.package)).outcome
        == InformationOutcome::Started);
    r.Finish();
    CHECK(r.world.Information().HasPackage(controller, r.package));
    const auto software = r.content.FindInformation("home_printer_software");
    REQUIRE(software.value);
    REQUIRE(Take(r.world.Information().Copy(r.actor, controller, r.At(r.stick), software)).outcome
        == InformationOutcome::Started);
    r.Finish();
    CHECK(r.world.Information().HasPackage(r.At(r.stick), software));
    REQUIRE(Take(r.world.Information().Remove(r.actor, controller, software)).outcome == InformationOutcome::Removed);
    CHECK_FALSE(r.world.Information().HasPackage(controller, software));
    CHECK(r.world.Information().HasPackage(r.At(r.stick), software));
}

TEST_CASE("Removal schedules work and refuses mutation while the actor is busy", "[digital][information]") {
    Rig r;
    SECTION("another scheduled action") {
        REQUIRE(r.world.ScheduleActorAction(r.actor, 100));
    }
    SECTION("active transfer") {
        REQUIRE(r.Copy(r.phone, r.stick) == InformationOutcome::Started);
    }
    SECTION("fully paid transfer awaiting completion") {
        REQUIRE(r.Copy(r.phone, r.stick) == InformationOutcome::Started);
        r.world.ElapseTicks(100);
    }
    SECTION("cancelled transfer retains paid deadline") {
        REQUIRE(r.Copy(r.phone, r.stick) == InformationOutcome::Started);
        Take(r.world.Information().Cancel(r.actor));
    }
    CHECK(Take(r.world.Information().Remove(r.actor, r.At(r.phone), r.package)).outcome
        == InformationOutcome::ActorBusy);
    CHECK(r.world.Information().HasPackage(r.At(r.phone), r.package));
    r.Finish();
    const auto turns = r.world.GetActor(r.actor)->turns;
    const auto removed = Take(r.world.Information().Remove(r.actor, r.At(r.phone), r.package));
    CHECK(removed.outcome == InformationOutcome::Removed);
    CHECK(removed.baseCost == 0);
    CHECK(r.world.GetActor(r.actor)->turns == turns + 1);
    CHECK(r.world.GetActor(r.actor)->actionDeadline.Remaining(r.world.GetTick()) == 100);
    CHECK_FALSE(r.world.Information().HasPackage(r.At(r.phone), r.package));
}

TEST_CASE("Event boundaries abort inaccessible transfers even before they become due", "[digital][information]") {
    Rig r;
    REQUIRE(r.Copy(r.phone, r.stick) == InformationOutcome::Started);
    REQUIRE(r.world.TransferItem(r.phone, ItemLocation::Ground({2, 2})));
    CHECK(r.world.Information().ProcessEvents());
    CHECK_FALSE(r.world.Information().Busy(r.actor));
    CHECK(r.world.GetActor(r.actor)->actionDeadline.Remaining(r.world.GetTick()) == 100);
    REQUIRE(r.world.TransferItem(r.phone, ItemLocation::Carried(r.actor)));
    r.Finish();
    CHECK_FALSE(r.world.Information().HasPackage(r.At(r.stick), r.package));
}

TEST_CASE("Importing a lower ID keeps information sorted through a save roundtrip", "[digital][information][save]") {
    struct SaveFile {
        std::filesystem::path directory = std::filesystem::temp_directory_path()
            / ("cataclysmx-information-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::string path;
        SaveFile() { REQUIRE(std::filesystem::create_directory(directory)); path = (directory / "world.sqlite").string(); }
        ~SaveFile() { std::error_code error; std::filesystem::remove_all(directory, error); }
    } file;
    Rig r;
    const auto laptop = r.Create("legacy_laptop");
    const auto other = r.content.FindInformation("digital_salvage_notes");
    REQUIRE(other.value);
    const bool primerFirst = r.package.value < other.value;
    const auto low = primerFirst ? r.package : other;
    const auto high = primerFirst ? other : r.package;
    const auto source = primerFirst ? r.phone : laptop;
    const auto destination = primerFirst ? laptop : r.phone;
    REQUIRE(Take(r.world.Information().Copy(r.actor, r.At(source), r.At(destination), low)).outcome
        == InformationOutcome::Started);
    r.Finish();
    const auto snapshot = r.world.CaptureItemState(destination);
    const auto* library = snapshot.Find("information_library"); REQUIRE(library);
    const auto* packages = library->Find("packages"); REQUIRE(packages); REQUIRE(packages->Size() == 2);
    CHECK(packages->At(0).StringValue() == r.content.GetInformation(low).id);
    CHECK(packages->At(1).StringValue() == r.content.GetInformation(high).id);
    const auto destinationId = r.world.GetItem(destination)->id;
    Diagnostics diagnostics;
    const auto saved = r.world.CreateSave(file.path.c_str(), {}, &diagnostics);
    INFO(FormatDiagnostics(diagnostics).c_str()); REQUIRE(saved);
    World restored(r.content); Take(restored.LoadSave(file.path.c_str()));
    Take(restored.RequestChunks({{0, 0}, 0}, LoadMode::active)); REQUIRE(restored.ReconcileChunks());
    const auto restoredItem = restored.FindItem(destinationId); REQUIRE(restoredItem != ItemHandle{});
    CHECK(restored.CaptureItemState(restoredItem) == snapshot);
    CHECK(restored.Information().HasPackage({destinationId, 0}, low));
    CHECK(restored.Information().HasPackage({destinationId, 0}, high));
}
}
