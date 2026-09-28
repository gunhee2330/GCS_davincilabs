#include "PoliceWarnings.h"

#include <algorithm>

#include <QtCore/QRegularExpression>

#include "AudioOutput.h"
#include "Fact.h"
#include "ParameterManager.h"
#include "Vehicle.h"

namespace {

/// Every parameter a limit is read from. A change to any of them re-reads the limits at once
/// rather than at the next telemetry change, which on a hovering aircraft may be a while.
constexpr const char* kLimitParams[] = {
    "GF_MAX_VER_DIST", "GF_MAX_HOR_DIST", "COM_WIND_WARN",
    "FENCE_ENABLE", "FENCE_TYPE", "FENCE_ALT_MAX", "FENCE_RADIUS",
};

/// ArduPilot FENCE_TYPE bits.
constexpr int kFenceAltitudeBit = 1;
constexpr int kFenceCircleBit   = 2;

/// Up when \a on, down when \a off, and in the band between the two it stays as it was. A NaN
/// reading is neither, so a dropout holds the banner rather than flicking it.
bool latch(bool active, bool on, bool off)
{
    return on || (active && !off);
}

}  // namespace

PoliceWarnings::PoliceWarnings(QObject* parent) : QObject(parent)
{
    s_instances.append(this);
}

PoliceWarnings::~PoliceWarnings()
{
    (void) s_instances.removeOne(this);
}

bool PoliceWarnings::voicesBattery(const Vehicle* vehicle)
{
    return vehicle && std::any_of(s_instances.cbegin(), s_instances.cend(),
                                  [vehicle](const PoliceWarnings* warnings) { return warnings->_vehicle.data() == vehicle; });
}

bool PoliceWarnings::isBatteryAnnouncement(const QString& text)
{
    // ArduPilot AP_BattMonitor::check_failsafes and Copter::announce_failsafe (Copter 4.5); the
    // failsafe line with an action after it ("Battery Failsafe: Disarming") says something the
    // sentence here does not, and is left alone. PX4 v1.17 FailsafeBase::notifyUser: those two
    // come as events, which this build lists without speaking (Vehicle::_onStatusTextFromEvent),
    // and are here so they stay silent should that path ever read them out as upstream once did.
    static const QRegularExpression ardupilot(QStringLiteral("^Battery \\d+ is (low|critical) "));
    return ardupilot.match(text).hasMatch()
           || (text == QStringLiteral("Battery Failsafe"))
           || (text == QStringLiteral("Low battery level, return advised"))
           || (text == QStringLiteral("Critical battery level, land now"));
}

void PoliceWarnings::setVehicle(Vehicle* vehicle)
{
    if (_vehicle == vehicle) {
        return;
    }

    if (_vehicle) {
        (void) disconnect(_vehicle->parameterManager(), nullptr, this, nullptr);
    }
    _vehicle = vehicle;
    if (_vehicle) {
        (void) connect(_vehicle->parameterManager(), &ParameterManager::parametersReadyChanged,
                       this, &PoliceWarnings::_watchLimits);
        _watchLimits();
    }

    emit vehicleChanged();
    _evaluate();
}

void PoliceWarnings::_watchLimits()
{
    if (!_vehicle || !_vehicle->parameterManager()->parametersReady()) {
        return;
    }
    ParameterManager* const params = _vehicle->parameterManager();
    for (const char* const name : kLimitParams) {
        if (params->parameterExists(ParameterManager::defaultComponentId, name)) {
            (void) connect(params->getParameter(ParameterManager::defaultComponentId, name), &Fact::rawValueChanged,
                           this, &PoliceWarnings::_evaluate, Qt::UniqueConnection);
        }
    }
    _evaluate();
}

double PoliceWarnings::_param(const char* name) const
{
    if (!_vehicle || !_vehicle->parameterManager()->parametersReady()
        || !_vehicle->parameterManager()->parameterExists(ParameterManager::defaultComponentId, name)) {
        return qQNaN();
    }
    return _vehicle->parameterManager()->getParameter(ParameterManager::defaultComponentId, name)->rawValue().toDouble();
}

bool PoliceWarnings::_fenceHas(int bit) const
{
    const double type = _param("FENCE_TYPE");
    return (_param("FENCE_ENABLE") == 1) && !qIsNaN(type) && (static_cast<int>(type) & bit);
}

double PoliceWarnings::altitudeLimit() const
{
    const double px4 = _param("GF_MAX_VER_DIST");
    if (px4 > 0) {
        return px4;
    }
    const double ardupilot = _param("FENCE_ALT_MAX");
    if (_fenceHas(kFenceAltitudeBit) && (ardupilot > 0)) {
        return ardupilot;
    }
    return kDefaultCeilingM;
}

