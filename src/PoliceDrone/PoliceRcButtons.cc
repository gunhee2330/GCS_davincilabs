#include "PoliceRcButtons.h"

#include <algorithm>
#include <iterator>
#include <vector>

#include <QtCore/QApplicationStatic>
#include <QtCore/QCoreApplication>
#include <QtCore/QPermissions>

#ifdef Q_OS_ANDROID
#include <QtCore/QJniEnvironment>
#include <QtCore/QJniObject>
#include <jni.h>
#endif

#include "Fact.h"
#include "MultiVehicleManager.h"
#include "PoliceLiveVoice.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "SiyiCameraController.h"
#include "SiyiCameraSettings.h"
#include "Vehicle.h"

QGC_LOGGING_CATEGORY(PoliceRcButtonsLog, "PoliceDrone.RcButtons")

namespace {

// PoliceRcKeys' bands. A momentary button reads 1050 released and 1950 held.
constexpr int kLowMax = 1300, kHighMin = 1700;
// 0 (ArduPilot) and UINT16_MAX (PX4) mean the channel is not sent; the handset stream uses -1.
constexpr int kValidMin = 800, kValidMax = 2200;
constexpr int kStaleCheckMsecs = 250;

Fact* channelSetting(PoliceRcButtons::Button button)
{
    SiyiCameraSettings* const s = SettingsManager::instance()->siyiCameraSettings();
    switch (button) {
    case PoliceRcButtons::Center: return s->rcKeyCenterChannel();
    case PoliceRcButtons::Talk:   return s->rcKeyTalkChannel();
    default:                      return s->rcKeyCameraChannel();
    }
}

// PoliceRcKeys' keys: a button of ours on one of their channels would fire both.
QList<Fact*> otherKeySettings()
{
    SiyiCameraSettings* const s = SettingsManager::instance()->siyiCameraSettings();
    return {s->rcKeyFpvChannel(), s->rcKeyZoomChannel(), s->rcKeyWideChannel(), s->rcKeyThermalChannel()};
}

#ifdef Q_OS_ANDROID
constexpr const char* kMonitorClass = "org/mavlink/qgroundcontrol/PoliceRcChannelMonitor";

// Binder thread: hop onto the object's thread.
void jniChannels(JNIEnv* env, jclass, jintArray values)
{
    const jsize count = env->GetArrayLength(values);
    std::vector<jint> buffer(static_cast<size_t>(count));
    env->GetIntArrayRegion(values, 0, count, buffer.data());
    const QVector<int> channels(buffer.cbegin(), buffer.cend());
    PoliceRcButtons* const buttons = PoliceRcButtons::instance();
    (void) QMetaObject::invokeMethod(buttons, [buttons, channels] { buttons->handleHandsetChannels(channels); },
                                     Qt::QueuedConnection);
}

void jniLost(JNIEnv*, jclass)
{
    PoliceRcButtons* const buttons = PoliceRcButtons::instance();
    (void) QMetaObject::invokeMethod(buttons, [buttons] { buttons->handleHandsetLost(); }, Qt::QueuedConnection);
}
#endif

}  // namespace

Q_APPLICATION_STATIC(PoliceRcButtons, _policeRcButtonsInstance, nullptr);

PoliceRcButtons::PoliceRcButtons(QObject* parent) : QObject(parent)
{
    _clock.start();
    QList<Fact*> settings = otherKeySettings();
    for (int button = 0; button < ButtonCount; ++button) {
        settings.append(channelSetting(static_cast<Button>(button)));
    }
    for (Fact* setting : settings) {
        (void) connect(setting, &Fact::rawValueChanged, this, [this]() {
            _letGo(false);
            _resetBaselines();
            _refreshChannels();
        });
    }
    _refreshChannels();

    _staleTimer.setInterval(kStaleCheckMsecs);
    (void) connect(&_staleTimer, &QTimer::timeout, this, &PoliceRcButtons::_checkStale);

    // Handset readings come only on a change, so the two second mark needs a clock of its own.
    _recordTimer.setSingleShot(true);
    _recordTimer.setInterval(kRecordHoldMsecs);
    (void) connect(&_recordTimer, &QTimer::timeout, this, [this]() {
        if (_held[Camera] && _pressSeen[Camera] && !_recordingToggledThisPress) {
            _recordingToggledThisPress = true;
            emit recordingToggleRequested();
        }
    });
}

PoliceRcButtons* PoliceRcButtons::instance()
{
    return _policeRcButtonsInstance();
}

