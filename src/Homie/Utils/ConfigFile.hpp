#pragma once

#include <stddef.h>
#include <stdint.h>

namespace HomieInternals {
namespace ConfigFile {
enum class LoadResult { INVALID, PRIMARY, RECOVERED, RECOVERY_FAILED };

// Keep the previous file until a complete replacement has been read back.
template <typename Filesystem>
bool recover(Filesystem& fs, const char* path, const char* backup) {
  return fs.exists(path) || (fs.exists(backup) && fs.rename(backup, path));
}

template <typename Filesystem, typename Validate>
LoadResult load(Filesystem& fs, const char* path, const char* backup, Validate validate) {
  if (validate(path)) return LoadResult::PRIMARY;
  if (!validate(backup)) return LoadResult::INVALID;
  if (fs.exists(path) && !fs.remove(path)) return LoadResult::RECOVERY_FAILED;
  return fs.rename(backup, path) ? LoadResult::RECOVERED : LoadResult::RECOVERY_FAILED;
}

template <typename Filesystem>
bool erase(Filesystem& fs, const char* path, const char* temporary,
           const char* backup, const char* nextMode) {
  // Delete the primary last: a failed backup removal must not resurrect it.
  const char* paths[] = {temporary, backup, nextMode, path};
  for (const char* entry : paths) {
    if (fs.exists(entry) && (!fs.remove(entry) || fs.exists(entry))) return false;
  }
  return true;
}

template <typename Filesystem>
bool write(Filesystem& fs, const char* path, const char* temporary,
           const char* backup, const char* data, size_t size) {
  if (!fs.exists(path) && fs.exists(backup) && !fs.rename(backup, path)) return false;
  if (fs.exists(temporary) && !fs.remove(temporary)) return false;
  auto file = fs.open(temporary, "w");
  if (!file) return false;
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(data), size);
  file.flush();
  file.close();
  if (written != size) return false;

  file = fs.open(temporary, "r");
  if (!file || file.size() != size) return false;
  bool matches = true;
  for (size_t i = 0; i < size; ++i) {
    if (file.read() != static_cast<uint8_t>(data[i])) { matches = false; break; }
  }
  file.close();
  if (!matches) return false;

  if (fs.exists(path)) {
    if (fs.exists(backup) && !fs.remove(backup)) return false;
    if (!fs.rename(path, backup)) return false;
  }
  if (fs.rename(temporary, path)) return true;
  recover(fs, path, backup);
  return false;
}
}  // namespace ConfigFile
}  // namespace HomieInternals
