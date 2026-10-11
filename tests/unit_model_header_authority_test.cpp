#include <aowis/model/units/conversion.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        std::abort(); \
    } \
} while (false)

// This target deliberately searches the GUI legacy include directory *before*
// the Model include directory. If somebody accidentally reintroduces a stale
// duplicate header, Model-only units must not silently disappear.
static_assert(aowis::units::unitScale("mg/m2/d").has_value(),
              "GUI conversion header does not forward to the authoritative Model registry");
static_assert(aowis::units::unitScale("mg/m2/h").has_value(),
              "Model zero-order wall-reaction unit missing");
static_assert(aowis::units::unitScale("mg/m2/s").has_value(),
              "Model zero-order wall-reaction unit missing");
static_assert(aowis::units::unitScale("g/m2/d").has_value(),
              "Model zero-order wall-reaction unit missing");

int main()
{
    const std::optional<double> converted = aowis::units::convert(
        "zero_order_wall_reaction_coefficient", 1.0, "mg/m2/d", "mg/m2/s");
    CHECK(converted.has_value());
    CHECK(std::abs(*converted - 1.0 / 86400.0) < 1e-15);
    return 0;
}
