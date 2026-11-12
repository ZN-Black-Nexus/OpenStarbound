#pragma once

#include "StarString.hpp"

namespace Star {

// Android first-run / per-launch preparation. The data directory is the app's
// external files dir (Android/data/<package>/files), so the user can reach it
// from a file manager; the Java ImportActivity also copies files into it.
//   <data>/assets/packed.pak     the user's own Starbound assets
//   <data>/assets/opensb/        OpenStarbound base assets, extracted from the APK
//   <data>/mods/                 .pak files and directories
//   <data>/storage/, <data>/logs/
// Extracts bundled assets when the build changes, writes sbinit.config, and
// changes the working directory to the data directory.
String prepareAndroidDataDirectory();

}
