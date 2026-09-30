#pragma once

/// \brief Draws the police interface on a Mac at the handset's proportions.
///
/// The UniRC 7 Pro lays the interface out in 768x480 logical pixels (its 1920x1200 panel at
/// Android's device pixel ratio of 2.5) with a 12 pt base font. A 13 inch MacBook has the same
/// 16:10 shape but about 1440 points across, so the same layout would come out at about half its
/// relative size. Scaling Qt by the display's width over 768 gives the Mac the handset's logical
/// width: every item keeps its place and only grows. ScreenTools gives macOS the handset's base
/// font to match.
namespace PoliceDesktopScale {

/// The logical width the handset lays the interface out in.
inline constexpr int kHandsetLogicalWidth = 768;

/// Sets QT_SCALE_FACTOR from the main display, unless the environment already sets it. A no-op
/// off macOS. Call before the QGuiApplication exists.
void apply();

}  // namespace PoliceDesktopScale
