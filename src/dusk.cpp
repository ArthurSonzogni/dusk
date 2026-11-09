#include "dusk.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <system_error>
#include <utility>

namespace dusk {

namespace {

namespace fs = std::filesystem;

// The most rows a directory lists. The rest, the smallest, are summed up in
// one line: nobody scrolls through 100,000 files, and the screen stays fast.
constexpr int kMaxRows = 2000;
constexpr int kBarWidth = 16;
constexpr int kPage = 10;

// `fraction` of kBarWidth cells, to the eighth of a cell.
std::string Bar(double fraction) {
  static constexpr const char* kEighths[] = {"",  "▏", "▎", "▍",
                                             "▌", "▋", "▊", "▉"};
  const int eighths =
      static_cast<int>(std::clamp(fraction, 0.0, 1.0) * kBarWidth * 8);
  std::string bar;
  for (int i = 0; i < eighths / 8; ++i) {
    bar += "█";
  }
  bar += kEighths[eighths % 8];
  return bar;
}

std::string Share(std::uint64_t part, std::uint64_t whole) {
  if (whole == 0) {
    return "";
  }
  const double percent =
      100.0 * static_cast<double>(part) / static_cast<double>(whole);
  char text[16];
  std::snprintf(text, sizeof(text), "%.1f%%", percent);
  return text;
}

// The root's name is its absolute path; every other node's, its file name.
fs::path FullPath(const Node* node) {
  if (!node->parent) {
    return fs::path(node->name);
  }
  return FullPath(node->parent) / node->name;
}

std::string Lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

// Adds `size` and `files` (negative to take away) to `node` and every
// directory above it.
void AddUp(Node* node, std::int64_t size, std::int64_t files) {
  for (; node; node = node->parent) {
    node->size = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(node->size) + size);
    node->files = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(node->files) + files);
  }
}

}  // namespace

Dusk::Dusk() {
  Bind(scanning);
  Bind(scan_path);
  Bind(scan_files);
  Bind(scan_bytes);
  Bind(scan_current);
  Bind(path);
  Bind(total);
  Bind(sort_label);
  Bind(notice);
  Bind(rows);
  Bind(filter);
  Bind(confirming);
  Bind(confirm_name);
  Bind(confirm_detail);
  Bind(is_empty);
  Bind(has_notice);
  Bind(has_filter);
  Bind(Click);
  Bind(ConfirmDelete);
  Bind(CancelDelete);
}

Dusk::~Dusk() {
  stop_ = true;
  if (worker_.joinable()) {
    worker_.join();
  }
}

void Dusk::StartScan(const fs::path& root) {
  ScanInto(nullptr, root);
}

void Dusk::ScanInto(Node* target, const fs::path& root) {
  scanning = true;
  scan_path = root.string();
  scan_files = "0";
  scan_bytes = FormatSize(0);
  scan_current.clear();
  if (synchronous) {
    Replace(target, Scan(root, stop_, [](const Progress&) {}));
    return;
  }
  if (worker_.joinable()) {
    worker_.join();  // The last scan, done since it handed its result over.
  }
  worker_ = std::thread([this, target, root] {
    auto tree = Scan(root, stop_, [this](const Progress& progress) {
      rtxui::PostTask([this, progress] {
        scan_files = FormatCount(progress.files);
        scan_bytes = FormatSize(progress.bytes);
        scan_current = progress.current;
      });
    });
    if (stop_) {
      return;
    }
    // A unique_ptr cannot ride in a copyable task: hand it over raw.
    rtxui::PostTask([this, target, raw = tree.release()] {
      Replace(target, std::unique_ptr<Node>(raw));
    });
  });
}

void Dusk::Replace(Node* target, std::unique_ptr<Node> scanned) {
  scanning = false;
  if (!target) {
    tree_ = std::move(scanned);
    Show(tree_.get());
    return;
  }
  // The directory keeps its place in the tree, with its new content.
  AddUp(target->parent,
        static_cast<std::int64_t>(scanned->size) -
            static_cast<std::int64_t>(target->size),
        static_cast<std::int64_t>(scanned->files) -
            static_cast<std::int64_t>(target->files));
  target->size = scanned->size;
  target->files = scanned->files;
  target->unreadable = scanned->unreadable;
  target->children = std::move(scanned->children);
  for (auto& child : target->children) {
    child->parent = target;
  }
  Show(target);
}

