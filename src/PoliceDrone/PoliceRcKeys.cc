#include "PoliceRcKeys.h"

#include <QtCore/QApplicationStatic>

#include "Fact.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "SiyiCameraController.h"
#include "SiyiCameraSettings.h"
#include "SiyiProtocol.h"
#include "Vehicle.h"

QGC_LOGGING_CATEGORY(PoliceRcKeysLog, "PoliceDrone.RcKeys")

namespace {

// ponytail: fixed bands for a 1000/1500/2000 us handset; move to settings if another handset's
// endpoints differ.
constexpr int kLowMax = 1300, kMidMin = 1400, kMidMax = 1600, kHighMin = 1700;
// 0 (ArduPilot) and UINT16_MAX (PX4) mean the channel is not sent.
constexpr int kValidMin = 800, kValidMax = 2200;

Fact* channelSetting(PoliceRcKeys::Key key)
{
    SiyiCameraSettings* const s = SettingsManager::instance()->siyiCameraSettings();
    switch (key) {
    case PoliceRcKeys::Fpv:     return s->rcKeyFpvChannel();
    case PoliceRcKeys::Zoom:    return s->rcKeyZoomChannel();
    case PoliceRcKeys::Wide:    return s->rcKeyWideChannel();
    default:                    return s->rcKeyThermalChannel();
    }
}

}  // namespace

Q_APPLICATION_STATIC(PoliceRcKeys, _policeRcKeysInstance, nullptr);

PoliceRcKeys::PoliceRcKeys(QObject* parent) : QObject(parent)
{
    for (int key = 0; key < KeyCount; ++key) {
        (void) connect(channelSetting(static_cast<Key>(key)), &Fact::rawValueChanged, this, &PoliceRcKeys::_resetBaselines);
    }
}

PoliceRcKeys* PoliceRcKeys::instance()
{
    return _policeRcKeysInstance();
}

void PoliceRcKeys::init()
{
    MultiVehicleManager* const manager = MultiVehicleManager::instance();
    (void) connect(manager, &MultiVehicleManager::activeVehicleChanged, this, &PoliceRcKeys::_follow);
    _follow(manager->activeVehicle());
}

void PoliceRcKeys::_follow(Vehicle* vehicle)
{
    if (_vehicle) {
        (void) disconnect(_vehicle, nullptr, this, nullptr);
    }
    _vehicle = vehicle;
    _resetBaselines();
    if (_vehicle) {
        (void) connect(_vehicle, &Vehicle::rcChannelsRawChanged, this, &PoliceRcKeys::handleChannels);
    }
}

void PoliceRcKeys::_resetBaselines()
{
    std::fill(std::begin(_position), std::end(_position), 0);
}

int PoliceRcKeys::positionFor(int us, int last)
{
    if ((us < kValidMin) || (us > kValidMax)) return last;
    if (us < kLowMax) return 1;
    if (us > kHighMin) return 3;
    if ((us >= kMidMin) && (us <= kMidMax)) return 2;
    return last;
}

int PoliceRcKeys::imageTypeFor(Key key, bool aiEnabled)
{
    // The AI module infers on the main stream and needs the zoom camera there, the same guard
    // the camera panel's 광각/줌 button uses.
    if (aiEnabled) return -1;
    if (key == Zoom) return static_cast<int>(SiyiProtocol::CameraImageType::MainZoomSubThermal);
    if (key == Wide) return static_cast<int>(SiyiProtocol::CameraImageType::MainWideAngleSubThermal);
    return -1;
}

void PoliceRcKeys::handleChannels(const QVector<int>& values)
{
    for (int k = 0; k < KeyCount; ++k) {
        const Key key = static_cast<Key>(k);
        const int channel = channelSetting(key)->rawValue().toInt();
        if ((channel < 1) || (channel > values.size())) continue;
        const int position = positionFor(values[channel - 1], _position[k]);
        if ((position == 0) || (position == _position[k])) continue;
        const bool baseline = (_position[k] == 0);
        _position[k] = position;
        if (!baseline) _press(key);
    }
}

void PoliceRcKeys::_press(Key key)
{
    static const char* const panel[KeyCount] = {"primary", "secondary", "secondary", "shared"};
    SiyiCameraController* const camera = SiyiCameraController::instance();
    const bool ai = SettingsManager::instance()->siyiCameraSettings()->aiEnabled()->rawValue().toBool();

    if ((key == Wide) && ai) {
        qCInfo(PoliceRcKeysLog) << "wide key: AI module on, main stream stays on the zoom camera";
    }
    const int imageType = imageTypeFor(key, ai);
    if (imageType >= 0) {
        camera->setCameraImageType(imageType);
    }
    emit camera->mainPictureRequested(QString::fromLatin1(panel[key]));
}
