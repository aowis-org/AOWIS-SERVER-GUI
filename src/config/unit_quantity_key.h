#pragma once

#include <aowis/model/units/quantity.h>
#include <QString>

// Validate the literal against the Model registry while preserving the
// QStringLiteral optimization for Qt presentation APIs.
#define AOWIS_QSTRING_QUANTITY(literal) \
    ([]() -> QString { \
        constexpr ::aowis::units::Quantity checked{literal}; \
        (void)checked; \
        return QStringLiteral(literal); \
    }())
