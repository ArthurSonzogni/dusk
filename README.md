# dusk

Where did the disk space go? `dusk` scans a directory, then lets you browse
it, biggest first, and delete what you no longer need.

```
 dusk  /usr/share                                         by size  3.7 GiB in 368,544 files
   498 MiB   13.2%  ██                icons/                                 174,547 files
   490 MiB   13.0%  ██                doc/                                    24,990 files
   400 MiB   10.6%  █▋                locale/                                 15,695 files
   305 MiB    8.1%  █▎                fonts/                                   1,341 files
   201 MiB    5.3%  ▊                 texlive/                                17,670 files
   188 MiB    5.0%  ▊                 go-1.26/                                15,000 files
   175 MiB    4.7%  ▋                 nodejs/                                 25,164 files
   144 MiB    3.8%  ▌                 ibus/                                      201 files
    78 MiB    2.1%  ▎                 libreoffice/                                41 files
    71 MiB    1.9%  ▎                 qt6/                                       612 files
    68 MiB    1.8%  ▎                 man/                                    15,011 files
    63 MiB    1.7%  ▎                 backgrounds/                                28 files
                               → open  ← back  / filter  s sort  r rescan  d delete  q quit
```

- Scans on a worker thread, showing its progress, and stays on the
  directory's filesystem.
- Counts the space files take on disk, as `du` does (on Linux and macOS;
  their size on Windows).
- Runs on Linux, macOS and Windows, with the keyboard or the mouse.

It is built with [RTXUI](https://github.com/ArthurSonzogni/RTXUI): the whole
interface is an HTML template and CSS, in [`src/dusk.hpp`](src/dusk.hpp).

## Keys

| Key | Does |
|---|---|
| <kbd>↑</kbd> <kbd>↓</kbd>, <kbd>j</kbd> <kbd>k</kbd> | Move |
| <kbd>PgUp</kbd> <kbd>PgDn</kbd> | Move by ten |
| <kbd>Home</kbd> <kbd>End</kbd>, <kbd>g</kbd> <kbd>G</kbd> | First, last |
| <kbd>→</kbd> <kbd>Enter</kbd> <kbd>l</kbd> | Open the directory |
| <kbd>←</kbd> <kbd>Backspace</kbd> <kbd>h</kbd> | Back to its parent |
| Click | Select, then open |
| <kbd>/</kbd> | Filter by name (<kbd>Enter</kbd> keeps it, <kbd>Esc</kbd> drops it) |
| <kbd>s</kbd> | Sort by size, name, then file count |
| <kbd>r</kbd> | Scan the directory shown again |
| <kbd>d</kbd> | Delete the entry, once confirmed |
| <kbd>q</kbd> | Quit |

## Build

You need CMake 3.24+ and a C++23 compiler (GCC 13+, Clang 18+ or MSVC).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/dusk ~/Downloads
```

`cmake --install build` puts `dusk` on your `PATH`.

## Test

```bash
ctest --test-dir build
```

The test builds a small tree, then drives the interface the way you would:
keys in, the screen's text out. Any run can do the same without a terminal:

```bash
printf 'jj\x1b[C' | RTXUI_HEADLESS=100x30 ./build/dusk /usr
```

## License

MIT, see [LICENSE](LICENSE).
