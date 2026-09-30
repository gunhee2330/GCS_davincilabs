#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QVector>

Q_DECLARE_LOGGING_CATEGORY(PoliceRcKeysLog)

class Vehicle;

/// \brief The UniRC handset keys, read from the RC channels the flight controller relays.
///
/// The keys never reach Android as input events: the handset puts them on S.Bus channels and the
/// flight controller sends those back as RC_CHANNELS. A key's channel moving from one band to
/// another is a press (or a release, which repeats the same harmless selection). The first valid
/// reading after connecting, or after a channel setting changes, only records where the channel
/// sits, so a reconnect never takes the big picture off the operator's choice.
class PoliceRcKeys : public QObject
{
    Q_OBJECT

public:
    enum Key { Fpv, Zoom, Wide, Thermal, KeyCount };

    explicit PoliceRcKeys(QObject* parent = nullptr);

    static PoliceRcKeys* instance();

    /// Starts following the active vehicle. Call once, after MultiVehicleManager::init().
    void init();

    /// Band of a channel reading: 1 low, 2 middle, 3 high. A reading outside the valid window or
    /// in a dead band keeps \a last (0 = no reading yet).
    static int positionFor(int us, int last);
    /// The pod routing \a key asks for, or -1 to leave the main stream alone.
    static int imageTypeFor(Key key, bool aiEnabled);

public slots:
    /// Index 0 is CH1, as Vehicle::rcChannelsRawChanged sends it.
    void handleChannels(const QVector<int>& values);

private:
    void _follow(Vehicle* vehicle);
    void _resetBaselines();
    static void _press(Key key);

    QPointer<Vehicle> _vehicle;
    int _position[KeyCount] = {};
};
