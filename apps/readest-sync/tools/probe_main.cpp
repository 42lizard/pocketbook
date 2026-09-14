#include "probe.h"
#include <inkview.h>
#include <json-c/json.h>

#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
const std::string root = "/mnt/ext1/system/readest-sync/probe";
const std::string book = "/mnt/ext1/Books/Readest/readest-sync-probe.epub";
ifont* font = nullptr;
std::string status = "Native-position diagnostic. Cloud sync is not implemented yet.";
std::string current_cfi;
int selected = 0;
unsigned sequence = 0;
bool ready = false;
std::string pending_cfi;
const char* labels[] = {
    "1. Open test EPUB normally",
    "2. Capture native saved position",
    "3. Remember captured position",
    "4. Prepare D2: Readest BRAVO",
    "5. Exit"
};
const int count = sizeof(labels) / sizeof(labels[0]);

void add(json_object* obj, const char* key, const std::string& value) {
    json_object_object_add(obj, key, json_object_new_string(value.c_str()));
}

void event(const std::string& action, const std::string& details) {
    json_object* entry = json_object_new_object();
    json_object_object_add(entry, "timestamp", json_object_new_int64(time(nullptr)));
    add(entry, "action", action);
    add(entry, "details", details);
    add(entry, "model", GetDeviceModel() ? GetDeviceModel() : "unknown");
    add(entry, "firmware", GetSoftwareVersion() ? GetSoftwareVersion() : "unknown");
    std::string output = json_object_to_json_string_ext(entry, JSON_C_TO_STRING_PLAIN);
    json_object_put(entry);
    FILE* log = fopen((root + "/events.jsonl").c_str(), "ab");
    if (!log) throw std::runtime_error("Cannot open diagnostic log");
    output += '\n';
    bool ok = fwrite(output.data(), 1, output.size(), log) == output.size();
    if (fflush(log) != 0 || fsync(fileno(log)) != 0) ok = false;
    if (fclose(log) != 0) ok = false;
    if (!ok) throw std::runtime_error("Cannot save diagnostic log");
}

int top() { return ScreenHeight() / 7; }
int row_height() { return ScreenHeight() / 13; }

void draw() {
    if (!font) return;
    ClearScreen();
    SetFont(font, BLACK);
    DrawTextRect(20, 12, ScreenWidth() - 40, top() - 20,
                 "Readest Sync\nPosition probe 4 - test D2", ALIGN_CENTER | VALIGN_MIDDLE);
    for (int i = 0; i < count; ++i) {
        int y = top() + i * row_height();
        if (i == selected) {
            FillArea(20, y, ScreenWidth() - 40, row_height() - 6, BLACK);
            SetFont(font, WHITE);
        } else {
            DrawRect(20, y, ScreenWidth() - 40, row_height() - 6, BLACK);
            SetFont(font, BLACK);
        }
        DrawTextRect(32, y + 4, ScreenWidth() - 64, row_height() - 14,
                     i == 3 && !pending_cfi.empty() ? "4. Run test D2 now" : labels[i],
                     ALIGN_LEFT | VALIGN_MIDDLE);
    }
    SetFont(font, BLACK);
    int y = top() + count * row_height() + 10;
    DrawTextRect(24, y, ScreenWidth() - 48, ScreenHeight() - y - 20,
                 status.c_str(), ALIGN_LEFT | VALIGN_TOP);
    FullUpdate();
}

void capture() {
    current_cfi.clear();
    std::string errors;
    for (const auto& pair : {
        std::make_pair("/mnt/ext1/system/explorer-3/explorer-3.db", "/snapshot/explorer-3.db"),
        std::make_pair("/mnt/ext1/system/config/books.db", "/snapshot/books.db")}) {
        try {
            readest::snapshot_database(pair.first, root + pair.second);
        } catch (const std::exception& e) {
            errors += std::string(e.what()) + "\n";
        }
    }
    // A failed copy must never be replaced by a stale previous snapshot.
    if (!errors.empty()) throw std::runtime_error(errors);
    auto state = readest::inspect(root + "/snapshot/explorer-3.db", root + "/snapshot/books.db", book);
    std::string filename = "capture-" + std::to_string(time(nullptr)) + "-" +
                           std::to_string(getpid()) + "-" + std::to_string(++sequence) + ".json";
    readest::write_text(root + "/" + filename, state.report + "\n");
    event("capture", filename);
    current_cfi = state.cfi;
    status = state.status + "\nSaved " + filename;
}

