#include "scan.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <system_error>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace dusk {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Entry {
  bool is_dir = false;
  bool is_link = false;
  std::uint64_t size = 0;
  std::uint64_t device = 0;
};

// What `path` is, without following a link. On POSIX, its size is the blocks
// it takes on disk, as `du` counts: a sparse file counts what it uses.
bool Stat(const fs::path& path, Entry& entry) {
#if defined(_WIN32)
  std::error_code error;
  const fs::file_status status = fs::symlink_status(path, error);
  if (error) {
    return false;
  }
  entry.is_link = fs::is_symlink(status);
  entry.is_dir = fs::is_directory(status);
  if (fs::is_regular_file(status)) {
    const std::uintmax_t size = fs::file_size(path, error);
    entry.size = error ? 0 : size;
  }
  return true;
#else
  struct stat info{};
  if (lstat(path.c_str(), &info) != 0) {
    return false;
  }
  entry.is_link = S_ISLNK(info.st_mode);
  entry.is_dir = S_ISDIR(info.st_mode);
  entry.size = static_cast<std::uint64_t>(info.st_blocks) * 512;
  entry.device = static_cast<std::uint64_t>(info.st_dev);
  return true;
#endif
}

class Scanner {
 public:
  Scanner(const std::atomic<bool>& stop,
          const std::function<void(const Progress&)>& report)
      : stop_(stop), report_(report) {}

  void Walk(const fs::path& path, Node& dir, std::uint64_t device) {
    progress_.current = path.string();
    std::error_code error;
    fs::directory_iterator it(path, error);
    if (error) {
      dir.unreadable = true;
      return;
    }
    for (; it != fs::directory_iterator(); it.increment(error)) {
      if (error) {
        dir.unreadable = true;
        break;
      }
      if (stop_) {
        return;
      }
      const fs::path child_path = it->path();
      Entry entry;
      if (!Stat(child_path, entry)) {
        continue;
      }
      auto child = std::make_unique<Node>();
      child->name = child_path.filename().string();
      child->parent = &dir;
      child->size = entry.size;
      // Another filesystem mounted here is not this one's space.
      if (entry.is_dir && !entry.is_link && entry.device == device) {
        child->is_dir = true;
        Walk(child_path, *child, device);
      } else {
        child->files = 1;
        ++progress_.files;
        progress_.bytes += entry.size;
      }
      dir.size += child->size;
      dir.files += child->files;
      dir.children.push_back(std::move(child));
      Report();
    }
    std::ranges::sort(dir.children, [](const auto& a, const auto& b) {
      return a->size != b->size ? a->size > b->size : a->name < b->name;
    });
  }

 private:
  void Report() {
    const auto now = Clock::now();
    if (now - last_report_ > std::chrono::milliseconds(50)) {
      last_report_ = now;
      report_(progress_);
    }
  }

  const std::atomic<bool>& stop_;
  const std::function<void(const Progress&)>& report_;
  Progress progress_;
  Clock::time_point last_report_ = Clock::now();
};

}  // namespace

std::unique_ptr<Node> Scan(const fs::path& root,
                           const std::atomic<bool>& stop,
                           const std::function<void(const Progress&)>& report) {
  auto node = std::make_unique<Node>();
  std::error_code error;
  const fs::path absolute = fs::absolute(root, error);
  node->name = (error ? root : absolute).lexically_normal().string();
  node->is_dir = true;
  Entry entry;
  if (!Stat(root, entry) || !entry.is_dir) {
    node->unreadable = true;
    return node;
  }
  node->size = entry.size;
  Scanner(stop, report).Walk(root, *node, entry.device);
  return node;
}

std::string FormatSize(std::uint64_t bytes) {
  static constexpr const char* kUnits[] = {"B",   "KiB", "MiB",
                                           "GiB", "TiB", "PiB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024 && unit < 5) {
    value /= 1024;
    ++unit;
  }
  char text[32];
  if (unit == 0) {
    std::snprintf(text, sizeof(text), "%llu B",
                  static_cast<unsigned long long>(bytes));
  } else {
    std::snprintf(text, sizeof(text), value < 10 ? "%.1f %s" : "%.0f %s", value,
                  kUnits[unit]);
  }
  return text;
}

std::string FormatCount(std::uint64_t count) {
  std::string digits = std::to_string(count);
  std::string out;
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i > 0 && (digits.size() - i) % 3 == 0) {
      out += ',';
    }
    out += digits[i];
  }
  return out;
}

}  // namespace dusk