void PoliceRcButtons::init()
{
    // Resolved when a button is pressed, not now: both are created on first use.
    (void) connect(this, &PoliceRcButtons::centerPressed, this, []() { SiyiCameraController::instance()->center(); });
    (void) connect(this, &PoliceRcButtons::photoRequested, this, []() { SiyiCameraController::instance()->takePhoto(); });
    (void) connect(this, &PoliceRcButtons::recordingToggleRequested, this,
                   []() { SiyiCameraController::instance()->toggleRecording(); });

    // Only a talk this button started is ended by it, so letting go of S1 cannot cut off the
    // screen's push-to-talk. The key never asks for the microphone: the system sheet would cover
    // the video in flight, so it is asked for below, at start.
    (void) connect(this, &PoliceRcButtons::talkPressed, this, [this]() {
        PoliceLiveVoice* const voice = PoliceLiveVoice::instance();
        if (voice->talking()) {
            return;
        }
        if (QCoreApplication::instance()->checkPermission(QMicrophonePermission{}) != Qt::PermissionStatus::Granted) {
            qCWarning(PoliceRcButtonsLog) << "Talk key ignored: microphone permission not granted";
            return;
        }
        voice->startTalking();
        _talkingFromKey = voice->talking();
        (void) connect(voice, &PoliceLiveVoice::talkingChanged, this, &PoliceRcButtons::_voiceTalkingChanged,
                       Qt::UniqueConnection);
    });
    (void) connect(this, &PoliceRcButtons::talkReleased, this, [this]() {
        if (_talkingFromKey) {
            _talkingFromKey = false;
            PoliceLiveVoice::instance()->stopTalking();
        }
    });
    if (_channel[Talk] > 0) {
        const QMicrophonePermission microphone;
        if (QCoreApplication::instance()->checkPermission(microphone) == Qt::PermissionStatus::Undetermined) {
            QCoreApplication::instance()->requestPermission(microphone, this, [](const QPermission&) {});
        }
    }

    MultiVehicleManager* const manager = MultiVehicleManager::instance();
    (void) connect(manager, &MultiVehicleManager::activeVehicleChanged, this, &PoliceRcButtons::_follow);
    _follow(manager->activeVehicle());
    _staleTimer.start();
    _startHandsetStream();
}

void PoliceRcButtons::_startHandsetStream()
{
#ifdef Q_OS_ANDROID
    const JNINativeMethod methods[]{
        {"nativeChannels", "([I)V", reinterpret_cast<void*>(jniChannels)},
        {"nativeLost", "()V", reinterpret_cast<void*>(jniLost)},
    };
    QJniEnvironment env;
    if (!env.registerNativeMethods(kMonitorClass, methods, std::size(methods))) {
        qCWarning(PoliceRcButtonsLog) << "Failed to register native methods for" << kMonitorClass;
        return;
    }
    QJniObject::callStaticMethod<void>(kMonitorClass, "start", "(Landroid/content/Context;)V",
                                       QNativeInterface::QAndroidApplication::context().object());
    if (env.checkAndClearExceptions()) {
        qCWarning(PoliceRcButtonsLog) << "PoliceRcChannelMonitor.start threw";
    }
#endif
}

void PoliceRcButtons::_setTalkHeld(bool held)
{
    if (held == _talkHeldSent) {
        return;
    }
    _talkHeldSent = held;
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kMonitorClass, "setTalkHeld", "(Z)V", static_cast<jboolean>(held));
    QJniEnvironment env;
    (void) env.checkAndClearExceptions();
#endif
}

void PoliceRcButtons::_follow(Vehicle* vehicle)
{
    if (_vehicle) {
        (void) disconnect(_vehicle, nullptr, this, nullptr);
    }
    if (!_handsetLive) {
        _letGo(true);
        _resetBaselines();
    }
    _lastReadingMsecs = -1;
    _vehicle = vehicle;
    if (_vehicle) {
        (void) connect(_vehicle, &Vehicle::rcChannelsRawChanged, this, &PoliceRcButtons::handleChannels);
    }
}

void PoliceRcButtons::_refreshChannels()
{
    QList<int> taken;
    for (const Fact* setting : otherKeySettings()) {
        taken.append(setting->rawValue().toInt());
    }
    for (int b = 0; b < ButtonCount; ++b) {
        const Button button = static_cast<Button>(b);
        const int channel = channelSetting(button)->rawValue().toInt();
        if ((channel > 0) && taken.contains(channel)) {
            qCWarning(PoliceRcButtonsLog) << channelSetting(button)->name() << "off: channel" << channel
                                          << "is already a key's";
            _channel[b] = 0;
            continue;
        }
        _channel[b] = channel;
        taken.append(channel);
    }
}

bool PoliceRcButtons::heldFor(int us, bool last)
{
    if ((us < kValidMin) || (us > kValidMax)) {
        return last;
    }
    if (us > kHighMin) {
        return true;
    }
    if (us < kLowMax) {
        return false;
    }
    return last;
}

void PoliceRcButtons::handleChannels(const QVector<int>& values)
{
    handleChannelsAt(values, _clock.elapsed());
}

void PoliceRcButtons::handleChannelsAt(const QVector<int>& values, qint64 nowMsecs)
{
    if (_handsetLive) {
        return;
    }
    if ((_lastReadingMsecs >= 0) && ((nowMsecs - _lastReadingMsecs) > kStaleMsecs)) {
        _letGo(true);
        _resetBaselines();
    }
    _lastReadingMsecs = nowMsecs;
    _process(values, nowMsecs);
}

