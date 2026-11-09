// The interface: a scan in progress, then the tree to browse, biggest first.
#ifndef DUSK_DUSK_HPP
#define DUSK_DUSK_HPP

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <rtxui/rtxui.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "scan.hpp"

namespace dusk {

// One line of the listing, as the template reads it.
struct Row {
  int index = 0;
  std::string name;
  std::string kind;  // "dir" or "file".
  std::string size;
  std::string share;
  std::string bar;
  std::string count;
  bool selected = false;
};

inline void Fields(const Row& row, rtxui::FieldSink& out) {
  out("id", "row-" + std::to_string(row.index));
  out("index", std::to_string(row.index));
  out("name", row.name);
  out("kind", row.kind);
  out("size", row.size);
  out("share", row.share);
  out("bar", row.bar);
  out("count", row.count);
  out("state", row.selected ? "selected" : "");
}

class Dusk : public rtxui::Component<Dusk> {
 public:
  enum class Sort { kSize, kName, kFiles };

  // Set by main(): what `q` does.
  std::function<void()> on_quit;
  // Scans on the calling thread rather than a worker: for RTXUI_HEADLESS
  // runs, which print the screen once the input is handled.
  bool synchronous = false;

  bool scanning = false;
  std::string scan_path;
  std::string scan_files;
  std::string scan_bytes;
  std::string scan_current;

  std::string path;
  std::string total;
  std::string sort_label = "size";
  std::string notice;  // What went wrong, or why a directory shows less.
  std::vector<Row> rows;
  int cursor = 0;

  // Typing a filter after `/`, and the filter itself.
  bool filtering = false;
  std::string filter;

  // The confirmation `d` asks for.
  bool confirming = false;
  std::string confirm_name;
  std::string confirm_detail;

  Dusk();
  ~Dusk() override;

  // Scans `root`, on a worker thread unless `synchronous`, showing its
  // progress meanwhile.
  void StartScan(const std::filesystem::path& root);

  bool is_empty() const { return !scanning && rows.empty(); }
  bool has_notice() const { return !notice.empty(); }
  bool has_filter() const { return filtering || !filter.empty(); }

  // Handlers, for the mouse.
  void Click(const std::string& index);
  void ConfirmDelete();
  void CancelDelete();

  bool OnEvent(rtxui::Event event) override;
  bool Digest() override;

  // The directory shown, and the entry under the cursor.
  const Node* directory() const { return dir_; }
  const Node* selected() const;
  Sort sort() const { return sort_; }

  std::string_view view = R"html(
      <div class="app">
        <div class="header">
          <span class="brand">dusk</span>
          <span class="path">{path}</span>
          <span>by {sort_label}</span>
          <span class="total">{total}</span>
        </div>

        <if condition="{scanning}">
          <div class="scan">
            <div class="card">
              <span class="title">Scanning {scan_path}</span>
              <span>{scan_files} files, {scan_bytes}</span>
              <span class="current">{scan_current}</span>
            </div>
          </div>
        </if>
        <else>
          <div id="list" class="list">
            <for each="{rows}" as="row">
              <div id="{row.id}" class="row {row.kind} {row.state}" onclick="Click({row.index})">
                <span class="size">{row.size}</span>
                <span class="share">{row.share}</span>
                <span class="bar">{row.bar}</span>
                <span class="name">{row.name}</span>
                <span class="count">{row.count}</span>
              </div>
            </for>
            <div if="{is_empty}" class="empty">Nothing here</div>
          </div>
        </else>

        <div class="footer">
          <span if="{has_filter}" class="filter">/{filter}</span>
          <span if="{has_notice}" class="notice">{notice}</span>
          <span class="spacer"></span>
          <span class="keys">
            <span><b>→</b> open</span>
            <span><b>←</b> back</span>
            <span><b>/</b> filter</span>
            <span><b>s</b> sort</span>
            <span><b>r</b> rescan</span>
            <span><b>d</b> delete</span>
            <span><b>q</b> quit</span>
          </span>
        </div>

