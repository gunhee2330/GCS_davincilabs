#include "PoliceRcKeysTest.h"

#include <QtCore/QRegularExpression>
#include <QtTest/QSignalSpy>

#include "Fact.h"
#include "PoliceRcKeys.h"
#include "SettingsManager.h"
#include "SiyiCameraController.h"
#include "SiyiCameraSettings.h"

namespace {

SiyiCameraSettings* settings()
{
    return SettingsManager::instance()->siyiCameraSettings();
}

/// Sixteen channels at rest (1000 us) with the given channel (1-based) at \a us.
QVector<int> reading(int channel = 0, int us = 1000)
{
    QVector<int> values(16, 1000);
    if (channel > 0) values[channel - 1] = us;
    return values;
}

}  // namespace

void PoliceRcKeysTest::cleanup()
{
    settings()->aiEnabled()->setRawValue(false);
    settings()->rcKeyFpvChannel()->setRawValue(13);
    settings()->rcKeyZoomChannel()->setRawValue(14);
    settings()->rcKeyWideChannel()->setRawValue(15);
    settings()->rcKeyThermalChannel()->setRawValue(16);
    UnitTest::cleanup();
}

void PoliceRcKeysTest::_positionFor_test()
{
    QCOMPARE(PoliceRcKeys::positionFor(1000, 0), 1);
    QCOMPARE(PoliceRcKeys::positionFor(1299, 0), 1);
    QCOMPARE(PoliceRcKeys::positionFor(1350, 1), 1);   // dead band keeps the last band
    QCOMPARE(PoliceRcKeys::positionFor(1350, 0), 0);
    QCOMPARE(PoliceRcKeys::positionFor(1400, 1), 2);
    QCOMPARE(PoliceRcKeys::positionFor(1600, 1), 2);
    QCOMPARE(PoliceRcKeys::positionFor(1650, 3), 3);
    QCOMPARE(PoliceRcKeys::positionFor(1701, 1), 3);
    QCOMPARE(PoliceRcKeys::positionFor(2000, 1), 3);
    QCOMPARE(PoliceRcKeys::positionFor(0, 2), 2);
    QCOMPARE(PoliceRcKeys::positionFor(65535, 2), 2);
    QCOMPARE(PoliceRcKeys::positionFor(799, 0), 0);
    QCOMPARE(PoliceRcKeys::positionFor(2201, 0), 0);
}

void PoliceRcKeysTest::_imageTypeFor_test()
{
    QCOMPARE(PoliceRcKeys::imageTypeFor(PoliceRcKeys::Zoom, false), 3);
    QCOMPARE(PoliceRcKeys::imageTypeFor(PoliceRcKeys::Wide, false), 5);
    QCOMPARE(PoliceRcKeys::imageTypeFor(PoliceRcKeys::Zoom, true), -1);
    QCOMPARE(PoliceRcKeys::imageTypeFor(PoliceRcKeys::Wide, true), -1);
    QCOMPARE(PoliceRcKeys::imageTypeFor(PoliceRcKeys::Fpv, false), -1);
    QCOMPARE(PoliceRcKeys::imageTypeFor(PoliceRcKeys::Thermal, false), -1);
}

void PoliceRcKeysTest::_baseline_test()
{
    PoliceRcKeys keys;
    QSignalSpy spy(SiyiCameraController::instance(), &SiyiCameraController::mainPictureRequested);

    // First reading, keys already held high: recorded only.
    keys.handleChannels(reading(16, 2000));
    QCOMPARE(spy.count(), 0);
    // Same reading again: nothing.
    keys.handleChannels(reading(16, 2000));
    QCOMPARE(spy.count(), 0);
    // Release: a band change, the thermal key's action.
    keys.handleChannels(reading(16, 1000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("shared"));
}

void PoliceRcKeysTest::_keys_test()
{
    PoliceRcKeys keys;
    QSignalSpy spy(SiyiCameraController::instance(), &SiyiCameraController::mainPictureRequested);
    keys.handleChannels(reading());
    QCOMPARE(spy.count(), 0);

    // Zoom: routes the zoom camera (refused here, the pod is not a ZT30) and asks for the zoom window.
    expectLogMessage("SiyiCamera.SiyiCameraController", QtWarningMsg, QRegularExpression(QStringLiteral("ZT30 only")));
    keys.handleChannels(reading(14, 2000));
    verifyExpectedLogMessage();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("secondary"));
    keys.handleChannels(reading(14, 2000));
    QCOMPARE(spy.count(), 0);
    expectLogMessage("SiyiCamera.SiyiCameraController", QtWarningMsg, QRegularExpression(QStringLiteral("ZT30 only")));
    keys.handleChannels(reading());
    verifyExpectedLogMessage();
    QCOMPARE(spy.count(), 1);
    spy.clear();

    keys.handleChannels(reading(16, 2000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("shared"));
    keys.handleChannels(reading());
    spy.clear();

    keys.handleChannels(reading(13, 1500));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("primary"));
}

void PoliceRcKeysTest::_wideWithAiOn_test()
{
    settings()->aiEnabled()->setRawValue(true);
    SiyiCameraController* const camera = SiyiCameraController::instance();
    const int before = camera->cameraImageType();

    PoliceRcKeys keys;
    QSignalSpy spy(camera, &SiyiCameraController::mainPictureRequested);
    keys.handleChannels(reading());
    // No ZT30 warning either: the routing is not attempted at all.
    keys.handleChannels(reading(15, 2000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("secondary"));
    QCOMPARE(camera->cameraImageType(), before);
}

void PoliceRcKeysTest::_ignoredReadings_test()
{
    PoliceRcKeys keys;
    QSignalSpy spy(SiyiCameraController::instance(), &SiyiCameraController::mainPictureRequested);
    keys.handleChannels(reading());

    keys.handleChannels(reading(16, 0));
    keys.handleChannels(reading(16, 65535));
    keys.handleChannels(QVector<int>(12, 2000));   // shorter than every key channel
    QCOMPARE(spy.count(), 0);

    // Channel 0 turns the key off.
    settings()->rcKeyThermalChannel()->setRawValue(0);
    keys.handleChannels(reading());
    keys.handleChannels(reading(16, 2000));
    QCOMPARE(spy.count(), 0);
}

void PoliceRcKeysTest::_settingChangeRebaselines_test()
{
    PoliceRcKeys keys;
    QSignalSpy spy(SiyiCameraController::instance(), &SiyiCameraController::mainPictureRequested);
    keys.handleChannels(reading());

    // Moving the thermal key to channel 9, where the handset already sits high: recorded only.
    settings()->rcKeyThermalChannel()->setRawValue(9);
    keys.handleChannels(reading(9, 2000));
    QCOMPARE(spy.count(), 0);
    keys.handleChannels(reading());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("shared"));
}

UT_REGISTER_TEST(PoliceRcKeysTest, TestLabel::Unit)
