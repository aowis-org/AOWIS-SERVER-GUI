#pragma once

// Compatibility forwarding header for the former GUI-local copy. There must
// never be a second set of physical conversion constants in the GUI.
// The real definitions are owned by the AOWIS-SERVER-MODEL submodule.
//
// Keep this forwarding path until the legacy tracked GUI header can be removed
// from the repository; ZIP-only updates cannot delete an existing tracked file.
#include "../../../../external/AOWIS-SERVER-MODEL/include/aowis/model/units/conversion.h"
