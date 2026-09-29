#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtQmlIntegration/QtQmlIntegration>

Q_DECLARE_LOGGING_CATEGORY(PinGateLog)

/// \brief A numeric PIN kept as a salted hash in the settings, behind a lockout.
///
/// Kept apart from DeveloperPin so that what a PIN opens stays separate from how it is kept. Until
/// a PIN has been set the one the unit shipped with answers, and pinIsDefault says so, for the
/// screen to keep saying until it has been replaced.
class PinGate : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    /// True while wrong guesses have shut the prompt; lockoutSecondsLeft counts it down.
    Q_PROPERTY(bool lockedOut READ lockedOut NOTIFY lockoutChanged)
    Q_PROPERTY(int lockoutSecondsLeft READ lockoutSecondsLeft NOTIFY lockoutChanged)
    /// True until the shipped default PIN has been replaced.
    Q_PROPERTY(bool pinIsDefault READ pinIsDefault NOTIFY pinChanged)

public:
    static constexpr int kMaxFailures = 5;

    /// \param settingsGroup where the hash and its salt live, one group per gate
    /// \param defaultPin what answers until a PIN has been set; refused as a new PIN
    PinGate(const QString &settingsGroup, const QString &defaultPin, QObject *parent);
    ~PinGate() override;

    [[nodiscard]] bool lockedOut() const;
    [[nodiscard]] int lockoutSecondsLeft() const;
    [[nodiscard]] bool pinIsDefault() const;

    /// Replaces the PIN. The current one has to be right and the new one four to eight digits,
    /// which is what a gloved hand on a tablet can be expected to enter. The shipped default is
    /// refused, since keeping it is what pinIsDefault exists to end.
    Q_INVOKABLE virtual bool changePin(const QString &current, const QString &next);

signals:
    void lockoutChanged();
    void pinChanged();

protected:
    /// True on a match. A wrong guess counts toward the lockout; a guess during the lockout is
    /// refused without counting, because it is the same guess still.
    [[nodiscard]] bool _verify(const QString &pin);

private:
    [[nodiscard]] bool _matches(const QString &pin) const;
    void _store(const QString &pin);

    const QString _settingsGroup;
    const QString _defaultPin;
    int _failures = 0;
    QDateTime _lockoutUntil;
    /// Ticks once a second while locked out, so the countdown on screen moves.
    QTimer _lockoutTick;
};
