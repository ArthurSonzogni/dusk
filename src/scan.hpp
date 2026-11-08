// Walks a directory tree and adds up what each directory holds.
#ifndef DUSK_SCAN_HPP
#define DUSK_SCAN_HPP

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dusk {

struct Node {
  std::string name;
  bool is_dir = false;
  bool unreadable = false;  // A directory that could not be listed.
  std::uint64_t size = 0;   // Bytes on disk, its descendants' included.
  std::uint64_t files = 0;  // Files beneath it, or 1 for a file.
  Node* parent = nullptr;
  std::vector<std::unique_ptr<Node>> children;  // Biggest first.
};

struct Progress {
  std::uint64_t files = 0;
  std::uint64_t bytes = 0;
  std::string current;  // The directory being read.
};

// Scans `root` without leaving its filesystem or following symbolic links.
// `report` is called now and then with how far it got, from the calling
// thread. Returns early, with what it has, once `stop` is set.
std::unique_ptr<Node> Scan(const std::filesystem::path& root,
                           const std::atomic<bool>& stop,
                           const std::function<void(const Progress&)>& report);

// "1.5 GiB", "312 KiB", "18 B".
std::string FormatSize(std::uint64_t bytes);
// "12,345".
std::string FormatCount(std::uint64_t count);

}  // namespace dusk

#endif  // DUSK_SCAN_HPP
