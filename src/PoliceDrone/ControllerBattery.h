#pragma once

#include <QtCore/QObject>
#include <QtQmlIntegration/QtQmlIntegration>

class QQmlEngine;
class QJSEngine;

/// \brief Charge level of the handheld the ground station is running on.
///
/// The aircraft's pack is on the bar because the aircraft sends it. The controller's is not
/// sent by anything - it is the Android device this application is running on, so it is read
/// from the platform. Anywhere else, and on any desktop, there is no such battery and the
/// value stays at -1, which is what keeps the platform test out of the QML.
class ControllerBattery : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int percent READ percent NOTIFY percentChanged)

public:
    /// No default argument, for the reason TakeoffCounter gives: a default-constructible
    /// QML_SINGLETON is default-constructed by the engine instead of going through create(),
    /// and QML then holds a second object that init() never started polling.
    explicit ControllerBattery(QObject* parent);
    ~ControllerBattery() override;

    static ControllerBattery* instance();
    static ControllerBattery* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    /// Starts the poll where there is a battery to poll. Call once from QGCApplication.
    void init();

    /// 0..100, or -1 when there is no reading. Never a fabricated zero.
    [[nodiscard]] int percent() const { return _percent; }

signals:
    void percentChanged();

private:
    void _refresh();

    int _percent = -1;
};
