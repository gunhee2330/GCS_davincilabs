#pragma once

#include "PinGate.h"

class QQmlEngine;
class QJSEngine;

/// \brief The PIN behind developer mode (raw parameter editing and the analysis screens) and the
/// recordings page's administrator mode.
///
/// It was a literal in both pages' QML, in a repository that has since gone public. Now it is a
/// salted hash in the settings, replaceable from the developer page, and the value a unit ships with is set when the package is built (QGC_DEVELOPER_PIN) rather than
/// written in the source. What a correct PIN opens stays with the page: this class only answers
/// whether it was correct.
class DeveloperPin : public PinGate
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    /// No default argument, for the reason TakeoffCounter gives: a default-constructible
    /// QML_SINGLETON is default-constructed by the engine rather than through create().
    explicit DeveloperPin(QObject *parent);
    ~DeveloperPin() override;

    static DeveloperPin *instance();
    static DeveloperPin *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    /// True on a match; false otherwise, counting toward the lockout.
    Q_INVOKABLE bool verify(const QString &pin);
};
