#pragma once

#include <string>
#include <vector>
#include <termios.h>

namespace cnc {
class TerminalUI {
public:
    explicit TerminalUI(bool fixed_screen);
    ~TerminalUI();
    TerminalUI(const TerminalUI&) = delete;
    TerminalUI& operator=(const TerminalUI&) = delete;
    bool fixed() const { return fixed_; }
    std::vector<std::string> read_commands(int timeout_ms);
    void draw(const std::string& status, const std::string& message);
private:
    bool fixed_{}, raw_{}, eof_{}, discard_{};
    unsigned escape_{};
    termios original_{};
    std::string input_;
};
} // namespace cnc
