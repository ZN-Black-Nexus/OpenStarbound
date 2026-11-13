#include "StarAndroidSetup.hpp"
#include "StarFile.hpp"
#include "StarLogging.hpp"
#include "StarJson.hpp"
#include "StarJsonBuilder.hpp"
#include "StarException.hpp"

#include "SDL3/SDL.h"

#include <stdlib.h>

namespace Star {

namespace {

Maybe<ByteArray> loadBundledFile(String const& name) {
  size_t size = 0;
  void* data = SDL_LoadFile(name.utf8Ptr(), &size);
  if (!data)
    return {};
  ByteArray result((char const*)data, size);
  SDL_free(data);
  return result;
}

// opensb.list holds one relative path per line, opensb.stamp identifies the build
void extractBundledAssets(String const& dataDir) {
  String assetsDir = File::relativeTo(dataDir, "assets");
  File::makeDirectoryRecursive(assetsDir);

  auto stamp = loadBundledFile("opensb.stamp");
  auto list = loadBundledFile("opensb.list");
  if (!stamp || !list) {
    Logger::warn("Android: APK does not contain bundled opensb assets");
    return;
  }

  String stampString(stamp->ptr(), stamp->size());
  String stampFile = File::relativeTo(assetsDir, ".opensb-stamp");
  String opensbDir = File::relativeTo(assetsDir, "opensb");
  if (File::isFile(stampFile) && File::isDirectory(opensbDir) && File::readFileString(stampFile) == stampString)
    return;

  Logger::info("Android: extracting bundled assets to '{}'", opensbDir);
  if (File::isDirectory(opensbDir))
    File::removeDirectoryRecursive(opensbDir);

  String listString(list->ptr(), list->size());
  for (auto line : listString.split('\n')) {
    line = line.trim();
    if (line.empty())
      continue;
    auto contents = loadBundledFile(strf("opensb/{}", line));
    if (!contents)
      throw StarException(strf("Android: bundled asset 'opensb/{}' missing from APK", line));
    String target = File::relativeTo(opensbDir, line);
    File::makeDirectoryRecursive(File::dirName(target));
    File::writeFile(*contents, target);
  }

  File::writeFile(stampString, stampFile);
}

// The launcher records the chosen volume (phone storage or SD card) in
// <internal files dir>/datadir.txt; falls back to the default external dir.
String chooseDataDirectory() {
  char const* internal = SDL_GetAndroidInternalStoragePath();
  if (internal) {
    String choiceFile = File::relativeTo(internal, "datadir.txt");
    if (File::isFile(choiceFile)) {
      String chosen = File::readFileString(choiceFile).trim();
      if (!chosen.empty()) {
        try {
          File::makeDirectoryRecursive(chosen);
          if (File::isDirectory(chosen))
            return chosen;
        } catch (StarException const& e) {
          Logger::warn("Android: chosen data directory '{}' unusable: {}", chosen, e.what());
        }
      }
    }
  }
  char const* path = SDL_GetAndroidExternalStoragePath();
  if (!path)
    path = internal;
  if (!path)
    throw StarException("Android: no writable storage path available");
  return String(path);
}

}

String prepareAndroidDataDirectory() {
  String dataDir = chooseDataDirectory();
  File::makeDirectoryRecursive(dataDir);
  File::changeDirectory(dataDir);

  for (auto sub : {"assets", "mods", "storage", "logs", "tmp"})
    File::makeDirectoryRecursive(File::relativeTo(dataDir, sub));

  // Temp files are not unlinked while open on Android, so clear out the last run's.
  String tmpDir = File::relativeTo(dataDir, "tmp");
  try {
    for (auto const& entry : File::dirList(tmpDir)) {
      if (!entry.second)
        File::remove(File::relativeTo(tmpDir, entry.first));
    }
  } catch (StarException const& e) {
    Logger::warn("Android: could not clean temp directory: {}", e.what());
  }

  // bionic has no writable /tmp; File temp helpers read TMPDIR on Android.
  setenv("TMPDIR", File::relativeTo(dataDir, "tmp").utf8Ptr(), 1);

  extractBundledAssets(dataDir);

  // Rewritten every launch so the paths always match the current data dir.
  Json bootConfig = JsonObject{
    {"assetDirectories", JsonArray{"assets/", "mods/"}},
    {"storageDirectory", "storage/"},
    {"logDirectory", "logs/"},
    {"defaultConfiguration", JsonObject{
      {"allowAdminCommandsFromAnyone", true},
      {"anonymousConnectionsAreAdmin", true},
      {"hdr", false},
      {"hardwareCursor", false},
      {"fullscreen", true},
      {"maximized", false},
      {"limitTextureAtlasSize", true},
      {"vsync", true}
    }}
  };
  File::writeFile(bootConfig.repr(2, true), File::relativeTo(dataDir, "sbinit.config"));

  return dataDir;
}

}
