#include "StandardModes.h"
#include "Vehicle.h"
#include "QGCLoggingCategory.h"

#include <algorithm>

QGC_LOGGING_CATEGORY(StandardModesLog, "Vehicle.StandardModes")

static void requestMessageResultHandler(void *resultHandlerData, MAV_RESULT result,
                                        [[maybe_unused]] Vehicle::RequestMessageResultHandlerFailureCode_t failureCode,
                                        const mavlink_message_t &message)
{
    StandardModes* standardModes = static_cast<StandardModes*>(resultHandlerData);
    standardModes->gotMessage(result, message);
}

StandardModes::StandardModes(QObject *parent, Vehicle *vehicle)
        : QObject(parent), _vehicle(vehicle)
{
}

void StandardModes::gotMessage(MAV_RESULT result, const mavlink_message_t &message)
{
    _requestActive = false;
    if (_wantReset) {
        _wantReset = false;
        request();
        return;
    }

    if (result == MAV_RESULT_ACCEPTED) {
        mavlink_available_modes_t availableModes;
        mavlink_msg_available_modes_decode(&message, &availableModes);
        bool cannotBeSet = availableModes.properties & MAV_MODE_PROPERTY_NOT_USER_SELECTABLE;
        bool advanced = availableModes.properties & MAV_MODE_PROPERTY_ADVANCED;
        availableModes.mode_name[sizeof(availableModes.mode_name)-1] = '\0';
        QString name = availableModes.mode_name;
        switch (availableModes.standard_mode) {
            case MAV_STANDARD_MODE_POSITION_HOLD:
                name = "Position";
                break;
            case MAV_STANDARD_MODE_ORBIT:
                name = "Orbit";
                cannotBeSet = true; // These are exposed in the UI as separate buttons
                break;
            case MAV_STANDARD_MODE_CRUISE:
                name = "Cruise";
                break;
            case MAV_STANDARD_MODE_ALTITUDE_HOLD:
                name = "Altitude";
                break;
            case MAV_STANDARD_MODE_SAFE_RECOVERY:
                name = "Safe Recovery";
                break;
            case MAV_STANDARD_MODE_MISSION:
                name = "Mission";
                break;
            case MAV_STANDARD_MODE_LAND:
                name = "Land";
                break;
            case MAV_STANDARD_MODE_TAKEOFF:
                name = "Takeoff";
                break;
        }

        qCDebug(StandardModesLog) << "Available mode received - name:" << name <<
            "index:" << availableModes.mode_index <<
            "standard_mode:" << availableModes.standard_mode <<
            "advanced:" << advanced <<
            "cannotBeSet:" << cannotBeSet <<
            "custom_mode:" << availableModes.custom_mode;

        const FirmwareFlightMode mode{
            name,
            availableModes.standard_mode,
            availableModes.custom_mode,
            !cannotBeSet,
            advanced,
            true,  // fixed wing - Since we don't know at this point we assume fixed wing support
            true   // multi-rotor - Since we don't know at this point we assume multi-rotor support as well
        };

        // Replaced rather than appended when this mode is already here. One request answered
        // twice - a retransmit, or a walk restarted while the first one's replies were still
        // arriving - otherwise leaves the same mode in the list twice, and ensureUniqueModeNames
        // below then tells the two apart by renaming the second: a vehicle with exactly one
        // Stabilized reported "Stabilized (1)" on the flight bar. custom_mode is what identifies
        // a mode to the rest of QGC - it is the key the name lookup is built on - so it is what
        // sameness is judged by here.
        const auto existing = std::find_if(_modeList.begin(), _modeList.end(),
                                           [&mode](const FirmwareFlightMode &candidate) {
                                               return candidate.custom_mode == mode.custom_mode;
                                           });
        if (existing == _modeList.end()) {
            _modeList += mode;
        } else {
            *existing = mode;
        }

        if (availableModes.mode_index >= availableModes.number_modes) { // We are done
            // Short means the walk was restarted part-way through: request() clears what has
            // arrived so far, the replies already in flight keep landing in the cleared list,
            // and the last of them ends the walk on a list missing everything before the
            // restart. Committing that drops those modes from the name lookup, which is how a
            // plain Stabilized came to read as "Unknown 81:458752" on the flight bar. Walk it
            // again rather than commit a hole - once, so a vehicle that really does answer
            // short still gets its list rather than an endless re-request.
            if ((_modeList.size() < availableModes.number_modes) && !_retriedShortList) {
                qCDebug(StandardModesLog) << "Short mode list" << _modeList.size() << "of"
                                          << availableModes.number_modes << "- re-requesting";
                _retriedShortList = true;
                request();
                return;
            }
            qCDebug(StandardModesLog) << "Completed, num modes:" << availableModes.number_modes;
            _retriedShortList = false;
            ensureUniqueModeNames();
            _vehicle->firmwarePlugin()->updateAvailableFlightModes(_modeList);
            emit modesUpdated();
            emit requestCompleted();
        } else {
            requestMode(availableModes.mode_index + 1);
        }
    } else {
        qCDebug(StandardModesLog) << "Failed to retrieve available modes - REQUEST_MESSAGE:MAV_RESULT" << result;
        emit requestCompleted();
    }
}

void StandardModes::ensureUniqueModeNames()
{
    // Ensure mode names are unique. This should generally already be the case, but e.g. during development when
    // restarting dynamic modes, it might not be.
    for (auto iter = _modeList.begin(); iter != _modeList.end(); ++iter) {
        int duplicateIdx = 0;
        for (auto iter2 = std::next(iter); iter2 != _modeList.end(); ++iter2) {
            if ((*iter).mode_name == (*iter2).mode_name) {
                (*iter2).mode_name += QStringLiteral(" (%1)").arg(duplicateIdx + 1);
                ++duplicateIdx;
            }
        }
    }
}

void StandardModes::request()
{
    if (_requestActive) {
        // If we are in the middle of waiting for a request, wait for the response first
        _wantReset = true;
        return;
    }

    qCDebug(StandardModesLog) << "Requesting available modes";
    // Request one at a time. This could be improved by requesting all, but we can't use Vehicle::requestMessage for that
    _modeList.clear();
    StandardModes::requestMode(1);
}

void StandardModes::requestMode(int modeIndex)
{
    _requestActive = true;
    _vehicle->requestMessage(
            requestMessageResultHandler,
            this,
            MAV_COMP_ID_AUTOPILOT1,
            MAVLINK_MSG_ID_AVAILABLE_MODES, modeIndex);
}

void StandardModes::availableModesMonitorReceived(uint8_t seq)
{
    if (_lastSeq == -1) {
        // The first monitor of this connection says nothing has changed - there is nothing yet
        // to have changed from. The initial request is already under way by now, and treating
        // this as a change restarts that walk from the beginning part-way through it, which is
        // exactly the truncation guarded against above. Seeded rather than compared, so the
        // next monitor is the first one that can mean anything.
        _lastSeq = seq;
        return;
    }

    if (_lastSeq != seq) {
        qCDebug(StandardModesLog) << "Available modes changed, re-requesting";
        _lastSeq = seq;
        request();
    }
}
