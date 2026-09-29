#pragma once

#include <QtCore/QTranslator>

/// \brief Keeps QGC's spoken announcements in English under the Korean interface.
///
/// The handset's text-to-speech engine carries no Korean voice, so the Korean translations of
/// QGC's announcements ("%1Communication lost" as 통신이 끊겼습니다) were read out by its English
/// voice as broken Korean. Installed after QGC's own translators, this one is asked first and
/// hands back, untranslated, only the sentences QGC never shows and only speaks. Every other
/// string falls through to the Korean catalogue, so what is on screen stays Korean.
class PoliceSpeechTranslator : public QTranslator
{
    Q_OBJECT

public:
    explicit PoliceSpeechTranslator(QObject* parent = nullptr);

    QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr,
                      int n = -1) const override;
    bool isEmpty() const override { return false; }

    /// Installs the one instance on the application. Call after QGCApplication::setLanguage():
    /// the translator installed last is asked first. A language change in the settings reinstalls
    /// QGC's translators ahead of this one until the next start, which that setting asks for.
    static void install();
};
