#include "PinGate.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QRandomGenerator>
#include <QtCore/QSettings>

#include <algorithm>

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(PinGateLog, "PoliceDrone.PinGate")

namespace {

constexpr const char *kHashKey = "pinHash";
constexpr const char *kSaltKey = "pinSalt";

constexpr int kMinPinDigits = 4;
constexpr int kMaxPinDigits = 8;
constexpr int kLockoutSeconds = 60;

QString hashOf(const QString &pin, const QString &salt)
{
    return QString::fromLatin1(
        QCryptographicHash::hash((salt + pin).toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool wellFormed(const QString &pin)
{
    if ((pin.size() < kMinPinDigits) || (pin.size() > kMaxPinDigits)) {
        return false;
    }
    return std::all_of(pin.cbegin(), pin.cend(), [](QChar c) { return c.isDigit(); });
}

}  // namespace

PinGate::PinGate(const QString &settingsGroup, const QString &defaultPin, QObject *parent)
    : QObject(parent), _settingsGroup(settingsGroup), _defaultPin(defaultPin)
{
    _lockoutTick.setInterval(1000);
    (void) connect(&_lockoutTick, &QTimer::timeout, this, [this]() {
        emit lockoutChanged();
        if (!lockedOut()) {
            _lockoutTick.stop();
        }
    });
}

PinGate::~PinGate() = default;

bool PinGate::lockedOut() const
{
    return _lockoutUntil.isValid() && (QDateTime::currentDateTime() < _lockoutUntil);
}

int PinGate::lockoutSecondsLeft() const
{
    if (!lockedOut()) {
        return 0;
    }
    return static_cast<int>(QDateTime::currentDateTime().secsTo(_lockoutUntil)) + 1;
}

bool PinGate::pinIsDefault() const
{
    QSettings settings;
    settings.beginGroup(_settingsGroup);
    return settings.value(QLatin1String(kHashKey)).toString().isEmpty();
}

bool PinGate::changePin(const QString &current, const QString &next)
{
    if (lockedOut() || !_matches(current)) {
        return false;
    }
    if (!wellFormed(next) || (next == _defaultPin)) {
        return false;
    }
    _store(next);
    emit pinChanged();
    return true;
}

bool PinGate::_verify(const QString &pin)
{
    if (lockedOut()) {
        return false;
    }

    if (_matches(pin)) {
        _failures = 0;
        return true;
    }

    ++_failures;
    if (_failures >= kMaxFailures) {
        _failures = 0;
        _lockoutUntil = QDateTime::currentDateTime().addSecs(kLockoutSeconds);
        _lockoutTick.start();
        qCWarning(PinGateLog) << _settingsGroup << "- too many wrong PINs, locked for" << kLockoutSeconds << "s";
        emit lockoutChanged();
    }
    return false;
}

bool PinGate::_matches(const QString &pin) const
{
    QSettings settings;
    settings.beginGroup(_settingsGroup);
    const QString storedHash = settings.value(QLatin1String(kHashKey)).toString();
    if (storedHash.isEmpty()) {
        return pin == _defaultPin;
    }
    const QString salt = settings.value(QLatin1String(kSaltKey)).toString();
    return hashOf(pin, salt) == storedHash;
}

void PinGate::_store(const QString &pin)
{
    QByteArray saltBytes(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(saltBytes.data()),
                                          saltBytes.size() / static_cast<int>(sizeof(quint32)));
    const QString salt = QString::fromLatin1(saltBytes.toHex());

    QSettings settings;
    settings.beginGroup(_settingsGroup);
    settings.setValue(QLatin1String(kSaltKey), salt);
    settings.setValue(QLatin1String(kHashKey), hashOf(pin, salt));
}
