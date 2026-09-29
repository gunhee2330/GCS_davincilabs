#include "PoliceSpeechTranslator.h"

#include <QtCore/QCoreApplication>

namespace {

struct SpokenString
{
    const char* context;
    const char* source;
};

/// Sentences QGC passes to AudioOutput and never puts on screen. Those it also shows (the link
/// switch messages) keep their Korean, since the screen outranks the voice.
constexpr SpokenString kSpokenOnly[] = {
    {"VehicleLinkManager", "%1Communication lost"},
    {"VehicleLinkManager", "%1Communication regained"},
    {"VehicleLinkManager", "%1Communication regained on %2 link"},
    {"VehicleLinkManager", "%1Communication lost on %2 link."},
    {"VehicleLinkManager", "primary"},
    {"VehicleLinkManager", "secondary"},
    {"Vehicle", "%1 %2 flight mode"},
    {"Vehicle", "armed"},
    {"Vehicle", "disarmed"},
    {"Vehicle", "warning"},
    {"Vehicle", "Vehicle %1 "},
    {"Vehicle", "battery %1 level low"},
    {"Vehicle", "battery %1 level is critical"},
    {"Vehicle", "battery %1 level emergency"},
    {"Vehicle", "battery %1 failed"},
    {"Vehicle", "battery %1 unhealthy"},
    {"Vehicle", "minimum altitude"},
    {"Vehicle", "maximum altitude"},
    {"Vehicle", "boundary"},
    {"Vehicle", "fence breached"},
    {"AudioOutput", "Audio test. Volume is %1 percent"},
};

} // namespace

PoliceSpeechTranslator::PoliceSpeechTranslator(QObject* parent)
    : QTranslator(parent)
{
}

QString PoliceSpeechTranslator::translate(const char* context, const char* sourceText,
                                          const char* disambiguation, int n) const
{
    (void) disambiguation;
    (void) n;

    if (!context || !sourceText) {
        return QString();
    }
    for (const SpokenString& spoken : kSpokenOnly) {
        if ((qstrcmp(context, spoken.context) == 0) && (qstrcmp(sourceText, spoken.source) == 0)) {
            return QString::fromUtf8(sourceText);
        }
    }
    return QString();
}

void PoliceSpeechTranslator::install()
{
    static PoliceSpeechTranslator* translator = nullptr;
    if (!translator) {
        translator = new PoliceSpeechTranslator(QCoreApplication::instance());
    }
    (void) QCoreApplication::removeTranslator(translator);
    (void) QCoreApplication::installTranslator(translator);
}
