#include <algorithm>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <conio.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifndef MLANG_SOURCE_DIR
#define MLANG_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;

namespace
{
constexpr std::string_view kAnsiReset = "\x1b[0m";
constexpr std::string_view kAnsiFgDefault = "\x1b[39m";
constexpr std::string_view kAnsiBlue = "\x1b[34m";
constexpr std::string_view kAnsiGreen = "\x1b[32m";
constexpr std::string_view kAnsiEditField = "\x1b[37;40m";
constexpr std::string_view kAnsiCurrentLineBg = "\x1b[48;2;24;64;36m";

constexpr int kKeyEsc = 27;
constexpr int kKeyUp = 1001;
constexpr int kKeyDown = 1002;
constexpr int kKeyLeft = 1003;
constexpr int kKeyRight = 1004;
constexpr int kKeyResize = 1005;

int gPendingKey = 0;

#ifdef _WIN32
DWORD gOriginalOutMode = 0;
bool gOriginalOutModeSet = false;

HANDLE stdout_handle() noexcept
{
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

void enable_console_output_mode()
{
    DWORD outMode = 0;
    HANDLE out = stdout_handle();
    if(GetConsoleMode(out, &outMode))
    {
        gOriginalOutMode = outMode;
        gOriginalOutModeSet = true;
        outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(out, outMode);
    }
}

void restore_console_output_mode()
{
    if(gOriginalOutModeSet)
        SetConsoleMode(stdout_handle(), gOriginalOutMode);
}
#endif

#ifndef _WIN32
volatile std::sig_atomic_t gResizePending = 0;

void handle_resize(int)
{
    gResizePending = 1;
}
#endif

struct TerminalSize
{
    int rows = 24;
    int cols = 80;
};

struct Layout
{
    int titleRows = 1;
    int keyRows = 1;
    int spacerRows = 1;
    int listRows = 1;
    int docTitleRows = 1;
    int docRows = 1;
    int outputRows = 2;
    int messageRows = 3;
};

enum class ItemKind
{
    Choice,
    Text,
    Toggle,
};

enum class RowKind
{
    Section,
    Item,
};

struct Config
{
    fs::path sourceDir = fs::absolute(fs::path(MLANG_SOURCE_DIR));
    fs::path buildDir = sourceDir / "build";
    std::string buildType = "Release";
    std::string installPrefix = "~/.local";
    std::string binDir = "~/.local/bin";
    std::string jobs;
    bool runUnitTests = false;
    bool runRobotTests = false;
};

struct Item
{
    ItemKind kind;
    std::string label;
    bool Config::* flag = nullptr;
    std::string Config::* text = nullptr;
    std::vector<std::string> choices;
    std::string help;
};

struct Section
{
    std::string label;
    std::string help;
    bool open = true;
    std::vector<Item> items;
};

struct VisibleRow
{
    RowKind kind;
    size_t section = 0;
    size_t item = 0;
};

const std::vector<std::string> kBuildTypes = {"Release", "Debug",
                                              "RelWithDebInfo", "MinSizeRel"};

std::string trim(std::string s)
{
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::string default_jobs()
{
    unsigned n = std::thread::hardware_concurrency();
    return std::to_string(n == 0 ? 4 : n);
}

bool truthy(const std::string& value)
{
    std::string v = value;
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return v == "1" || v == "on" || v == "true" || v == "yes";
}

fs::path config_path(const Config& cfg)
{
    return cfg.buildDir / "mlang-config.conf";
}

fs::path cache_path(const Config& cfg)
{
    return cfg.buildDir / "mlang_config_cache.cmake";
}

std::map<std::string, std::string> read_key_values(const fs::path& path)
{
    std::map<std::string, std::string> out;
    std::ifstream in(path);
    std::string line;
    while(std::getline(in, line))
    {
        line = trim(line);
        if(line.empty() || line[0] == '#')
            continue;
        auto eq = line.find('=');
        if(eq == std::string::npos)
            continue;
        out[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return out;
}

void load_config(Config& cfg, const fs::path& path)
{
    auto kv = read_key_values(path);
    if(kv.count("build_dir"))
        cfg.buildDir = kv["build_dir"];
    if(kv.count("build_type"))
        cfg.buildType = kv["build_type"];
    if(kv.count("install_prefix"))
        cfg.installPrefix = kv["install_prefix"];
    if(kv.count("bin_dir"))
        cfg.binDir = kv["bin_dir"];
    if(kv.count("jobs"))
        cfg.jobs = kv["jobs"];
    if(kv.count("run_unit_tests"))
        cfg.runUnitTests = truthy(kv["run_unit_tests"]);
    if(kv.count("run_robot_tests"))
        cfg.runRobotTests = truthy(kv["run_robot_tests"]);
}

void save_config(const Config& cfg)
{
    fs::create_directories(cfg.buildDir);
    {
        std::ofstream out(config_path(cfg));
        out << "# Generated by mlang-config.\n";
        out << "build_dir=" << cfg.buildDir.string() << "\n";
        out << "build_type=" << cfg.buildType << "\n";
        out << "install_prefix=" << cfg.installPrefix << "\n";
        out << "bin_dir=" << cfg.binDir << "\n";
        out << "jobs=" << cfg.jobs << "\n";
        out << "run_unit_tests=" << (cfg.runUnitTests ? "ON" : "OFF") << "\n";
        out << "run_robot_tests=" << (cfg.runRobotTests ? "ON" : "OFF")
            << "\n";
    }
    {
        std::ofstream out(cache_path(cfg));
        out << "# Generated by mlang-config.\n";
        out << "set(CMAKE_BUILD_TYPE \"" << cfg.buildType
            << "\" CACHE STRING \"\" FORCE)\n";
        out << "set(BUILD_TESTS " << (cfg.runUnitTests ? "ON" : "OFF")
            << " CACHE BOOL \"\" FORCE)\n";
        out << "set(CMAKE_INSTALL_PREFIX \"" << cfg.installPrefix
            << "\" CACHE PATH \"\" FORCE)\n";
    }
}

class TerminalRawMode
{
public:
    TerminalRawMode()
    {
#ifdef _WIN32
        enable_console_output_mode();
#else
        if(::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &original) == 0)
        {
            termios raw = original;
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            active = (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0);
        }
#endif
    }

    TerminalRawMode(const TerminalRawMode&) = delete;
    TerminalRawMode& operator=(const TerminalRawMode&) = delete;

    ~TerminalRawMode()
    {
#ifdef _WIN32
        restore_console_output_mode();
#else
        if(active)
            ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
#endif
    }

private:
#ifndef _WIN32
    termios original{};
#endif
    bool active = false;
};

bool stdin_is_tty()
{
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return ::isatty(STDIN_FILENO) != 0;
#endif
}

void write_stdout(std::string_view text)
{
    std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void flush_stdout()
{
    std::cout.flush();
}

int read_key()
{
    if(gPendingKey != 0)
    {
        const int key = gPendingKey;
        gPendingKey = 0;
        return key;
    }
#ifdef _WIN32
    int c = _getch();
    if(c == 0 || c == 224)
    {
        int next = _getch();
        if(next == 72)
            return kKeyUp;
        if(next == 80)
            return kKeyDown;
        if(next == 75)
            return kKeyLeft;
        if(next == 77)
            return kKeyRight;
    }
    return c;
#else
    unsigned char c = 0;
    const ssize_t n = ::read(STDIN_FILENO, &c, 1);
    if(n != 1)
    {
        if(n == -1 && errno == EINTR && gResizePending)
        {
            gResizePending = 0;
            return kKeyResize;
        }
        return 0;
    }
    if(c == 27)
    {
        unsigned char seq[2]{};
        const int flags = ::fcntl(STDIN_FILENO, F_GETFL, 0);
        if(flags != -1)
            ::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        if(::read(STDIN_FILENO, &seq[0], 1) == 1)
        {
            if(seq[0] != '[')
                gPendingKey = seq[0];
            else if(::read(STDIN_FILENO, &seq[1], 1) == 1)
            {
                if(flags != -1)
                    ::fcntl(STDIN_FILENO, F_SETFL, flags);
                if(seq[1] == 'A')
                    return kKeyUp;
                if(seq[1] == 'B')
                    return kKeyDown;
                if(seq[1] == 'D')
                    return kKeyLeft;
                if(seq[1] == 'C')
                    return kKeyRight;
            }
        }
        if(flags != -1)
            ::fcntl(STDIN_FILENO, F_SETFL, flags);
        return kKeyEsc;
    }
    return c;
#endif
}

TerminalSize terminal_size()
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if(GetConsoleScreenBufferInfo(stdout_handle(), &info))
    {
        return {static_cast<int>(info.srWindow.Bottom - info.srWindow.Top + 1),
                static_cast<int>(info.srWindow.Right - info.srWindow.Left + 1)};
    }
    return {};
#else
    winsize size{};
    if(::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0)
    {
        TerminalSize result;
        if(size.ws_row > 0)
            result.rows = size.ws_row;
        if(size.ws_col > 0)
            result.cols = size.ws_col;
        return result;
    }
    return {};
#endif
}

std::vector<Section> make_sections()
{
    return {
        {"Target",
         "Build directory, CMake configuration, and build parallelism. "
         "Language features are always enabled.",
         true,
         {{ItemKind::Text,
           "Build directory",
           nullptr,
           nullptr,
           {},
           "Directory where CMake writes generated files and binaries."},
          {ItemKind::Choice,
           "Build type",
           nullptr,
           nullptr,
           kBuildTypes,
           "CMake build type. Release is the normal default."},
          {ItemKind::Text,
           "Parallel jobs",
           nullptr,
           &Config::jobs,
           {},
           "Parallel build jobs used by bootstrap/build scripts."}}},
        {"Install",
         "Install destinations used by bootstrap-managed tooling.",
         true,
         {{ItemKind::Text,
           "Install prefix",
           nullptr,
           &Config::installPrefix,
           {},
           "CMake install prefix. Defaults to ~/.local."},
          {ItemKind::Text,
           "Binary install directory",
           nullptr,
           &Config::binDir,
           {},
           "Directory where mlang, mlangd-mla, mlang-format, and related "
           "tool binaries are copied."}}},
        {"Tests",
         "Test tasks are opt-in. Unit tests and Robot tests stay off unless "
         "you enable them here.",
         true,
         {{ItemKind::Toggle,
           "Unit tests",
           &Config::runUnitTests,
           nullptr,
           {},
           "Enable bootstrap/unit-test tasks when generated config is used."},
          {ItemKind::Toggle,
           "Robot tests",
           &Config::runRobotTests,
           nullptr,
           {},
           "Enable Robot Framework example tests when generated config is "
           "used."}}},
    };
}

std::vector<VisibleRow> visible_rows(const std::vector<Section>& sections)
{
    std::vector<VisibleRow> rows;
    for(size_t section = 0; section < sections.size(); ++section)
    {
        rows.push_back({RowKind::Section, section, 0});
        if(!sections[section].open)
            continue;
        for(size_t item = 0; item < sections[section].items.size(); ++item)
            rows.push_back({RowKind::Item, section, item});
    }
    return rows;
}

std::string value_for(const Config& cfg, const Item& item)
{
    if(item.kind == ItemKind::Choice)
        return cfg.buildType;
    if(item.kind == ItemKind::Text)
    {
        if(item.label == "Build directory")
            return cfg.buildDir.string();
        return cfg.*(item.text);
    }
    return cfg.*(item.flag) ? "ON" : "OFF";
}

void set_text_value(Config& cfg, const Item& item, const std::string& value)
{
    if(item.label == "Build directory")
        cfg.buildDir = value;
    else if(item.text)
        cfg.*(item.text) = value;
}

std::string help_for(const Config&, const std::vector<Section>& sections,
                     const VisibleRow& row)
{
    if(row.kind == RowKind::Section)
        return sections[row.section].help;
    return sections[row.section].items[row.item].help;
}

int choice_index(const std::string& value,
                 const std::vector<std::string>& choices)
{
    for(size_t i = 0; i < choices.size(); ++i)
    {
        if(choices[i] == value)
            return static_cast<int>(i);
    }
    return 0;
}

std::vector<std::string> wrap_text(std::string_view text, int width)
{
    width = std::max(1, width);
    std::vector<std::string> lines;
    std::string current;
    size_t pos = 0;
    while(pos < text.size())
    {
        while(pos < text.size() && text[pos] == ' ')
            ++pos;
        size_t start = pos;
        while(pos < text.size() && text[pos] != ' ')
            ++pos;
        if(start == pos)
            break;
        std::string word(text.substr(start, pos - start));
        if(static_cast<int>(current.size() + word.size() +
                            (current.empty() ? 0 : 1)) > width)
        {
            if(!current.empty())
                lines.push_back(current);
            current = std::move(word);
        }
        else
        {
            if(!current.empty())
                current += ' ';
            current += word;
        }
    }
    if(!current.empty())
        lines.push_back(current);
    if(lines.empty())
        lines.push_back("");
    return lines;
}

int ansi_visible_width(std::string_view line)
{
    int width = 0;
    for(size_t i = 0; i < line.size();)
    {
        if(line[i] == '\x1b' && i + 1 < line.size() && line[i + 1] == '[')
        {
            i += 2;
            while(i < line.size() && line[i] != 'm')
                ++i;
            if(i < line.size())
                ++i;
            continue;
        }
        ++width;
        ++i;
    }
    return width;
}

std::string truncate_line(std::string line, int cols)
{
    if(cols <= 0)
        return {};
    std::string out;
    int width = 0;
    for(size_t i = 0; i < line.size() && width < cols;)
    {
        if(line[i] == '\x1b' && i + 1 < line.size() && line[i + 1] == '[')
        {
            const size_t start = i;
            i += 2;
            while(i < line.size() && line[i] != 'm')
                ++i;
            if(i < line.size())
                ++i;
            out.append(line, start, i - start);
            continue;
        }
        out.push_back(line[i]);
        ++width;
        ++i;
    }
    return out;
}

std::string pad_ansi_line(std::string line, int cols)
{
    const int width = ansi_visible_width(line);
    if(width < cols)
        line.append(static_cast<size_t>(cols - width), ' ');
    return line;
}

std::string row_text(const Config& cfg, const std::vector<Section>& sections,
                     const VisibleRow& row, bool selected, bool editing)
{
    std::string out = selected ? "> " : "  ";
    if(row.kind == RowKind::Section)
    {
        const Section& section = sections[row.section];
        out += kAnsiBlue;
        out += section.open ? "[-]" : "[+]";
        out += kAnsiFgDefault;
        out += ' ';
        out += section.label;
        return out;
    }

    const Item& item = sections[row.section].items[row.item];
    if(item.kind == ItemKind::Toggle)
    {
        out += "  ";
        out += kAnsiBlue;
        out += '[';
        if(cfg.*(item.flag))
        {
            out += kAnsiGreen;
            out += 'X';
        }
        else
            out += ' ';
        out += kAnsiBlue;
        out += ']';
        out += kAnsiFgDefault;
        out += ' ';
    }
    else if(item.kind == ItemKind::Text)
        out += "  >   ";
    else
        out += "      ";

    out += item.label;
    const int pad = std::max(1, 32 - static_cast<int>(item.label.size()));
    out += std::string(static_cast<size_t>(pad), ' ');
    if(editing && item.kind == ItemKind::Text)
    {
        out += kAnsiEditField;
        out += value_for(cfg, item);
        out += "\x1b[5m_\x1b[25m";
        out += kAnsiReset;
        if(selected)
            out += kAnsiCurrentLineBg;
    }
    else
        out += value_for(cfg, item);
    return out;
}

int list_content_rows_for_offset(int scrollOffset, int rowCount, int listHeight)
{
    if(rowCount <= 0 || listHeight <= 0)
        return 0;
    const int topMarkerRows = (scrollOffset > 0 && listHeight > 1) ? 1 : 0;
    int contentRows = std::max(1, listHeight - topMarkerRows);
    const bool needsBottomMarker =
        scrollOffset + contentRows < rowCount && contentRows > 1;
    if(needsBottomMarker)
        --contentRows;
    return std::max(1, contentRows);
}

int clamp_scroll(int scrollOffset, int cursor, int rowCount, int listHeight)
{
    if(rowCount <= 0)
        return 0;
    scrollOffset = std::clamp(scrollOffset, 0, rowCount - 1);
    cursor = std::clamp(cursor, 0, rowCount - 1);
    for(int i = 0; i < rowCount + 2; ++i)
    {
        const int contentRows =
            list_content_rows_for_offset(scrollOffset, rowCount, listHeight);
        if(cursor < scrollOffset)
            scrollOffset = cursor;
        else if(cursor >= scrollOffset + contentRows)
            scrollOffset = cursor - contentRows + 1;
        else
            break;
        scrollOffset = std::clamp(scrollOffset, 0, rowCount - 1);
    }
    return scrollOffset;
}

void append_wrapped_fixed(std::vector<std::string>& out, std::string_view text,
                          int rows, int cols)
{
    const std::vector<std::string> lines = wrap_text(text, cols);
    for(int i = 0; i < rows; ++i)
    {
        if(i < static_cast<int>(lines.size()))
            out.push_back(truncate_line(lines[static_cast<size_t>(i)], cols));
        else
            out.push_back("");
    }
}

Layout make_layout(TerminalSize screen)
{
    Layout layout;
    layout.keyRows = static_cast<int>(
        wrap_text("j/k/arrows move  h/left close  l/right open  "
                  "space/enter change  s save  q quit",
                  screen.cols)
            .size());
    layout.outputRows = static_cast<int>(
        wrap_text("Output: build/mlang-config.conf and "
                  "build/mlang_config_cache.cmake",
                  screen.cols)
            .size());
    const int fixed = layout.titleRows + layout.keyRows + layout.spacerRows +
                      layout.docTitleRows + layout.outputRows +
                      layout.messageRows;
    const int available = std::max(3, screen.rows - fixed);
    layout.docRows = std::max(2, std::min(5, available / 3));
    layout.listRows = std::max(1, available - layout.docRows);
    return layout;
}

void draw(const Config& cfg, const std::vector<Section>& sections, int cursor,
          int scrollOffset, TerminalSize screen, std::string_view message,
          bool editingText)
{
    static std::vector<std::string> previousScreenLines;
    static TerminalSize previousScreen{};

    const std::vector<VisibleRow> rows = visible_rows(sections);
    const int safeCursor =
        std::clamp(cursor, 0, std::max(0, static_cast<int>(rows.size()) - 1));
    const Layout layout = make_layout(screen);
    const int rowCount = static_cast<int>(rows.size());

    std::vector<std::string> screenLines;
    screenLines.push_back("\x1b[1;36m" +
                          truncate_line("mlang build configurator",
                                        screen.cols) +
                          "\x1b[0m");
    append_wrapped_fixed(screenLines,
                         "j/k/arrows move  h/left close  l/right open  "
                         "space/enter change  s save  q quit",
                         layout.keyRows, screen.cols);
    screenLines.push_back("");

    int listRowsPrinted = 0;
    if(scrollOffset > 0 && layout.listRows > 1)
    {
        screenLines.push_back(truncate_line("  ...", screen.cols));
        ++listRowsPrinted;
    }

    const int contentRows =
        list_content_rows_for_offset(scrollOffset, rowCount, layout.listRows);
    const int endRow = std::min(rowCount, scrollOffset + contentRows);
    const bool bottomMarker =
        endRow < rowCount && listRowsPrinted + contentRows < layout.listRows;
    for(int i = scrollOffset; i < endRow && listRowsPrinted < layout.listRows;
        ++i)
    {
        std::string line = truncate_line(
            row_text(cfg, sections, rows[static_cast<size_t>(i)],
                     i == safeCursor, editingText && i == safeCursor),
            screen.cols);
        if(i == safeCursor)
        {
            line = pad_ansi_line(std::move(line), screen.cols);
            line = std::string(kAnsiCurrentLineBg) + line +
                   std::string(kAnsiReset);
        }
        screenLines.push_back(std::move(line));
        ++listRowsPrinted;
    }
    if(bottomMarker && listRowsPrinted < layout.listRows)
    {
        screenLines.push_back(truncate_line("  ...", screen.cols));
        ++listRowsPrinted;
    }
    for(int i = listRowsPrinted; i < layout.listRows; ++i)
        screenLines.push_back("");

    screenLines.push_back("\x1b[1mDocumentation\x1b[0m");
    if(!rows.empty())
        append_wrapped_fixed(screenLines,
                             help_for(cfg, sections,
                                      rows[static_cast<size_t>(safeCursor)]),
                             layout.docRows, screen.cols);

    append_wrapped_fixed(screenLines,
                         "Output: build/mlang-config.conf and "
                         "build/mlang_config_cache.cmake",
                         layout.outputRows, screen.cols);
    append_wrapped_fixed(screenLines, message, layout.messageRows, screen.cols);

    while(static_cast<int>(screenLines.size()) < screen.rows)
        screenLines.push_back("");
    if(static_cast<int>(screenLines.size()) > screen.rows)
        screenLines.resize(static_cast<size_t>(screen.rows));

    auto cursor_position = [](size_t row) -> std::string
    { return "\x1b[" + std::to_string(row + 1) + ";1H"; };

    const bool fullRedraw =
        previousScreenLines.empty() || previousScreen.rows != screen.rows ||
        previousScreen.cols != screen.cols ||
        previousScreenLines.size() != screenLines.size();

    std::string output;
    output += "\x1b[?25l";
    if(fullRedraw)
    {
        output += "\x1b[H\x1b[2J";
        for(size_t i = 0; i < screenLines.size(); ++i)
        {
            if(i > 0)
                output += "\n";
            output += screenLines[i];
        }
    }
    else
    {
        for(size_t i = 0; i < screenLines.size(); ++i)
        {
            if(screenLines[i] == previousScreenLines[i])
                continue;
            output += cursor_position(i);
            output += "\x1b[K";
            output += screenLines[i];
        }
    }
    write_stdout(output);
    flush_stdout();

    previousScreenLines = std::move(screenLines);
    previousScreen = screen;
}

std::string prompt(const std::string& label, const std::string& current)
{
    std::cout << label << " [" << current << "]: ";
    std::string line;
    std::getline(std::cin, line);
    line = trim(line);
    return line.empty() ? current : line;
}

void print_summary(const Config& cfg)
{
    std::cout << "\nMLang configuration\n";
    std::cout << "  build dir       " << cfg.buildDir.string() << "\n";
    std::cout << "  build type      " << cfg.buildType << "\n";
    std::cout << "  install prefix  " << cfg.installPrefix << "\n";
    std::cout << "  bin dir         " << cfg.binDir << "\n";
    std::cout << "  jobs            " << cfg.jobs << "\n";
    std::cout << "  unit tests      " << (cfg.runUnitTests ? "ON" : "OFF")
              << "\n";
    std::cout << "  Robot tests     " << (cfg.runRobotTests ? "ON" : "OFF")
              << "\n";
    std::cout << "\nLanguage features are always enabled.\n";
}

void choose_build_type(Config& cfg)
{
    const std::vector<std::string> values = {"Release", "Debug",
                                             "RelWithDebInfo", "MinSizeRel"};
    std::cout << "\nBuild type\n";
    for(size_t i = 0; i < values.size(); ++i)
        std::cout << "  " << (i + 1) << ". " << values[i] << "\n";
    std::cout << "Select [current " << cfg.buildType << "]: ";
    std::string line;
    std::getline(std::cin, line);
    if(line.empty())
        return;
    int idx = std::atoi(line.c_str());
    if(idx >= 1 && static_cast<size_t>(idx) <= values.size())
        cfg.buildType = values[static_cast<size_t>(idx - 1)];
}

void run_menu(Config& cfg)
{
    if(!stdin_is_tty())
    {
        for(;;)
        {
            print_summary(cfg);
            std::cout << "\nMenu\n";
            std::cout << "  1. Edit build directory\n";
            std::cout << "  2. Select build type\n";
            std::cout << "  3. Edit install prefix\n";
            std::cout << "  4. Edit binary install directory\n";
            std::cout << "  5. Edit parallel jobs\n";
            std::cout << "  6. Toggle unit tests\n";
            std::cout << "  7. Toggle Robot tests\n";
            std::cout << "  s. Save and exit\n";
            std::cout << "  q. Quit without saving\n";
            std::cout << "> ";

            std::string choice;
            std::getline(std::cin, choice);
            if(choice == "1")
                cfg.buildDir = prompt("Build directory", cfg.buildDir.string());
            else if(choice == "2")
                choose_build_type(cfg);
            else if(choice == "3")
                cfg.installPrefix = prompt("Install prefix", cfg.installPrefix);
            else if(choice == "4")
                cfg.binDir = prompt("Binary install directory", cfg.binDir);
            else if(choice == "5")
                cfg.jobs = prompt("Parallel jobs", cfg.jobs);
            else if(choice == "6")
                cfg.runUnitTests = !cfg.runUnitTests;
            else if(choice == "7")
                cfg.runRobotTests = !cfg.runRobotTests;
            else if(choice == "s" || choice == "S")
            {
                save_config(cfg);
                std::cout << "Wrote " << config_path(cfg) << "\n";
                std::cout << "Wrote " << cache_path(cfg) << "\n";
                return;
            }
            else if(choice == "q" || choice == "Q")
                return;
        }
    }

#ifndef _WIN32
    struct sigaction sa {};
    sa.sa_handler = handle_resize;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, nullptr);
#endif

    TerminalRawMode raw;
    std::vector<Section> sections = make_sections();
    int cursor = 0;
    int scrollOffset = 0;
    bool editingText = false;
    std::string message =
        "Language features are always enabled. Unit and Robot tests default "
        "to OFF.";

    for(;;)
    {
        const TerminalSize screen = terminal_size();
        const std::vector<VisibleRow> rows = visible_rows(sections);
        const Layout layout = make_layout(screen);
        cursor = std::clamp(cursor, 0,
                            std::max(0, static_cast<int>(rows.size()) - 1));
        scrollOffset = clamp_scroll(scrollOffset, cursor,
                                    static_cast<int>(rows.size()),
                                    layout.listRows);
        draw(cfg, sections, cursor, scrollOffset, screen, message,
             editingText);

        int key = read_key();
        if(key == kKeyResize)
            continue;

        VisibleRow row = rows[static_cast<size_t>(cursor)];
        Item* item = nullptr;
        if(row.kind == RowKind::Item)
            item = &sections[row.section].items[row.item];

        if(editingText)
        {
            if(key == '\r' || key == '\n')
            {
                editingText = false;
                message = "Updated " + item->label + ".";
            }
            else if(key == kKeyEsc)
            {
                editingText = false;
                message = "Edit finished.";
            }
            else if((key == 127 || key == 8) && item)
            {
                std::string value = value_for(cfg, *item);
                if(!value.empty())
                    value.pop_back();
                set_text_value(cfg, *item, value);
            }
            else if(key >= 32 && key < 127 && item)
            {
                std::string value = value_for(cfg, *item);
                value.push_back(static_cast<char>(key));
                set_text_value(cfg, *item, value);
            }
            continue;
        }

        if(key == kKeyUp || key == 'k')
            --cursor;
        else if(key == kKeyDown || key == 'j')
            ++cursor;
        else if(key == kKeyLeft || key == 'h')
        {
            if(row.kind == RowKind::Section)
                sections[row.section].open = false;
        }
        else if(key == kKeyRight || key == 'l')
        {
            if(row.kind == RowKind::Section)
                sections[row.section].open = true;
        }
        else if(key == ' ' || key == '\r' || key == '\n')
        {
            if(row.kind == RowKind::Section)
                sections[row.section].open = !sections[row.section].open;
            else if(item && item->kind == ItemKind::Toggle)
            {
                cfg.*(item->flag) = !(cfg.*(item->flag));
                message = "Toggled " + item->label + ".";
            }
            else if(item && item->kind == ItemKind::Choice)
            {
                int idx = choice_index(cfg.buildType, item->choices);
                idx = (idx + 1) % static_cast<int>(item->choices.size());
                cfg.buildType = item->choices[static_cast<size_t>(idx)];
                message = "Selected build type " + cfg.buildType + ".";
            }
            else if(item && item->kind == ItemKind::Text)
            {
                editingText = true;
                message = "Editing " + item->label +
                          ". Type text, Backspace deletes, Enter finishes.";
            }
        }
        else if(key == 's' || key == 'S')
        {
            save_config(cfg);
            write_stdout("\x1b[?25h\x1b[0m\n");
            std::cout << "Wrote " << config_path(cfg) << "\n";
            std::cout << "Wrote " << cache_path(cfg) << "\n";
            return;
        }
        else if(key == 'q' || key == 'Q' || key == kKeyEsc)
        {
            write_stdout("\x1b[?25h\x1b[0m\n");
            return;
        }
    }
}

void print_help(const char* argv0)
{
    std::cout << "Usage: " << argv0 << " [options]\n"
              << "  --build-dir DIR       Build directory\n"
              << "  --build-type TYPE     Release, Debug, RelWithDebInfo, MinSizeRel\n"
              << "  --install-prefix DIR  Install prefix, default ~/.local\n"
              << "  --bin-dir DIR         Binary install directory, default ~/.local/bin\n"
              << "  --jobs N              Parallel build jobs\n"
              << "  --unit-tests on|off   Save unit-test preference, default off\n"
              << "  --robot-tests on|off  Save Robot-test preference, default off\n"
              << "  --write               Write config without opening the menu\n"
              << "  --print               Print current config and exit\n";
}
}

int main(int argc, char** argv)
{
    Config cfg;
    cfg.jobs = default_jobs();

    fs::path importPath;
    bool writeOnly = false;
    bool printOnly = false;

    for(int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        auto needValue = [&](const std::string& name) -> std::string {
            if(i + 1 >= argc)
            {
                std::cerr << name << " requires a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if(arg == "--help" || arg == "-h")
        {
            print_help(argv[0]);
            return 0;
        }
        else if(arg == "--build-dir")
            cfg.buildDir = needValue(arg);
        else if(arg == "--build-type")
            cfg.buildType = needValue(arg);
        else if(arg == "--install-prefix")
            cfg.installPrefix = needValue(arg);
        else if(arg == "--bin-dir")
            cfg.binDir = needValue(arg);
        else if(arg == "--jobs")
            cfg.jobs = needValue(arg);
        else if(arg == "--unit-tests")
            cfg.runUnitTests = truthy(needValue(arg));
        else if(arg == "--robot-tests")
            cfg.runRobotTests = truthy(needValue(arg));
        else if(arg == "--import")
            importPath = needValue(arg);
        else if(arg == "--write")
            writeOnly = true;
        else if(arg == "--print")
            printOnly = true;
        else
        {
            std::cerr << "unknown option: " << arg << "\n";
            return 2;
        }
    }

    fs::path defaultConfig = config_path(cfg);
    if(!importPath.empty())
        load_config(cfg, importPath);
    else if(fs::exists(defaultConfig))
        load_config(cfg, defaultConfig);

    for(int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        auto needValue = [&](const std::string&) -> std::string {
            return (i + 1 < argc) ? argv[++i] : std::string();
        };
        if(arg == "--build-dir")
            cfg.buildDir = needValue(arg);
        else if(arg == "--build-type")
            cfg.buildType = needValue(arg);
        else if(arg == "--install-prefix")
            cfg.installPrefix = needValue(arg);
        else if(arg == "--bin-dir")
            cfg.binDir = needValue(arg);
        else if(arg == "--jobs")
            cfg.jobs = needValue(arg);
        else if(arg == "--unit-tests")
            cfg.runUnitTests = truthy(needValue(arg));
        else if(arg == "--robot-tests")
            cfg.runRobotTests = truthy(needValue(arg));
        else if(arg == "--import")
            (void)needValue(arg);
    }

    if(printOnly)
    {
        print_summary(cfg);
        return 0;
    }
    if(writeOnly)
    {
        save_config(cfg);
        std::cout << "Wrote " << config_path(cfg) << "\n";
        std::cout << "Wrote " << cache_path(cfg) << "\n";
        return 0;
    }

    run_menu(cfg);
    return 0;
}
