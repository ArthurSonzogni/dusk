// dusk: where did the disk space go? Scans a directory, then lets you browse
// it, biggest first.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <rtxui/rtxui.hpp>
#include <string_view>

#include "dusk.hpp"

int main(int argc, char** argv) {
  const std::string_view arg = argc > 1 ? argv[1] : ".";
  if (arg == "-h" || arg == "--help") {
    std::puts(
        "Usage: dusk [directory]\n"
        "\n"
        "Shows what takes the space in a directory (the current one by\n"
        "default), biggest first. It stays on the directory's filesystem.\n"
        "\n"
        "  Up, Down, j, k         move\n"
        "  PageUp, PageDown       move by ten\n"
        "  Home, End, g, G        first, last\n"
        "  Right, Enter, l        open the directory\n"
        "  Left, Backspace, h     back to its parent\n"
        "  Click                  select, then open\n"
        "  /                      filter by name (Enter keeps it, Escape drops "
        "it)\n"
        "  s                      sort by size, name, then file count\n"
        "  r                      scan the directory shown again\n"
        "  d                      delete the entry, once confirmed\n"
        "  q                      quit");
    return 0;
  }
  const std::filesystem::path root(arg);
  std::error_code error;
  if (!std::filesystem::is_directory(root, error)) {
    std::fprintf(stderr, "dusk: %.*s is not a directory\n",
                 static_cast<int>(arg.size()), arg.data());
    return 1;
  }

  auto app = rtxui::Ref<dusk::Dusk>::New();
  rtxui::Screen screen(app);
  screen.SetInline(false);
  app->on_quit = [&screen] { screen.Exit(); };

  // Run headless (RTXUI_HEADLESS=80x24, for scripts and tests), the screen
  // is printed once the input piped in is handled: scan right away, so that
  // it shows the result rather than the scan's progress.
  app->synchronous = std::getenv("RTXUI_HEADLESS") != nullptr;
  app->StartScan(root);
  screen.Loop();
  return 0;
}