void open_book() {
    struct stat st;
    if (stat(book.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        throw std::runtime_error("Copy readest-sync-probe.epub into Books/Readest first.");
    const char* handler = GetFileHandler(book.c_str());
    event("open-request", "api=OpenBook; normal open; handler=" + std::string(handler ? handler : "unknown"));
    int result = OpenBook(book.c_str(), nullptr, 0);
    event("open-return", std::to_string(result));
    status = "Launch returned " + std::to_string(result) +
             ". This does not prove the position was applied. Check the visible marker.";
}

void activate() {
    if (selected == count - 1) { CloseApp(); return; }
    if (!ready) { draw(); return; }
    if (selected != 3) pending_cfi.clear();
    try {
        event("select", labels[selected]);
        if (selected == 0) open_book();
        else if (selected == 1) capture();
        else if (selected == 2) {
            if (current_cfi.empty()) throw std::runtime_error("Capture an unambiguous position first.");
            readest::write_text(root + "/remembered-cfi.txt", current_cfi + "\n");
            event("remember", current_cfi);
            status = "Position remembered. Move elsewhere in the test book, close it, then replay.";
        } else {
            if (pending_cfi.empty()) {
                pending_cfi = "epubcfi(/6/4[bravo]!/4/2)";
                if (pending_cfi.empty()) throw std::runtime_error("Missing or unsupported saved CFI.");
                status = "D2: update TEST BOOK position only\nTarget: BRAVO-01\n" + pending_cfi +
                         "\nClose the book with Back first.\nRun D2 backs up, writes position, and opens the book. Back cancels.\n"
                         "After launch, report ALPHA, BRAVO, or the visible marker.";
                event("prepare-D2", pending_cfi);
            } else {
                pending_cfi.clear();
                if (std::string(GetDeviceModel() ? GetDeviceModel() : "") != "PB743G" ||
                    std::string(GetSoftwareVersion() ? GetSoftwareVersion() : "") != "U743g.6.11.1683")
                    throw std::runtime_error("D2 is restricted to the tested InkPad 4 firmware.");
                std::string directory = root + "/trial-" + std::to_string(time(nullptr)) + "-" +
                    std::to_string(getpid()) + "-" + std::to_string(++sequence);
                event("apply-D2-start", directory);
                readest::apply_readest_trial("/mnt/ext1/system/explorer-3/explorer-3.db", directory);
                event("apply-D2-committed", directory);
                open_book();
            }
        }
    } catch (const std::exception& e) {
        status = e.what();
    }
    draw();
}

int handle(int type, int a, int b) {
    if (type == EVT_INIT) {
        font = OpenFont("DejaVuSans", ScreenWidth() / 34, 1);
        if (!font) { CloseApp(); return 0; }
        try {
            readest::make_directory("/mnt/ext1/system/readest-sync");
            readest::make_directory(root);
            readest::make_directory(root + "/snapshot");
            event("start", "Probe 4; D2 updates only fixture position/timestamp after explicit activation. No cloud requests.");
            ready = true;
        } catch (const std::exception& e) { status = e.what(); }
    } else if (type == EVT_SHOW || type == EVT_ORIENTATION) {
        draw();
    } else if (type == EVT_POINTERUP) {
        if (a >= 20 && a < ScreenWidth() - 20 && b >= top() && b < top() + count * row_height()) {
            if ((b - top()) % row_height() >= row_height() - 6) return 0;
            selected = (b - top()) / row_height();
            activate();
        }
    } else if (type == EVT_KEYPRESS) {
        if (a == IV_KEY_BACK) {
            if (pending_cfi.empty()) CloseApp();
            else { pending_cfi.clear(); status = "Test D2 cancelled."; draw(); }
        }
        else if (a == IV_KEY_OK) activate();
        else if (a == IV_KEY_NEXT || a == IV_KEY_DOWN) { pending_cfi.clear(); selected = (selected + 1) % count; draw(); }
        else if (a == IV_KEY_PREV || a == IV_KEY_UP) { pending_cfi.clear(); selected = (selected + count - 1) % count; draw(); }
    } else if (type == EVT_EXIT && font) {
        CloseFont(font);
        font = nullptr;
    }
    return 0;
}
} // namespace

int main() { InkViewMain(handle); return 0; }