void PoliceRcButtons::handleHandsetChannels(const QVector<int>& values)
{
    handleHandsetChannelsAt(values, _clock.elapsed());
}

void PoliceRcButtons::handleHandsetChannelsAt(const QVector<int>& values, qint64 nowMsecs)
{
    if (!_handsetLive) {
        qCDebug(PoliceRcButtonsLog) << "Handset channel stream live; RC_CHANNELS ignored";
        _handsetLive = true;
        _letGo(true);
        _resetBaselines();
        _lastReadingMsecs = -1;
    }
    _process(values, nowMsecs);
}

void PoliceRcButtons::handleHandsetLost()
{
    if (!_handsetLive) {
        return;
    }
    qCDebug(PoliceRcButtonsLog) << "Handset channel stream lost; following RC_CHANNELS";
    _handsetLive = false;
    _letGo(true);
    _resetBaselines();
}

void PoliceRcButtons::_process(const QVector<int>& values, qint64 nowMsecs)
{
    for (int b = 0; b < ButtonCount; ++b) {
        const Button button = static_cast<Button>(b);
        const int channel = _channel[b];
        if ((channel < 1) || (channel > values.size())) {
            continue;
        }
        const int us = values[channel - 1];
        if ((us < kValidMin) || (us > kValidMax)) {
            continue;
        }
        // A talk ends on any reading that is not plainly held: a centred switch, a dial or a
        // failsafe value must not keep the microphone open. The other buttons keep the dead band.
        const bool held = (button == Talk) ? (us > kHighMin) : heldFor(us, _held[b]);
        if (!_known[b]) {
            _known[b] = true;
            _held[b] = held;
            if ((button == Talk) && _resumeTalk && held) {
                // Only the readings were cut off, and the key is still down: the talk goes on.
                _pressSeen[Talk] = true;
                emit talkPressed();
            }
            if (button == Talk) {
                _resumeTalk = false;
            }
            continue;
        }
        if (held != _held[b]) {
            _held[b] = held;
            _changed(button, held, nowMsecs);
        } else if (held && (button == Camera) && _pressSeen[Camera] && !_recordingToggledThisPress &&
                   ((nowMsecs - _cameraPressedMsecs) >= kRecordHoldMsecs)) {
            // At the two second mark, while still held, so the operator knows when to let go.
            _recordingToggledThisPress = true;
            emit recordingToggleRequested();
        }
    }
    _setTalkHeld(_held[Talk] && _pressSeen[Talk]);
}

void PoliceRcButtons::_changed(Button button, bool held, qint64 nowMsecs)
{
    if (held) {
        _pressSeen[button] = true;
    } else if (!_pressSeen[button]) {
        // The press happened before the baseline.
        return;
    }

    switch (button) {
    case Center:
        if (held) {
            emit centerPressed();
        }
        break;
    case Talk:
        if (held) {
            emit talkPressed();
        } else {
            emit talkReleased();
        }
        break;
    case Camera:
        if (held) {
            _cameraPressedMsecs = nowMsecs;
            _recordingToggledThisPress = false;
            _recordTimer.start();
        } else {
            _recordTimer.stop();
            if (!_recordingToggledThisPress) {
                // Sparse readings can skip the two second mark; the length decides on release too.
                if ((nowMsecs - _cameraPressedMsecs) >= kRecordHoldMsecs) {
                    emit recordingToggleRequested();
                } else {
                    emit photoRequested();
                }
            }
        }
        break;
    default:
        break;
    }
    if (!held) {
        _pressSeen[button] = false;
    }
}

void PoliceRcButtons::_letGo(bool mayResume)
{
    // A talk must not stay open past the readings that would have carried its release. A held
    // camera button does nothing: how long it was held is no longer known.
    _resumeTalk = false;
    if (_pressSeen[Talk] && _held[Talk]) {
        emit talkReleased();
        _resumeTalk = mayResume;
    }
}

void PoliceRcButtons::_resetBaselines()
{
    std::fill(std::begin(_known), std::end(_known), false);
    std::fill(std::begin(_held), std::end(_held), false);
    std::fill(std::begin(_pressSeen), std::end(_pressSeen), false);
    _recordingToggledThisPress = false;
    _recordTimer.stop();
    _setTalkHeld(false);
}

void PoliceRcButtons::_checkStale()
{
    if (_handsetLive) {
        return;
    }
    if ((_lastReadingMsecs >= 0) && ((_clock.elapsed() - _lastReadingMsecs) > kStaleMsecs)) {
        qCDebug(PoliceRcButtonsLog) << "RC_CHANNELS stopped, letting go of held buttons";
        _letGo(true);
        _resetBaselines();
        _lastReadingMsecs = -1;
    }
}

void PoliceRcButtons::_voiceTalkingChanged()
{
    // The talk can end without the key: the five minute cap, leaving the app, the screen button.
    if (!PoliceLiveVoice::instance()->talking()) {
        _talkingFromKey = false;
    }
}
