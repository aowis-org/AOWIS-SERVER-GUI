# Unit conversion consolidation — final boundary audit

Source snapshots: `gui(20261009-165010).zip`, `model(20261009-163609).zip`, `epanet(20261009-165211).zip`.

## Ownership

- `AOWIS-SERVER-MODEL/include/aowis/model/units/conversion.h`: physical definitions and explicit canonical conversions. Header-only; no Qt or EPANET dependency.
- `AOWIS-SERVER-GUI/src/import/epanet_js_units.cpp`: unit aliases, omitted-unit interpretation, roughness convention, emitter exponent, and conversion into AOWIS representations. Physical primitive scales delegate to MODEL.
- EPANET wrapper and bundled upstream EPANET: preserve their existing format and engine compatibility semantics until a separate compatibility change with reference comparisons. Do **not** mechanically replace historical EPANET factors with physical constants.

## Reviewed conversion sites

GUI EPANET-JS adapter, settings, and water-quality import use the central international-foot and flow conversions. The remaining `10.0`, `1000.0`, `0.001` in the adapter represent simple metric-prefix conversions, not approximated customary conversion factors.

The EPANET integration has its own INP/report import, geometry, result reader, hydraulic energy thresholds, MSX diffusivity, and coordinate-reference transforms. These are **not** migrated in this patch: pressure conversions in report import may intentionally track EPANET conventions; US survey feet in CRS transformation are not international feet; thresholds and reference diffusivity are not generic unit constants. Review each with independent compatibility fixtures before migration.

## API and semantics

- `feetToMetres` uses international feet. `usSurveyFeetToMetres` is distinct.
- `usGallonsToCubicMetres` means US liquid gallons; imperial gallons are used only in their explicitly named flow conversion.
- Canonical flow is m³/h and canonical diameter is mm; this is not a general SI-base-units type system.
- `pascalsToMetresHead` requires explicit density (kg/m³) and gravity (m/s²); it does not assume a fluid or EPANET pressure convention.
- Physical conversion functions operate on `double`, and do not validate nonfinite inputs; format boundaries are responsible for rejecting inappropriate inputs.

## Regression policy

`unit_conversion_test.cpp` checks independent numerical references and round trips. Checks must run in Release/NDEBUG builds too; they must not rely on `assert`. GUI EPANET-JS tests cover aliases, pressure and emitter behavior, metric/US fixtures. Linux GUI suite was reported as 7/7 passing before this patch; rerun after application. Windows and WASM builds remain unverified here.

## Scope closure

No solver-conditioning, physical uncertainty, or hydraulic-confidence work belongs to this unit-conversion cleanup. Further EPANET engine factor changes are a separate task.