        <dialog open="{confirming}" title="Delete">
          <div>Delete <b>{confirm_name}</b>?</div>
          <div class="detail">{confirm_detail}. This cannot be undone.</div>
          <div class="actions">
            <button class="danger" onclick="ConfirmDelete">y  Delete</button>
            <button onclick="CancelDelete">n  Cancel</button>
          </div>
        </dialog>
      </div>

      <style>
        self {
          --surface: rgb(22, 27, 34);
          --raised: rgb(33, 38, 45);
          --border: rgb(48, 54, 61);
          --muted: rgb(139, 148, 158);
          --accent: rgb(88, 166, 255);
          --folder: rgb(210, 153, 34);

          display: block;
          width: 100%;
          height: 100%;
          color: rgb(230, 237, 243);
        }
        .app {
          display: flex;
          flex-direction: column;
          height: 100%;
        }
        .header, .footer {
          display: flex;
          gap: 2;
          padding: 0 1;
          background-color: var(--surface);
          color: var(--muted);
        }
        /* What does not wrap keeps its width; the path and the notice,
           which clip, give way instead. */
        .header > span, .footer > span {
          white-space: nowrap;
        }
        .brand {
          color: var(--accent);
          font-weight: bold;
        }
        .path {
          flex-grow: 1;
          color: rgb(230, 237, 243);
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
        }
        .spacer {
          flex-grow: 1;
        }
        .keys {
          display: flex;
          gap: 2;
        }
        b {
          color: rgb(230, 237, 243);
        }
        .notice {
          color: var(--folder);
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
        }
        .filter {
          color: var(--accent);
        }
        .detail {
          color: var(--muted);
        }
        .actions {
          display: flex;
          justify-content: flex-end;
          gap: 2;
          margin-top: 1;
        }
        button {
          padding: 0 1;
        }
        button:hover, button:focus {
          color: var(--accent);
        }
        .danger {
          background-color: rgb(218, 54, 51);
          color: rgb(255, 255, 255);
        }

        .scan {
          display: flex;
          flex-grow: 1;
          align-items: center;
          justify-content: center;
        }
        .card {
          display: flex;
          flex-direction: column;
          border: rounded;
          border-color: var(--border);
          padding: 1 3;
          width: 60;
        }
        .title {
          color: var(--accent);
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
        }
        .current {
          color: var(--muted);
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
        }

        .list {
          flex-grow: 1;
          overflow-y: scroll;
        }
        .row {
          display: flex;
          gap: 2;
          padding: 0 1;
          white-space: nowrap;
        }
        .row:hover {
          background-color: var(--surface);
        }
        .row.selected {
          background-color: var(--raised);
        }
        .size {
          width: 9;
          text-align: right;
        }
        .share {
          width: 6;
          text-align: right;
          color: var(--muted);
        }
        .bar {
          width: 16;
          color: var(--accent);
          background-color: var(--surface);
        }
        .name {
          flex-grow: 1;
          overflow: hidden;
          text-overflow: ellipsis;
        }
        .dir .name {
          color: var(--folder);
          font-weight: bold;
        }
        .count {
          width: 14;
          text-align: right;
          color: var(--muted);
        }
        .empty {
          padding: 1 2;
          color: var(--muted);
        }
      </style>
    )html";

 private:
  void Show(Node* dir, const Node* select = nullptr);
  void MoveTo(int index);
  void Open();
  void Back();
  void CycleSort();
  void Rescan();
  void AskDelete();
  // Scans `target`'s path: the whole tree when null, else that directory,
  // whose content the result replaces.
  void ScanInto(Node* target, const std::filesystem::path& root);
  void Replace(Node* target, std::unique_ptr<Node> scanned);
  bool OnFilterKey(const rtxui::Event& event);

  std::unique_ptr<Node> tree_;
  Node* dir_ = nullptr;
  std::vector<Node*> shown_;  // What the rows show, in their order.
  Sort sort_ = Sort::kSize;
  std::thread worker_;
  std::atomic<bool> stop_ = false;
  bool reveal_ = false;  // The cursor's row has to be scrolled into view.
};

}  // namespace dusk

#endif  // DUSK_DUSK_HPP
