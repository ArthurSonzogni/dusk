// Scans a small tree made for the test, then drives the interface the way a
// user does: keys in, the screen's text out.
#include "dusk.hpp"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <rtxui/rtxui.hpp>
#include <string>
#include <string_view>

#include "scan.hpp"

namespace fs = std::filesystem;

namespace {

int failures = 0;

void Expect(bool ok, std::string_view what, const std::string& context = "") {
  if (!ok) {
    std::printf("FAILED: %.*s\n%s\n", static_cast<int>(what.size()),
                what.data(), context.c_str());
    ++failures;
  }
}

void WriteFile(const fs::path& path, size_t bytes) {
  std::ofstream(path, std::ios::binary) << std::string(bytes, 'x');
}

}  // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / "dusk_test_tree";
  fs::remove_all(root);
  fs::create_directories(root / "sub");
  WriteFile(root / "big.bin", 400'000);
  WriteFile(root / "sub" / "inner.bin", 200'000);
  WriteFile(root / "mid.bin", 100'000);
  WriteFile(root / "small.txt", 10);

  const std::atomic<bool> stop = false;
  auto tree = dusk::Scan(root, stop, [](const dusk::Progress&) {});
  Expect(tree->files == 4, "four files are counted");
  Expect(tree->children.size() == 4, "the root lists its four entries");
  if (tree->children.size() == 4) {
    Expect(tree->children[0]->name == "big.bin", "the biggest comes first");
    Expect(tree->children[1]->name == "sub", "then the directory");
    Expect(tree->children[3]->name == "small.txt", "the smallest comes last");
  }

  // The app scans on a thread, as it does in a terminal.
  auto app = rtxui::Ref<dusk::Dusk>::New();
  rtxui::HeadlessScreen screen(app, 100, 12);
  app->StartScan(root);
  Expect(screen.WaitUntil([&] { return !app->scanning; }), "the scan ends");
  Expect(screen.Text().find("big.bin") != std::string::npos,
         "the listing is drawn", screen.Text());

  screen.Input("\x1b[B");  // Down, to sub/.
  Expect(app->selected() && app->selected()->name == "sub",
         "Down selects the next entry", screen.Text());

  screen.Input("\x1b[C");  // Right opens it.
  Expect(app->directory()->name == "sub", "Right opens the directory",
         screen.Text());
  Expect(screen.Text().find("inner.bin") != std::string::npos,
         "its content is drawn", screen.Text());

  screen.Input("\x1b[D");  // Left goes back, with sub/ still selected.
  Expect(app->directory()->parent == nullptr, "Left goes back", screen.Text());
  Expect(app->selected() && app->selected()->name == "sub",
         "the directory left is selected", screen.Text());

  // On a screen showing two rows, End scrolls to the last entry.
  screen.Resize(100, 4);
  screen.Input("G");
  Expect(screen.Text().find("small.txt") != std::string::npos,
         "the list scrolls to the selection", screen.Text());
  Expect(screen.Text().find("big.bin") == std::string::npos,
         "what it scrolled past is hidden", screen.Text());

  screen.Resize(100, 12);
  auto names = [&] {
    std::string out;
    for (const dusk::Row& row : app->rows) {
      out += row.name + " ";
    }
    return out;
  };

  // s sorts by name, then by file count, then by size again.
  screen.Input("s");
  Expect(names() == "big.bin mid.bin small.txt sub/ ", "s sorts by name",
         names());
  screen.Input("ss");
  Expect(names() == "big.bin sub/ mid.bin small.txt ", "back to size", names());

  // / filters by name; Escape drops the filter.
  screen.Input("/MI");
  Expect(names() == "mid.bin ", "the filter ignores case", names());
  screen.Input("\r");
  Expect(!app->filtering && app->filter == "MI", "Enter keeps the filter");
  screen.Input("\x1b");
  Expect(app->filter.empty() && app->rows.size() == 4,
         "Escape drops the filter", names());

  // d asks first; n keeps the file, y deletes it.
  screen.Input("G");  // small.txt.
  screen.Input("d");
  Expect(app->confirming, "d asks for confirmation", screen.Text());
  Expect(screen.Text().find("Delete small.txt?") != std::string::npos,
         "the dialog names the entry", screen.Text());
  screen.Input("n");
  Expect(!app->confirming && fs::exists(root / "small.txt"), "n keeps it",
         screen.Text());
  screen.Input("dy");
  Expect(!fs::exists(root / "small.txt"), "y deletes it from the disk");
  Expect(names() == "big.bin sub/ mid.bin ", "and from the list", names());
  Expect(app->directory()->files == 3, "the totals follow");

  // r scans the directory again, on a thread, which finds a file made
  // meanwhile.
  WriteFile(root / "new.bin", 300'000);
  screen.Input("r");
  Expect(screen.WaitUntil([&] { return !app->scanning; }), "the scan ends");
  Expect(names() == "big.bin new.bin sub/ mid.bin ", "r finds the new file",
         names());
  Expect(app->directory()->files == 4, "and counts it");

  fs::remove_all(root);
  return failures == 0 ? 0 : 1;
}
