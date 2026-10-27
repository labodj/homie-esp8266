#include <cassert>
#include <map>
#include <string>
#include <iostream>
#include "../../src/Homie/Utils/ConfigFile.hpp"

struct PowerLoss {};
struct Filesystem {
  std::map<std::string, std::string> files;
  bool shortWrite = false, corruptWrite = false, failOpen = false;
  std::string failRemove;
  int renameCount = 0, failRename = 0, mutationCount = 0, cutAt = 0;
  void changed() { if (++mutationCount == cutAt) throw PowerLoss{}; }
  struct File {
    Filesystem* fs = nullptr;
    std::string path;
    size_t offset = 0;
    explicit operator bool() const { return fs != nullptr; }
    size_t size() { return fs->files.at(path).size(); }
    size_t write(const uint8_t* data, size_t count) {
      if (fs->shortWrite) --count;
      fs->files[path].assign(reinterpret_cast<const char*>(data), count);
      if (fs->corruptWrite) fs->files[path][0] = '!';
      fs->changed();
      return count;
    }
    void flush() { fs->changed(); }
    void close() {}
    int read() { const auto& s = fs->files.at(path); return offset < s.size() ? static_cast<uint8_t>(s[offset++]) : -1; }
  };
  bool exists(const char* path) const { return files.count(path); }
  File open(const char* path, const char* mode) {
    if (failOpen) return {};
    if (mode[0] == 'w') { files[path] = ""; changed(); }
    else if (!exists(path)) return {};
    File result; result.fs = this; result.path = path; return result;
  }
  bool remove(const char* path) {
    if (failRemove == path) return false;
    files.erase(path); changed(); return true;
  }
  bool rename(const char* from, const char* to) {
    if (++renameCount == failRename || !exists(from) || exists(to)) return false;
    files[to] = files[from]; files.erase(from); changed(); return true;
  }
};

bool save(Filesystem& fs) {
  return HomieInternals::ConfigFile::write(fs, "config", "tmp", "backup", "new", 3);
}
void recover(Filesystem& fs) {
  fs.cutAt = 0;
  HomieInternals::ConfigFile::recover(fs, "config", "backup");
}
int main() {
  using HomieInternals::ConfigFile::LoadResult;
  for (int fault = 0; fault < 3; ++fault) {
    Filesystem fs; fs.files["config"] = "corrupt"; fs.files["backup"] = fault == 1 ? "corrupt" : "old";
    fs.failRename = fault == 2 ? 1 : 0;
    auto validate = [&fs](const char* path) { return fs.exists(path) && fs.files.at(path) == "old"; };
    const auto loaded = HomieInternals::ConfigFile::load(fs, "config", "backup", validate);
    assert(loaded == (fault == 0 ? LoadResult::RECOVERED : fault == 2 ? LoadResult::RECOVERY_FAILED : LoadResult::INVALID));
    if (loaded == LoadResult::RECOVERED) assert(fs.files.at("config") == "old");
    if (fault == 2) assert(HomieInternals::ConfigFile::load(fs, "config", "backup", validate) == LoadResult::RECOVERED);
  }
  for (bool missing : {false, true}) {
    Filesystem fs;
    if (!missing) fs.files["config"] = "device_id=j4";
    fs.files["backup"] = "device_id=j4";
    auto validate = [&fs](const char* path) { return fs.exists(path) && fs.files.at(path) == "device_id=j4"; };
    assert(HomieInternals::ConfigFile::load(fs, "config", "backup", validate) ==
           (missing ? LoadResult::RECOVERED : LoadResult::PRIMARY));
    assert(fs.files.at("config") == "device_id=j4");
  }
  for (const char* failed : {"tmp", "backup", "next", "config", ""}) {
    Filesystem fs;
    fs.files = {{"config", "device_id=j4"}, {"backup", "device_id=j4"}, {"tmp", "partial"}, {"next", "#2"}};
    fs.failRemove = failed;
    assert(HomieInternals::ConfigFile::erase(fs, "config", "tmp", "backup", "next") == (fs.failRemove.empty()));
    if (!fs.failRemove.empty()) assert(fs.files.at("config") == "device_id=j4");
    else assert(fs.files.empty());
  }
  {
    Filesystem fs; fs.files["config"] = "old";
    assert(save(fs)); assert(fs.files.at("config") == "new"); assert(fs.files.at("backup") == "old");
  }
  for (int fault = 0; fault < 5; ++fault) {
    Filesystem fs; fs.files["config"] = "old";
    fs.shortWrite = fault == 0; fs.corruptWrite = fault == 1; fs.failOpen = fault == 2;
    fs.failRename = fault == 3 ? 1 : (fault == 4 ? 2 : 0);
    assert(!save(fs)); recover(fs); assert(fs.files.at("config") == "old");
  }
  for (int cut = 1; cut <= 8; ++cut) {
    Filesystem fs; fs.files["config"] = "old"; fs.files["backup"] = "older"; fs.cutAt = cut;
    try { save(fs); } catch (const PowerLoss&) {}
    recover(fs);
    assert(fs.files.at("config") == "old" || fs.files.at("config") == "new");
  }
  {
    Filesystem fs; fs.files["backup"] = "old"; fs.files["tmp"] = "partial";
    recover(fs); assert(fs.files.at("config") == "old");
  }
  {
    Filesystem fs; fs.shortWrite = true; assert(!save(fs)); recover(fs);
    assert(!fs.exists("config"));
  }
  std::cout << "Config storage: success, short/corrupt writes, open/rename failures and power-loss recovery passed\n";
}
