#ifndef MAP_BGFX_GLOBE_HUD_CONTROLLER_H
#define MAP_BGFX_GLOBE_HUD_CONTROLLER_H

#include "map/bgfx/map_bgfx_hud.h"

#include <QObject>
#include <QSize>
#include <QString>
#include <QTimer>

#include <array>

class MapModel;
class QVariantAnimation;

// The camera side of the bgfx globe HUD (roadmap Phase 3, slice 5): what the
// QWidget globe HUD's sliders and compass do to MapModel
// (map_monitor_hud_controls.cpp), driven by MapBgfxHud's commands.
//
// - Camera sliders (network height, tilt, distance, vertical exaggeration):
//   the same value mapping (0.1 m / 1 deg / logarithmic 1000 steps / 0.1x),
//   tilt and distance drags inside a rotate interaction (the terrain follow
//   freezes the pivot), right click animates back to the default (240 ms).
// - Compass: drags turn the camera by the horizontal pointer motion, a
//   click animates to north (280 ms), the wheel turns 5 deg per notch inside
//   a rotate interaction that ends 350 ms after the last notch.
// - HUD state: slider positions and labels in the user's units, the globe
//   scale (MapMonitorScaleHudWidget's formula).
//
// Every interaction stops when the view mode changes.
class MapBgfxGlobeHudController final : public QObject
{
public:
    explicit MapBgfxGlobeHudController(MapModel *map_model, QObject *parent = nullptr);
    ~MapBgfxGlobeHudController() override;

    // A HUD command of a slider or the compass (GlobeSlider,
    // GlobeSliderReset, CompassDrag, CompassWheel); others are ignored.
    void handleCommand(const MapBgfxHud::Command &command);

    // The globe part of the HUD state for the current camera: yaw, slider
    // positions and labels, scale (viewport: logical size of the map).
    // Toggles, underground mode and terrain-mesh progress are the
    // surface's and left untouched.
    static void fillHudState(const MapModel &map_model, const QSize &viewport, MapBgfxHud::Globe *globe);

private:
    void setSliderFraction(int index, double fraction);
    void resetSlider(int index);
    void compassPressed();
    void compassDragged(double delta_x);
    void compassReleased(bool clicked);
    void compassWheel(double notches);
    void animateNorth();
    void stopNorthAnimation();
    void finishWheelRotation();
    void stopEverything();
    bool globeShown() const;

    MapModel *map_model = nullptr;
    std::array<QVariantAnimation *, MapBgfxHud::GlobeSliderCount> reset_animations{};
    QVariantAnimation *north_animation = nullptr;
    double north_delta_yaw_deg = 0.0;
    double north_last_progress = 0.0;
    // A slider drag (tilt and distance hold a rotate interaction) and the
    // compass drag in progress.
    int dragged_slider = -1;
    bool compass_drag_active = false;
    // Sub-pixel pointer motion carried to the next compass step.
    double compass_remainder_x = 0.0;
    QTimer wheel_end_timer;
    bool wheel_rotate_active = false;
};

#endif // MAP_BGFX_GLOBE_HUD_CONTROLLER_H