double PoliceWarnings::radiusLimit() const
{
    const double px4 = _param("GF_MAX_HOR_DIST");
    if (px4 > 0) {
        return px4;
    }
    const double ardupilot = _param("FENCE_RADIUS");
    if (_fenceHas(kFenceCircleBit) && (ardupilot > 0)) {
        return ardupilot;
    }
    return qQNaN();
}

double PoliceWarnings::windLimit() const
{
    const double px4 = _param("COM_WIND_WARN");
    return (px4 > 0) ? px4 : kDefaultWindWarnMps;
}

void PoliceWarnings::_evaluate()
{
    // Up with the top bar the moment it changes colour; down only once the pack reads
    // kBatteryHysteresisPercent above where it went up. With no percentage it follows the bar.
    int level = std::clamp(_batteryInput, 0, 2);
    if ((level < _batteryLevel) && (_batteryPercent < (_batteryEntryPercent + kBatteryHysteresisPercent))) {
        level = _batteryLevel;
    }
    const bool batteryRose = level > _batteryLevel;
    if (batteryRose) {
        _batteryEntryPercent = _batteryPercent;
    }
    _batteryLevel = level;

    const double altitudeLimit = this->altitudeLimit();
    const double radiusLimit = this->radiusLimit();
    const double windLimit = this->windLimit();

    const bool altitudeWas = _altitudeOver;
    const bool radiusWas = _radiusOver;
    const bool windWas = _windHigh;
    _altitudeOver = _flying && latch(_altitudeOver, _altitude > altitudeLimit,
                                     _altitude <= (altitudeLimit - kDistanceHysteresisM));
    _radiusOver = _flying && !qIsNaN(radiusLimit)
                  && latch(_radiusOver, _homeDistance > radiusLimit, _homeDistance <= (radiusLimit - kDistanceHysteresisM));
    _windHigh = _flying && latch(_windHigh, _windSpeed >= windLimit, _windSpeed < (windLimit - kWindHysteresisMps));

    const QString action = (_batteryLevel == 2) ? tr("즉시 복귀하십시오") : tr("복귀를 준비하십시오");
    const int percent = qIsNaN(_batteryPercent) ? 0 : qRound(_batteryPercent);
    const QString text[WarningCount] = {
        (_batteryLevel == 0) ? QString()
            : qIsNaN(_batteryPercent) ? action
            : tr("잔량 %1%, %2").arg(percent).arg(action),
        _altitudeOver ? tr("제한 고도 %1 m를 넘었습니다").arg(qRound(altitudeLimit)) : QString(),
        _radiusOver ? tr("제한 반경 %1 m를 넘었습니다").arg(qRound(radiusLimit)) : QString(),
        _windHigh ? tr("제자리 유지가 어렵습니다") : QString(),
    };

    // A dismissal lasts while its condition does; the battery's also ends when it steps up.
    bool changed = false;
    for (int i = 0; i < WarningCount; i++) {
        const bool dismissed = _dismissed[i] && !text[i].isEmpty() && !((i == Battery) && batteryRose);
        if ((text[i] != _text[i]) || (dismissed != _dismissed[i])) {
            _text[i] = text[i];
            _dismissed[i] = dismissed;
            changed = true;
        }
    }
    if (changed) {
        emit textsChanged();
    }

    if (batteryRose) {
        _say(qIsNaN(_batteryPercent) ? tr("배터리가 부족합니다. %1.").arg(action)
                                     : tr("배터리가 부족합니다. 잔량 %1퍼센트. %2.").arg(percent).arg(action));
    }
    if (_altitudeOver && !altitudeWas) {
        _say(tr("고도 초과. 제한 고도 %1미터를 넘었습니다.").arg(qRound(altitudeLimit)));
    }
    if (_radiusOver && !radiusWas) {
        _say(tr("반경 초과. 제한 반경 %1미터를 넘었습니다.").arg(qRound(radiusLimit)));
    }
    if (_windHigh && !windWas) {
        _say(tr("강풍 경고. 제자리 유지가 어렵습니다."));
    }
}

void PoliceWarnings::dismiss(Warning warning)
{
    if ((warning >= 0) && (warning < WarningCount) && !_text[warning].isEmpty() && !_dismissed[warning]) {
        _dismissed[warning] = true;
        emit textsChanged();
    }
}

void PoliceWarnings::_say(const QString& sentence)
{
    emit spoke(sentence);
    AudioOutput::instance()->say(sentence);
}
