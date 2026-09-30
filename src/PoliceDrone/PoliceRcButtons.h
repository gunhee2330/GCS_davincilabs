#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QVector>

Q_DECLARE_LOGGING_CATEGORY(PoliceRcButtonsLog)

class Vehicle;

/// \brief The handset buttons that act rather than pick the big picture: L1 centres the gimbal,
/// S1 is push-to-talk for the loudspeaker, and S2 takes a photo on a short press or starts and
/// stops recording when held for two seconds.
///
/// They are read from the handset itself through the SIYI remote-control service
/// (PoliceRcChannelMonitor), at once and with the drone off. Without that service they are read
/// the way PoliceRcKeys reads its keys, from the S.Bus channels the flight controller relays in
/// RC_CHANNELS, which is slower and needs the drone linked. Never from both: while the handset
/// stream is live, RC_CHANNELS is ignored.
///
/// The buttons must be momentary (UniGCS "초기화"), high only while held. The first reading after
/// starting, after the source changes, after a gap in readings or after a channel setting changes
/// is only a baseline: a press that began unseen does nothing, except that a talk the key was
/// holding when its readings were cut off goes on if the key is still held.
///
/// The talk and camera channels ship off (0). UniGCS maps every channel to some control, so
/// until S1 and S2 are moved onto one, any default would put a dial or a switch on the microphone.
class PoliceRcButtons : public QObject
{
    Q_OBJECT

public:
    enum Button { Center, Talk, Camera, ButtonCount };

    explicit PoliceRcButtons(QObject* parent = nullptr);

    static PoliceRcButtons* instance();

    /// Wires the buttons to the pod and the live voice, starts the handset stream and follows the
    /// active vehicle. Call once, after MultiVehicleManager::init().
    void init();

    /// Whether a reading means held: above the high band yes, below the low band no. A reading
    /// in the dead band or outside the valid window keeps \a last.
    static bool heldFor(int us, bool last);

    /// RC_CHANNELS from the flight controller. Index 0 is CH1. \a nowMsecs is any monotonic clock;
    /// tests drive it directly.
    void handleChannelsAt(const QVector<int>& values, qint64 nowMsecs);

    /// The handset's own channel outputs, sent only when one changes. Index 0 is CH1, -1 unreported.
    void handleHandsetChannelsAt(const QVector<int>& values, qint64 nowMsecs);
    void handleHandsetChannels(const QVector<int>& values);

    /// The handset stream stopped answering; RC_CHANNELS takes over.
    void handleHandsetLost();

    [[nodiscard]] bool handsetLive() const { return _handsetLive; }

    static constexpr qint64 kRecordHoldMsecs = 2000;
    /// RC_CHANNELS further apart than this mean the link dropped, and a release may have been lost.
    /// The flight controller streams it at its own rate, measured near 2 Hz on the 115200 link.
    static constexpr qint64 kStaleMsecs = 2500;

public slots:
    void handleChannels(const QVector<int>& values);

signals:
    void centerPressed();
    void talkPressed();
    void talkReleased();
    void photoRequested();
    void recordingToggleRequested();

private slots:
    void _voiceTalkingChanged();

private:
    void _follow(Vehicle* vehicle);
    void _process(const QVector<int>& values, qint64 nowMsecs);
    void _changed(Button button, bool held, qint64 nowMsecs);
    void _letGo(bool mayResume);
    void _resetBaselines();
    void _checkStale();
    void _refreshChannels();
    void _setTalkHeld(bool held);
    void _startHandsetStream();

    QPointer<Vehicle> _vehicle;
    int _channel[ButtonCount] = {};
    bool _known[ButtonCount] = {};
    bool _held[ButtonCount] = {};
    bool _pressSeen[ButtonCount] = {};
    bool _handsetLive = false;
    bool _resumeTalk = false;
    bool _talkHeldSent = false;
    qint64 _lastReadingMsecs = -1;
    qint64 _cameraPressedMsecs = 0;
    bool _recordingToggledThisPress = false;
    bool _talkingFromKey = false;
    QElapsedTimer _clock;
    QTimer _staleTimer;
    QTimer _recordTimer;
};
