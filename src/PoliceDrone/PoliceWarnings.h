#pragma once

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QtNumeric>
#include <QtQmlIntegration/QtQmlIntegration>

class Vehicle;

/// \brief The four warnings RFP p6 아 and p8 ask for beside the lost link: low battery, altitude
/// over its limit, distance from home over its limit, and wind the aircraft cannot hold against.
///
/// The dashboard binds what it already shows into the inputs, and this reads only the limits off
/// the vehicle's parameters. The battery input is the top bar's own verdict rather than a second
/// reading of the packs, so the strip goes up at the moment the bar changes colour and never on a
/// different plan.
///
/// Each warning speaks once, on the way up, through QGC's AudioOutput (the same TTS and mute
/// setting the lost link speaks through). A small hysteresis on the way down keeps a value sitting
/// on its threshold from putting the strip up and down and repeating the sentence.
///
/// A tap on a strip dismisses that warning alone. It stays hidden until its condition clears and
/// comes back, or, for the battery, until low turns critical, which shows and speaks again.
///
/// The battery sentence replaces what the app said for the same event before: while one of these
/// watches a vehicle, Vehicle leaves unspoken its own low and critical announcement off
/// BATTERY_STATUS and the autopilot's English low and critical battery text (voicesBattery and
/// isBatteryAnnouncement, called from Vehicle.cc). The text still reaches the message list.
class PoliceWarnings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("Vehicle.h")

    /// Whose parameters the limits are read from.
    Q_PROPERTY(Vehicle* vehicle READ vehicle WRITE setVehicle NOTIFY vehicleChanged)

    /// The top bar's battery state: 0 nothing, 1 low (its orange), 2 critical (its red).
    Q_PROPERTY(int    batteryLevel   MEMBER _batteryInput   WRITE setBatteryLevel)
    /// The percentage the top bar shows, NaN when nothing reports one.
    Q_PROPERTY(double batteryPercent MEMBER _batteryPercent WRITE setBatteryPercent)
    Q_PROPERTY(bool   flying         MEMBER _flying         WRITE setFlying)
    /// Metres above home.
    Q_PROPERTY(double altitude       MEMBER _altitude       WRITE setAltitude)
    /// Horizontal metres from home.
    Q_PROPERTY(double homeDistance   MEMBER _homeDistance   WRITE setHomeDistance)
    /// Metres per second.
    Q_PROPERTY(double windSpeed      MEMBER _windSpeed      WRITE setWindSpeed)

    /// Each strip's detail, empty while that warning is down or dismissed.
    Q_PROPERTY(QString batteryText  READ batteryText  NOTIFY textsChanged)
    Q_PROPERTY(QString altitudeText READ altitudeText NOTIFY textsChanged)
    Q_PROPERTY(QString radiusText   READ radiusText   NOTIFY textsChanged)
    Q_PROPERTY(QString windText     READ windText     NOTIFY textsChanged)
    /// The battery warning is at its critical step (the strip's title says so).
    Q_PROPERTY(bool batteryCritical READ batteryCritical NOTIFY textsChanged)

public:
    /// The strips' fixed top-to-bottom order.
    enum Warning { Battery, Altitude, Radius, Wind, WarningCount };
    Q_ENUM(Warning)

    explicit PoliceWarnings(QObject* parent = nullptr);
    ~PoliceWarnings() override;

    /// A PoliceWarnings watches \a vehicle, so its Korean battery sentence is the one heard.
    [[nodiscard]] static bool voicesBattery(const Vehicle* vehicle);
    /// \a text is an autopilot's English low or critical battery announcement: ArduPilot's
    /// "Battery N is low|critical ..." and the "Battery Failsafe" it sends straight after, PX4's
    /// two failsafe warnings.
    [[nodiscard]] static bool isBatteryAnnouncement(const QString& text);

    [[nodiscard]] Vehicle* vehicle() const { return _vehicle; }
    void setVehicle(Vehicle* vehicle);

    void setBatteryLevel(int level)          { _set(_batteryInput, level); }
    void setBatteryPercent(double percent)   { _set(_batteryPercent, percent); }
    void setFlying(bool flying)              { _set(_flying, flying); }
    void setAltitude(double metres)          { _set(_altitude, metres); }
    void setHomeDistance(double metres)      { _set(_homeDistance, metres); }
    void setWindSpeed(double metresPerSecond) { _set(_windSpeed, metresPerSecond); }

    [[nodiscard]] QString batteryText() const  { return _shown(Battery); }
    [[nodiscard]] QString altitudeText() const { return _shown(Altitude); }
    [[nodiscard]] QString radiusText() const   { return _shown(Radius); }
    [[nodiscard]] QString windText() const     { return _shown(Wind); }
    [[nodiscard]] bool batteryCritical() const { return _batteryLevel == 2; }

    /// Hides \a warning's strip while its condition lasts; the others are untouched.
    Q_INVOKABLE void dismiss(Warning warning);

    /// PX4 GF_MAX_VER_DIST when set; ArduPilot FENCE_ALT_MAX when the fence is on with its
    /// altitude bit; otherwise the 150 m the Korean rules set as the ceiling.
    [[nodiscard]] double altitudeLimit() const;
    /// PX4 GF_MAX_HOR_DIST when set; ArduPilot FENCE_RADIUS when the fence is on with its circle
    /// bit; otherwise NaN, and no radius warning at all.
    [[nodiscard]] double radiusLimit() const;
    /// PX4 COM_WIND_WARN when set, otherwise 10 m/s.
    [[nodiscard]] double windLimit() const;

    static constexpr double kDefaultCeilingM          = 150.0;
    static constexpr double kDefaultWindWarnMps       = 10.0;
    static constexpr double kBatteryHysteresisPercent = 2.0;
    static constexpr double kDistanceHysteresisM      = 2.0;
    static constexpr double kWindHysteresisMps        = 1.0;

signals:
    void vehicleChanged();
    void textsChanged();
    /// Every sentence as it is handed to AudioOutput.
    void spoke(const QString& sentence);

private slots:
    void _evaluate();
    void _watchLimits();

private:
    template <typename T>
    void _set(T& member, T value)
    {
        if (member != value) {
            member = value;
            _evaluate();
        }
    }

    /// The parameter's value, NaN when the vehicle has no such parameter or none loaded yet.
    [[nodiscard]] double _param(const char* name) const;
    /// ArduPilot's fence is enabled and FENCE_TYPE carries \a bit.
    [[nodiscard]] bool _fenceHas(int bit) const;
    [[nodiscard]] QString _shown(Warning warning) const { return _dismissed[warning] ? QString() : _text[warning]; }
    void _say(const QString& sentence);

    /// Every live instance, for voicesBattery.
    static inline QList<const PoliceWarnings*> s_instances;

    QPointer<Vehicle> _vehicle;

    int    _batteryInput   = 0;
    double _batteryPercent = qQNaN();
    bool   _flying         = false;
    double _altitude       = qQNaN();
    double _homeDistance   = qQNaN();
    double _windSpeed      = qQNaN();

    int    _batteryLevel        = 0;
    /// The percentage when the level last went up; it steps down only kBatteryHysteresisPercent
    /// above that.
    double _batteryEntryPercent = qQNaN();
    bool   _altitudeOver        = false;
    bool   _radiusOver          = false;
    bool   _windHigh            = false;

    /// What each warning says while its condition lasts, dismissed or not; empty while down.
    QString _text[WarningCount];
    bool    _dismissed[WarningCount] = {};
};
