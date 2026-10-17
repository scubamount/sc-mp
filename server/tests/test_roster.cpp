// SPDX-License-Identifier: GPL-3.0-only
// Tests of the roster (server/roster.h): the lowest free slot, the generation of a slot that goes up
// with each new occupant, reuse after a leave, a full roster, and a clean start after the session ends.
#include "server/roster.h"
#include "server/tests/check.h"
#include <string>

namespace {

using sco::relay::Roster;

void TestSlotsAreTheLowestFree() {
    Roster roster(3);
    CHECK(roster.Add(2, "Ann") == 0);
    CHECK(roster.Add(3, "Bob") == 1);
    CHECK(roster.Add(4, "Cid") == 2);
    CHECK(roster.Count() == 3);
    CHECK(roster.SlotOf(3) == 1 && roster.SlotOf(99) == -1);
    CHECK(roster.Seats().size() == 3 && roster.Seats()[1].peer == 3 && roster.Seats()[1].name == "Bob");

    std::string name = "?";
    CHECK(roster.Remove(3, &name) && name == "Bob");
    CHECK(roster.Count() == 2 && roster.SlotOf(3) == -1 && roster.Seats()[1].peer == 0);

    // The next player takes the hole, not the end.
    CHECK(roster.Add(5, "Dan") == 1);
    CHECK(roster.SlotOf(5) == 1);
}

void TestGenerationGoesUpWithEachNewOccupant() {
    Roster roster(2);
    CHECK(roster.Seats()[0].generation == 0);   // nobody has sat there yet
    CHECK(roster.Add(2, "Ann") == 0);
    CHECK(roster.Seats()[0].generation == 1);
    CHECK(roster.Add(3, "Bob") == 1);
    CHECK(roster.Seats()[1].generation == 1);   // each slot counts its own occupants

    std::string name;
    CHECK(roster.Remove(2, &name));
    CHECK(roster.Seats()[0].generation == 1);   // leaving does not change it: the next occupant adds one
    CHECK(roster.Add(4, "Cid") == 0);
    CHECK(roster.Seats()[0].generation == 2);
    CHECK(roster.Seats()[1].generation == 1);   // Bob's slot did not move
}

void TestGenerationWrapsAfter255() {
    // 256 occupants of the same slot bring the counter back to 0. Only equality is ever asked of it.
    Roster roster(1);
    std::string name;
    for (uint32_t occupant = 1; occupant <= 256; ++occupant) {
        CHECK(roster.Add(1000 + occupant, "Walker") == 0);
        CHECK(static_cast<uint32_t>(roster.Seats()[0].generation) == occupant % 256);
        CHECK(roster.Remove(1000 + occupant, &name));
    }
}

void TestFullAndUnknown() {
    Roster roster(2);
    CHECK(roster.Add(2, "Ann") == 0 && roster.Add(3, "Bob") == 1);
    CHECK(roster.Add(4, "Cid") == -1);              // no slot left
    CHECK(roster.Count() == 2 && roster.SlotOf(4) == -1);

    std::string name = "untouched";
    CHECK(!roster.Remove(99, &name));
    CHECK(name == "untouched");

    // The same player added twice keeps its slot and does not count twice.
    CHECK(roster.Add(2, "Ann") == 0);
    CHECK(roster.Count() == 2 && roster.Seats()[0].generation == 1);
}

void TestClearStartsOver() {
    Roster roster(2);
    std::string name;
    roster.Add(2, "Ann");
    roster.Remove(2, &name);
    roster.Add(3, "Bob");
    roster.Add(4, "Cid");
    roster.Clear();
    CHECK(roster.Count() == 0 && roster.Seats().size() == 2);
    CHECK(roster.Seats()[0].peer == 0 && roster.Seats()[0].generation == 0 && roster.Seats()[1].peer == 0);
    CHECK(roster.Add(5, "Dan") == 0 && roster.Seats()[0].generation == 1);
}

}  // namespace

namespace relaytest {

void RunRosterTests() {
    Run("TestSlotsAreTheLowestFree", TestSlotsAreTheLowestFree);
    Run("TestGenerationGoesUpWithEachNewOccupant", TestGenerationGoesUpWithEachNewOccupant);
    Run("TestGenerationWrapsAfter255", TestGenerationWrapsAfter255);
    Run("TestFullAndUnknown", TestFullAndUnknown);
    Run("TestClearStartsOver", TestClearStartsOver);
}

}  // namespace relaytest
