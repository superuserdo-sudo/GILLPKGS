#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>

namespace fs = std::filesystem;

class Giga {
    std::vector<std::string> lines{std::string{}};
    std::string filename;
    struct termios saved{};
    bool raw = false, dirty = false, running = true, selecting = false;
    size_t row = 0, col = 0, top = 0;
    int screenRows = 24, screenCols = 80;

    enum { NONE = 1000, ESC, ALT_ESC, UP, DOWN, LEFT, RIGHT, HOME, END, DEL, PGUP, PGDN };

    void rawMode(bool enable) {
        if (enable && !raw) {
            if (tcgetattr(STDIN_FILENO, &saved) != 0) return;
            termios t = saved;
            t.c_lflag &= ~(ECHO | ICANON | IEXTEN);
            t.c_iflag &= ~(IXON | ICRNL);
            t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
            if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) == 0) raw = true;
        } else if (!enable && raw) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved); raw = false;
        }
    }

    int byteRead(int ms = -1) {
        fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
        timeval tv{}; timeval* p = nullptr;
        if (ms >= 0) { tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000; p = &tv; }
        int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, p);
        if (ready <= 0) return -1;
        unsigned char ch;
        return ::read(STDIN_FILENO, &ch, 1) == 1 ? ch : -1;
    }

    int keyRead() {
        int c = byteRead(); if (c < 0) return NONE;
        if (c != 27) return c;
        int n = byteRead(70);
        if (n < 0) return ESC;
        if (n == 27) return ALT_ESC;
        if (n == '[' || n == 'O') {
            int k = byteRead(70);
            if (k == 'A') return UP;
            if (k == 'B') return DOWN;
            if (k == 'C') return RIGHT;
            if (k == 'D') return LEFT;
            if (k == 'H') return HOME;
            if (k == 'F') return END;
            if (k >= '0' && k <= '9') {
                int end = byteRead(70);
                if (end == '~') { if (k == '3') return DEL; if (k == '5') return PGUP; if (k == '6') return PGDN; }
            }
            return NONE;
        }
        if (n >= 'A' && n <= 'Z') n += 'a' - 'A';
        return 2000 + n;
    }

    void dimensions() {
        winsize w{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row && w.ws_col) {
            screenRows = w.ws_row; screenCols = w.ws_col;
        }
    }
    void moveTo(int r, int c) { std::cout << "\033[" << r << ';' << c << 'H'; }
    void clear() { std::cout << "\033[2J\033[H"; }
    static bool contains(const std::vector<std::string>& v, const std::string& s) {
        return std::find(v.begin(), v.end(), s) != v.end();
    }

    std::string language() const {
        std::string ext = fs::path(filename).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
        if (ext == ".py") return "python";
        if (ext == ".c" || ext == ".h") return "c";
        if (ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".hpp" || ext == ".hh") return "cpp";
        if (ext == ".go") return "go";
        if (ext == ".sh" || ext == ".bash" || ext == ".zsh") return "shell";
        if (ext == ".gsh") return "gsh";
        if (ext == ".rs") return "rust";
        if (ext == ".js" || ext == ".jsx" || ext == ".mjs") return "javascript";
        if (ext == ".ts" || ext == ".tsx") return "typescript";
        if (ext == ".java") return "java";
        if (ext == ".json") return "json";
        if (ext == ".html" || ext == ".htm" || ext == ".xml") return "html";
        if (ext == ".css") return "css";
        if (ext == ".lua") return "lua";
        if (ext == ".nix") return "nix";
        return "plain";
    }

    bool keyword(const std::string& s, const std::string& lang) const {
        static const std::vector<std::string> common = {
            "if","else","for","while","do","switch","case","break","continue","return","func","fn","let","mut","const","static","struct","class","public","private","protected","import","from","as","package","var","type","interface","impl","trait","match","enum","use","pub","mod","def","elif","try","except","finally","with","lambda","yield","async","await","true","false","True","False","None","null","nil","self","this","new","delete","namespace","using","include","int","char","float","double","void","bool","auto","string","print","println","echo","export","default","function","extends","throws","throw","fn"
        };
        (void)lang;
        return contains(common, s);
    }

    void paintLine(const std::string& s) {
        const std::string lang = language();
        bool inString = false; char quote = 0;
        for (size_t i = 0; i < s.size();) {
            if (!inString && ((s[i] == '#' && (lang == "python" || lang == "shell" || lang == "gsh" || lang == "nix")) ||
                (s[i] == '/' && i + 1 < s.size() && s[i+1] == '/' && lang != "plain" && lang != "shell" && lang != "gsh"))) {
                std::cout << "\033[90m" << s.substr(i) << "\033[0m"; return;
            }
            if (!inString && s[i] == '/' && i + 1 < s.size() && s[i+1] == '*' && (lang == "c" || lang == "cpp" || lang == "go" || lang == "rust" || lang == "javascript" || lang == "typescript" || lang == "java" || lang == "css")) {
                std::cout << "\033[90m" << s.substr(i) << "\033[0m"; return;
            }
            if (s[i] == '"' || s[i] == '\'' || (lang == "python" && s[i] == '`')) {
                if (!inString) { inString = true; quote = s[i]; std::cout << "\033[32m" << s[i] << "\033[0m"; ++i; continue; }
                if (s[i] == quote) { inString = false; std::cout << "\033[32m" << s[i] << "\033[0m"; ++i; continue; }
            }
            if (inString) { std::cout << "\033[32m" << s[i] << "\033[0m"; ++i; continue; }
            if (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_') {
                size_t j = i + 1;
                while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
                std::string word = s.substr(i, j-i);
                if (keyword(word, lang)) std::cout << "\033[1;34m" << word << "\033[0m";
                else if (j < s.size() && s[j] == '(') std::cout << "\033[36m" << word << "\033[0m";
                else std::cout << word;
                i = j; continue;
            }
            if (std::isdigit(static_cast<unsigned char>(s[i]))) {
                size_t j = i + 1;
                while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == '_')) ++j;
                std::cout << "\033[35m" << s.substr(i, j-i) << "\033[0m"; i = j; continue;
            }
            if (std::string("{}[]()").find(s[i]) != std::string::npos) std::cout << "\033[1;33m" << s[i] << "\033[0m";
            else std::cout << s[i];
            ++i;
        }
    }

    void draw() {
        dimensions(); clear();
        std::cout << "\033[1;36m GIGA \033[0m  " << (filename.empty() ? "[No name]" : filename)
                  << (dirty ? " \033[1;31m[Modified]\033[0m" : "")
                  << "  \033[90m" << language() << "\033[0m\n";
        std::cout << "\033[90m" << std::string(std::max(1, screenCols), '-') << "\033[0m\n";
        int contentRows = std::max(1, screenRows - 4);
        if (row < top) top = row;
        if (row >= top + static_cast<size_t>(contentRows)) top = row - contentRows + 1;
        for (int i = 0; i < contentRows; ++i) {
            size_t ix = top + static_cast<size_t>(i);
            if (ix < lines.size()) {
                if (ix == row) std::cout << "\033[1;36m>\033[0m";
                else std::cout << ' ';
                std::cout << "\033[90m" << ix + 1 << "\033[0m ";
                if (selecting && ix == row) std::cout << "\033[7m";
                paintLine(lines[ix]);
                if (selecting && ix == row) std::cout << "\033[0m";
            }
            std::cout << "\033[K\n";
        }
        std::cout << "\033[90mEsc Exit  Alt+H Help  Alt+S Save  Alt+I Open  Alt+F Files  Alt+L Select line\033[0m\033[K";
        moveTo(static_cast<int>(row - top) + 3, static_cast<int>(std::min(col, lines[row].size())) + 4);
        std::cout.flush();
    }

    std::string prompt(const std::string& message) {
        rawMode(false); std::string answer;
        std::cout << "\n" << message; std::getline(std::cin, answer);
        rawMode(true); return answer;
    }
    bool save() {
        if (filename.empty()) { filename = prompt("Save as (path): "); if (filename.empty()) return false; }
        std::ofstream out(filename);
        if (!out) { prompt("Cannot save file. Press Enter..."); return false; }
        for (size_t i = 0; i < lines.size(); ++i) { out << lines[i]; if (i + 1 < lines.size()) out << '\n'; }
        if (!out) { prompt("Write failed. Press Enter..."); return false; }
        dirty = false; return true;
    }
    bool openFile(const std::string& name) {
        std::ifstream in(name); if (!in) { prompt("Cannot open file. Press Enter..."); return false; }
        std::vector<std::string> temp; std::string s;
        while (std::getline(in, s)) temp.push_back(s);
        if (temp.empty()) temp.emplace_back();
        lines = std::move(temp); filename = name; row = col = top = 0; dirty = false; selecting = false; return true;
    }
    bool mayDiscard() {
        if (!dirty) return true;
        std::string ans = prompt("Save changes before exit? Type YES or NO: ");
        std::transform(ans.begin(), ans.end(), ans.begin(), [](unsigned char c){ return std::toupper(c); });
        if (ans == "YES") { if (!save()) return false; return true; }
        if (ans == "NO") return true;
        prompt("Please type YES or NO. Press Enter..."); return false;
    }
    void help() {
        rawMode(false);
        std::cout << "\nGiga Editor Help\n"
                  << "  Esc       Exit (asks to save if modified)\n"
                  << "  Alt+H     Help\n  Alt+S     Save\n  Alt+I     Open file by path\n"
                  << "  Alt+F     Pick a file from current directory\n  Alt+L     Toggle current-line selection\n"
                  << "  Arrows    Move cursor\n  Home/End  Start/end of line\n  Backspace Delete previous character\n"
                  << "  ) ] }     Insert matching closing bracket\n  Enter     New line with basic indentation\n\nPress Enter to return...";
        std::string dummy; std::getline(std::cin, dummy); rawMode(true);
    }
    void picker() {
        std::vector<fs::path> items;
        try { for (const auto& e : fs::directory_iterator(".")) items.push_back(e.path()); }
        catch (...) { prompt("Cannot read current directory. Press Enter..."); return; }
        std::sort(items.begin(), items.end()); rawMode(false);
        std::cout << "\nFiles in current directory:\n";
        for (size_t i=0; i<items.size(); ++i) {
            std::cout << i+1 << ". " << items[i].string();
            try { if (fs::is_directory(items[i])) std::cout << '/'; } catch (...) {}
            std::cout << '\n';
        }
        std::cout << "Enter number (0 cancel): "; std::string value; std::getline(std::cin, value); rawMode(true);
        try {
            size_t n = std::stoul(value);
            if (n == 0 || n > items.size()) return;
            if (fs::is_directory(items[n-1])) { prompt("Directories are not opened as text. Press Enter..."); return; }
            if (dirty) { std::string a = prompt("Unsaved changes. Type YES to discard: "); if (a != "YES" && a != "yes") return; }
            openFile(items[n-1].string());
        } catch (...) { }
    }
    void insertChar(char c) {
        lines[row].insert(col, 1, c); ++col; dirty = true; selecting = false;
        if (c == '(' || c == '[' || c == '{' || c == '"' || c == '\'') {
            char close = c == '(' ? ')' : c == '[' ? ']' : c == '{' ? '}' : c;
            lines[row].insert(col, 1, close);
        }
    }
    void newline() {
        std::string rest = lines[row].substr(col);
        std::string indent;
        for (char c : lines[row]) { if (c == ' ' || c == '\t') indent += c; else break; }
        if (col > 0 && (lines[row][col-1] == '{' || lines[row][col-1] == '[' || lines[row][col-1] == '(')) indent += "    ";
        lines[row].erase(col); lines.insert(lines.begin() + static_cast<long>(row+1), indent + rest);
        ++row; col = indent.size(); dirty = true; selecting = false;
    }
    void backspace() {
        if (col > 0) { lines[row].erase(col-1, 1); --col; dirty = true; }
        else if (row > 0) { col = lines[row-1].size(); lines[row-1] += lines[row]; lines.erase(lines.begin()+static_cast<long>(row)); --row; dirty = true; }
        selecting = false;
    }
    void deleteForward() {
        if (col < lines[row].size()) { lines[row].erase(col, 1); dirty = true; }
        else if (row + 1 < lines.size()) { lines[row] += lines[row+1]; lines.erase(lines.begin()+static_cast<long>(row+1)); dirty = true; }
        selecting = false;
    }
    void handle(int k) {
        if (k == ESC) { if (mayDiscard()) running = false; return; }
        if (k == ALT_ESC) { if (mayDiscard()) running = false; return; }
        if (k == 2000+'h') { help(); return; }
        if (k == 2000+'s') { save(); return; }
        if (k == 2000+'i') { std::string n = prompt("Open file path: "); if (!n.empty()) { if (!dirty || prompt("Unsaved changes. Type YES to discard: ") == "YES") openFile(n); } return; }
        if (k == 2000+'f') { picker(); return; }
        if (k == 2000+'l') { selecting = !selecting; return; }
        if (k == UP) { if (row > 0) --row; col = std::min(col, lines[row].size()); selecting = false; return; }
        if (k == DOWN) { if (row+1 < lines.size()) ++row; col = std::min(col, lines[row].size()); selecting = false; return; }
        if (k == LEFT) { if (col > 0) --col; else if (row > 0) { --row; col = lines[row].size(); } selecting = false; return; }
        if (k == RIGHT) { if (col < lines[row].size()) ++col; else if (row+1 < lines.size()) { ++row; col = 0; } selecting = false; return; }
        if (k == HOME) { col = 0; selecting = false; return; }
        if (k == END) { col = lines[row].size(); selecting = false; return; }
        if (k == PGUP) { row = row > 10 ? row-10 : 0; col = std::min(col, lines[row].size()); return; }
        if (k == PGDN) { row = std::min(row+10, lines.size()-1); col = std::min(col, lines[row].size()); return; }
        if (k == DEL) { deleteForward(); return; }
        if (k == 127 || k == 8) { backspace(); return; }
        if (k == '\r' || k == '\n') { newline(); return; }
        if (k == 9) { insertChar(' '); insertChar(' '); insertChar(' '); insertChar(' '); return; }
        if (k >= 32 && k <= 126) {
            char c = static_cast<char>(k);
            if (c == ')' || c == ']' || c == '}') {
                if (col < lines[row].size() && lines[row][col] == c) { ++col; return; }
            }
            insertChar(c); return;
        }
        if (k == 3) { if (mayDiscard()) running = false; }
    }
public:
    ~Giga() { rawMode(false); std::cout << "\033[0m\033[?25h\033[2J\033[H" << std::flush; }
    int run(int argc, char** argv) {
        if (argc > 1) { filename = argv[1]; std::ifstream test(filename); if (test) openFile(filename); else { lines = {""}; } }
        rawMode(true);
        std::cout << "\033[?25h";
        while (running) { draw(); handle(keyRead()); }
        return 0;
    }
};

int main(int argc, char** argv) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        std::cerr << "Giga needs an interactive terminal.\n"; return 1;
    }
    Giga editor;
    return editor.run(argc, argv);
}
