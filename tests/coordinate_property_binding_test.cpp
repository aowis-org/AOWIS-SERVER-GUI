#include "entity_inspector/coordinate_property_binding.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "Failure at line " << __LINE__ << ": " #condition "\n"; std::abort(); } } while (false)

static void checkCoordinateEdits(bool demand_point)
{
    // Run the identical binding contract for a hydraulic node and a Demand Point.
    CoordinateWGS84 model;
    model.latitude_deg = 12.12345678912345;
    model.longitude_deg = 18.98765432198765;
    model.altitude_m = 123.456789123456;
    const CoordinateWGS84 original = model;
    int writes = 0;
    bool available = true;

    const CoordinatePropertyBinding::CoordinateReader read = [&]() -> std::optional<CoordinateWGS84> {
        return available ? std::optional<CoordinateWGS84>(model) : std::nullopt;
    };
    const CoordinatePropertyBinding::CoordinateWriter write = [&](const CoordinateWGS84 &next) {
        model = next;
        ++writes;
        return true;
    };
    CoordinatePropertyBinding latitude(read, write, CoordinatePropertyBinding::Component::Latitude);
    CoordinatePropertyBinding longitude(read, write, CoordinatePropertyBinding::Component::Longitude);
    using Result = NumericPropertyBinding::Result;

    // Merely opening, rounding, closing, or changing presentation must not write.
    CHECK(latitude.presentation("deg") == original.latitude_deg);
    CHECK(longitude.presentation("deg") == original.longitude_deg);
    CHECK(!latitude.presentation("m"));
    for (int i = 0; i < 20; ++i)
    {
        CHECK(latitude.beginEdit("deg", 12.123457));
        CHECK(latitude.commit() == Result::Unchanged);
        CHECK(longitude.beginEdit("deg", 18.987654));
        CHECK(longitude.userEdited(18.987654));
        CHECK(longitude.commit() == Result::Unchanged);
    }
    CHECK(writes == 0);
    CHECK(std::memcmp(&model.latitude_deg, &original.latitude_deg, sizeof(double)) == 0);
    CHECK(std::memcmp(&model.longitude_deg, &original.longitude_deg, sizeof(double)) == 0);
    CHECK(model.altitude_m == original.altitude_m);

    // Changing latitude must not round longitude to the GUI's six decimals.
    CHECK(latitude.beginEdit("deg", 12.123457));
    CHECK(latitude.userEdited(12.125));
    CHECK(latitude.commit() == Result::Committed);
    CHECK(model.latitude_deg == 12.125 && writes == 1);
    CHECK(std::memcmp(&model.longitude_deg, &original.longitude_deg, sizeof(double)) == 0);
    CHECK(model.altitude_m == original.altitude_m);

    // Changing longitude must preserve the exact latitude and altitude.
    model.latitude_deg = original.latitude_deg;
    CHECK(longitude.beginEdit("deg", 18.987654));
    CHECK(longitude.userEdited(19.125));
    CHECK(longitude.commit() == Result::Committed && writes == 2);
    CHECK(std::memcmp(&model.latitude_deg, &original.latitude_deg, sizeof(double)) == 0);
    CHECK(model.altitude_m == original.altitude_m);

    // A companion coordinate can change externally during an edit. The writer
    // must fetch its *current* value rather than use a stale snapshot.
    CHECK(latitude.beginEdit("deg", 12.123457));
    CHECK(latitude.userEdited(13.0));
    model.longitude_deg = 31.12345678901234;
    CHECK(latitude.commit() == Result::Committed && writes == 3);
    CHECK(model.latitude_deg == 13.0 && model.longitude_deg == 31.12345678901234);
    CHECK(model.altitude_m == original.altitude_m);

    // A concurrent change to the *edited* component is a conflict, not an overwrite.
    CHECK(latitude.beginEdit("deg", 13.0));
    CHECK(latitude.userEdited(14.0));
    model.latitude_deg = 13.5;
    CHECK(latitude.commit() == Result::Conflict && writes == 3 && model.latitude_deg == 13.5);
    latitude.cancel();

    // Removing the node/Demand Point makes the binding unavailable.
    available = false;
    CHECK(!latitude.beginEdit("deg", 13.5));
    CHECK(!longitude.presentation("deg"));
    available = true;

    // Latitude/longitude range constraints are independent.
    CHECK(latitude.beginEdit("deg", 13.5));
    CHECK(!latitude.userEdited(95.0));
    CHECK(latitude.commit() == Result::Invalid && writes == 3);
    latitude.cancel();
    CHECK(longitude.beginEdit("deg", 31.123457));
    CHECK(longitude.userEdited(95.0));
    CHECK(longitude.commit() == Result::Committed && writes == 4);
    CHECK(model.latitude_deg == 13.5 && model.longitude_deg == 95.0);
    CHECK(model.altitude_m == original.altitude_m);

    if (demand_point)
        std::cout << "Demand Point coordinate binding passed\n";
    else
        std::cout << "Hydraulic-node coordinate binding passed\n";
}

int main()
{
    checkCoordinateEdits(false);
    checkCoordinateEdits(true);
}