const Node* Dusk::selected() const {
  if (cursor < 0 || cursor >= static_cast<int>(shown_.size())) {
    return nullptr;
  }
  return shown_[cursor];
}

void Dusk::Show(Node* dir, const Node* select) {
  if (dir != dir_) {
    filter.clear();
    filtering = false;
  }
  dir_ = dir;
  path = FullPath(dir).string();
  total = FormatSize(dir->size) + " in " + FormatCount(dir->files) + " files";
  if (notice.empty() && dir->unreadable) {
    notice = "Some of it could not be read.";
  }

  std::ranges::stable_sort(dir->children, [&](const auto& a, const auto& b) {
    switch (sort_) {
      case Sort::kName:
        return Lower(a->name) < Lower(b->name);
      case Sort::kFiles:
        return a->files > b->files;
      case Sort::kSize:
        break;
    }
    return a->size > b->size;
  });

  const std::string needle = Lower(filter);
  std::vector<Node*> matches;
  for (auto& child : dir->children) {
    if (needle.empty() ||
        Lower(child->name).find(needle) != std::string::npos) {
      matches.push_back(child.get());
    }
  }

  shown_.assign(matches.begin(),
                matches.begin() + std::min<size_t>(matches.size(), kMaxRows));
  rows.clear();
  int select_index = 0;
  for (size_t i = 0; i < shown_.size(); ++i) {
    const Node& child = *shown_[i];
    Row row;
    row.index = static_cast<int>(i);
    row.name = child.name + (child.is_dir ? "/" : "");
    row.kind = child.is_dir ? "dir" : "file";
    row.size = FormatSize(child.size);
    row.share = Share(child.size, dir->size);
    row.bar = Bar(dir->size ? static_cast<double>(child.size) /
                                  static_cast<double>(dir->size)
                            : 0.0);
    row.count = child.is_dir ? FormatCount(child.files) + " files" : "";
    if (&child == select) {
      select_index = static_cast<int>(i);
    }
    rows.push_back(std::move(row));
  }
  if (matches.size() > shown_.size()) {
    std::uint64_t rest = 0;
    for (size_t i = shown_.size(); i < matches.size(); ++i) {
      rest += matches[i]->size;
    }
    Row row;
    row.index = static_cast<int>(shown_.size());
    row.name =
        "and " + FormatCount(matches.size() - shown_.size()) + " more entries";
    row.kind = "file";
    row.size = FormatSize(rest);
    row.share = Share(rest, dir->size);
    rows.push_back(std::move(row));
  }
  cursor = 0;
  MoveTo(select_index);
}

void Dusk::MoveTo(int index) {
  const int last = static_cast<int>(shown_.size()) - 1;
  if (last < 0) {
    return;
  }
  index = std::clamp(index, 0, last);
  rows[cursor].selected = false;
  rows[index].selected = true;
  cursor = index;
  reveal_ = true;
}

bool Dusk::Digest() {
  const bool changed = Component<Dusk>::Digest();
  // Once the rows are rendered: MoveTo() may run before the row it moves to
  // exists, right after Show() made it.
  if (reveal_) {
    reveal_ = false;
    QueryElement("#row-" + std::to_string(cursor)).ScrollIntoView();
  }
  return changed;
}

void Dusk::Open() {
  Node* node = shown_.empty() ? nullptr : shown_[cursor];
  if (node && node->is_dir) {
    notice.clear();
    Show(node);
  }
}

void Dusk::Back() {
  if (dir_ && dir_->parent) {
    notice.clear();
    Show(dir_->parent, dir_);
  }
}

void Dusk::CycleSort() {
  switch (sort_) {
    case Sort::kSize:
      sort_ = Sort::kName;
      sort_label = "name";
      break;
    case Sort::kName:
      sort_ = Sort::kFiles;
      sort_label = "files";
      break;
    case Sort::kFiles:
      sort_ = Sort::kSize;
      sort_label = "size";
      break;
  }
  Show(dir_, selected());
}

void Dusk::Rescan() {
  notice.clear();
  ScanInto(dir_, FullPath(dir_));
}

