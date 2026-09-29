#pragma once

#include "QmlUITestBase.h"

/// The laser aim point and reading on the zoom picture: up only full screen with the laser
/// switched on, and reading 범위 밖 while the pod gives no distance.
class PoliceLrfOverlayUITest : public QmlUITestBase
{
    Q_OBJECT

private slots:
    /// Windowed with the laser on: no overlay. Full screen with the laser on: overlay up,
    /// reading 범위 밖. Laser switched off through the controller: overlay gone.
    void _testOverlayFollowsFullscreenAndLaser();
};
