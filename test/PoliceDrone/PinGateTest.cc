#include "PinGateTest.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtTest/QSignalSpy>

#include "PinGate.h"

namespace {

/// The base with nothing on top, so what is tested is the keeping of a PIN and not a screen.
class TestGate : public PinGate
{
public:
    explicit TestGate(const QString &group, const QString &defaultPin)
        : PinGate(group, defaultPin, nullptr) {}

    bool verify(const QString &pin) { return _verify(pin); }
};

}  // namespace

void PinGateTest::_defaultAnswersUntilChanged_test()
{
    TestGate gate(QStringLiteral("PinGateTest/A"), QStringLiteral("000000"));

    QVERIFY(gate.pinIsDefault());
    QVERIFY(gate.verify(QStringLiteral("000000")));
    QVERIFY(!gate.verify(QStringLiteral("123456")));

    QSignalSpy changed(&gate, &PinGate::pinChanged);
    QVERIFY(gate.changePin(QStringLiteral("000000"), QStringLiteral("2468")));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!gate.pinIsDefault());

    QVERIFY(gate.verify(QStringLiteral("2468")));
    QVERIFY(!gate.verify(QStringLiteral("000000")));

    // The store is the settings, not the object: a fresh gate over the same group reads it back.
    TestGate again(QStringLiteral("PinGateTest/A"), QStringLiteral("000000"));
    QVERIFY(!again.pinIsDefault());
    QVERIFY(again.verify(QStringLiteral("2468")));

    // And it is a hash, not the PIN.
    QSettings settings;
    settings.beginGroup(QStringLiteral("PinGateTest/A"));
    const QString stored = settings.value(QStringLiteral("pinHash")).toString();
    QVERIFY(!stored.isEmpty());
    QVERIFY(!stored.contains(QStringLiteral("2468")));
}

void PinGateTest::_changeRequiresCurrentAndWellFormedNext_test()
{
    TestGate gate(QStringLiteral("PinGateTest/B"), QStringLiteral("000000"));

    QVERIFY(!gate.changePin(QStringLiteral("999999"), QStringLiteral("2468")));  // wrong current
    QVERIFY(!gate.changePin(QStringLiteral("000000"), QStringLiteral("123")));   // too short
    QVERIFY(!gate.changePin(QStringLiteral("000000"), QStringLiteral("123456789")));  // too long
    QVERIFY(!gate.changePin(QStringLiteral("000000"), QStringLiteral("12a4")));  // not digits
    QVERIFY(!gate.changePin(QStringLiteral("000000"), QStringLiteral("000000")));  // the default again
    QVERIFY(gate.pinIsDefault());

    QVERIFY(gate.changePin(QStringLiteral("000000"), QStringLiteral("12345678")));
    QVERIFY(gate.verify(QStringLiteral("12345678")));
}

void PinGateTest::_lockoutAfterRepeatedWrongGuesses_test()
{
    TestGate gate(QStringLiteral("PinGateTest/C"), QStringLiteral("000000"));
    QSignalSpy lockout(&gate, &PinGate::lockoutChanged);

    for (int i = 0; i < PinGate::kMaxFailures - 1; ++i) {
        QVERIFY(!gate.verify(QStringLiteral("1111")));
        QVERIFY(!gate.lockedOut());
    }
    QCOMPARE(lockout.count(), 0);

    expectLogMessage("PoliceDrone.PinGate", QtWarningMsg,
                     QRegularExpression(QStringLiteral("too many wrong PINs")));
    QVERIFY(!gate.verify(QStringLiteral("1111")));
    verifyExpectedLogMessage();
    QVERIFY(gate.lockedOut());
    QVERIFY(gate.lockoutSecondsLeft() > 0);
    QCOMPARE(lockout.count(), 1);

    // The right PIN does not open a locked gate, and a change is refused under it too.
    QVERIFY(!gate.verify(QStringLiteral("000000")));
    QVERIFY(!gate.changePin(QStringLiteral("000000"), QStringLiteral("2468")));
    QVERIFY(gate.pinIsDefault());
}

void PinGateTest::_gatesKeepSeparatePins_test()
{
    TestGate first(QStringLiteral("PinGateTest/D1"), QStringLiteral("000000"));
    TestGate second(QStringLiteral("PinGateTest/D2"), QStringLiteral("704183"));

    QVERIFY(first.changePin(QStringLiteral("000000"), QStringLiteral("1357")));
    QVERIFY(first.verify(QStringLiteral("1357")));
    QVERIFY(!second.verify(QStringLiteral("1357")));
    QVERIFY(second.verify(QStringLiteral("704183")));
    QVERIFY(second.pinIsDefault());
}

UT_REGISTER_TEST_LIGHTWEIGHT(PinGateTest, TestLabel::Unit)