void Dusk::AskDelete() {
  const Node* node = selected();
  if (!node) {
    return;
  }
  confirm_name = node->name + (node->is_dir ? "/" : "");
  confirm_detail = FormatSize(node->size);
  if (node->is_dir) {
    confirm_detail += ", " + FormatCount(node->files) + " files";
  }
  confirming = true;
}

void Dusk::CancelDelete() {
  confirming = false;
}

void Dusk::ConfirmDelete() {
  confirming = false;
  Node* node = shown_.empty() ? nullptr : shown_[cursor];
  if (!node) {
    return;
  }
  std::error_code error;
  fs::remove_all(FullPath(node), error);
  if (error) {
    // Part of it may be gone: count again what is left.
    const std::string message = error.message();
    Rescan();
    notice = "Could not delete " + node->name + ": " + message;
    return;
  }
  AddUp(dir_, -static_cast<std::int64_t>(node->size),
        -static_cast<std::int64_t>(node->files));
  const int was = cursor;
  std::erase_if(dir_->children,
                [&](const auto& child) { return child.get() == node; });
  notice = "Deleted " + confirm_name;
  Show(dir_);
  MoveTo(was);
}

void Dusk::Click(const std::string& index) {
  const int row = std::stoi(index);
  // A click selects; a click on the selected row opens it.
  if (row == cursor) {
    Open();
  } else {
    MoveTo(row);
  }
}

// While typing after `/`: text narrows the list, Backspace takes a character
// back, Enter keeps the filter, Escape drops it.
bool Dusk::OnFilterKey(const rtxui::Event& event) {
  using rtxui::Event;
  if (event == Event::Return()) {
    filtering = false;
  } else if (event == Event::Escape()) {
    filtering = false;
    filter.clear();
    Show(dir_);
  } else if (event == Event::Backspace()) {
    if (!filter.empty()) {
      filter.pop_back();
    }
    Show(dir_);
  } else if (event.is<Event::Keyboard>()) {
    const std::uint32_t c = event.get<Event::Keyboard>().codepoint();
    if (c < 32 || c > 126) {
      return false;  // Arrows and the rest move as usual.
    }
    filter += static_cast<char>(c);
    Show(dir_);
  } else {
    return false;
  }
  return true;
}

bool Dusk::OnEvent(rtxui::Event event) {
  using rtxui::Event;
  auto is = [&](char c) { return event == Event(Event::Keyboard::From(c)); };

  if (confirming) {
    if (is('y') || event == Event::Return()) {
      ConfirmDelete();
      return true;
    }
    if (is('n') || event == Event::Escape()) {
      CancelDelete();
      return true;
    }
    return Component<Dusk>::OnEvent(event);  // The dialog's buttons.
  }
  if (filtering && OnFilterKey(event)) {
    return true;
  }
  if (is('q')) {
    if (on_quit) {
      on_quit();
    }
    return true;
  }
  if (scanning || !dir_) {
    return Component<Dusk>::OnEvent(event);
  }
  if (event == Event::ArrowDown() || is('j')) {
    MoveTo(cursor + 1);
  } else if (event == Event::ArrowUp() || is('k')) {
    MoveTo(cursor - 1);
  } else if (event == Event::PageDown()) {
    MoveTo(cursor + kPage);
  } else if (event == Event::PageUp()) {
    MoveTo(cursor - kPage);
  } else if (event == Event::Home() || is('g')) {
    MoveTo(0);
  } else if (event == Event::End() || is('G')) {
    MoveTo(static_cast<int>(shown_.size()) - 1);
  } else if (event == Event::ArrowRight() || event == Event::Return() ||
             is('l')) {
    Open();
  } else if (event == Event::ArrowLeft() || event == Event::Backspace() ||
             is('h')) {
    Back();
  } else if (is('/')) {
    filtering = true;
  } else if (event == Event::Escape() && !filter.empty()) {
    filter.clear();
    Show(dir_);
  } else if (is('s')) {
    CycleSort();
  } else if (is('r')) {
    Rescan();
  } else if (is('d')) {
    AskDelete();
  } else {
    return Component<Dusk>::OnEvent(event);
  }
  return true;
}

}  // namespace dusk
