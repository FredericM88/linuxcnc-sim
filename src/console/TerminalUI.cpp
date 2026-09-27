#include "console/TerminalUI.hpp"

#include <algorithm>
#include <cerrno>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <system_error>
#include <sys/ioctl.h>
#include <unistd.h>

namespace cnc {
TerminalUI::TerminalUI(bool fixed_screen) : fixed_(fixed_screen && isatty(0) && isatty(1)) {
    if (fixed_) {
        if (tcgetattr(0, &original_) < 0) throw std::system_error(errno, std::generic_category(), "tcgetattr");
        auto settings = original_;
        settings.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        settings.c_iflag &= static_cast<tcflag_t>(~(IXON | IXOFF));
        settings.c_cc[VMIN] = 1;
        settings.c_cc[VTIME] = 0;
        if (tcsetattr(0, TCSANOW, &settings) < 0) throw std::system_error(errno, std::generic_category(), "tcsetattr");
        raw_ = true;
        std::cout << "\033[?1049h\033[2J\033[H" << std::flush;
    }
}
TerminalUI::~TerminalUI() {
    if (raw_) tcsetattr(0, TCSANOW, &original_);
    if (fixed_) std::cout << "\033[0m\033[?25h\033[?1049l" << std::flush;
}
std::vector<std::string> TerminalUI::read_commands(int timeout_ms) {
    pollfd descriptor{eof_ ? -1 : 0, POLLIN, 0};
    const int ready = poll(&descriptor, 1, timeout_ms);
    if (ready < 0 && errno != EINTR) throw std::system_error(errno, std::generic_category(), "console poll");
    std::vector<std::string> commands;
    if (ready <= 0) return commands;
    char buffer[4096];
    const auto count = read(0, buffer, sizeof(buffer));
    if (count == 0 || (descriptor.revents & POLLNVAL)) { eof_ = true; return commands; }
    if (count < 0) {
        if (errno == EINTR || errno == EAGAIN) return commands;
        throw std::system_error(errno, std::generic_category(), "console read");
    }
    for (ssize_t i = 0; i < count; ++i) {
        const auto c = static_cast<unsigned char>(buffer[i]);
        if (escape_) {
            if (escape_ == 1 && (c == '[' || c == 'O')) escape_ = 2;
            else if (escape_ == 1 || (c >= 0x40 && c <= 0x7e)) escape_ = 0;
            continue;
        }
        if (c == 27) { escape_ = 1; continue; }
        if (c == '\n' || c == '\r') {
            commands.push_back(discard_ ? "__input_error__" : std::move(input_));
            input_.clear();
            discard_ = false;
        } else if (c == 127 || c == 8) {
            if (!input_.empty()) {
                auto end = input_.size() - 1;
                while (end && (static_cast<unsigned char>(input_[end]) & 0xc0) == 0x80) --end;
                input_.resize(end);
            }
        } else if (c == 21) { input_.clear(); discard_ = false; }
        else if (c == 4 && input_.empty()) commands.emplace_back("quit");
        else if (c >= 32 || c == '\t') {
            if (input_.size() < 8192 && !discard_) input_ += static_cast<char>(c);
            else discard_ = true;
        } else discard_ = true;
    }
    return commands;
}
void TerminalUI::draw(const std::string& status, const std::string& message) {
    if (!fixed_) return;
    winsize size{};
    ioctl(1, TIOCGWINSZ, &size);
    const unsigned rows = size.ws_row ? size.ws_row : 24;
    const unsigned cols = size.ws_col ? size.ws_col : 80;
    std::vector<std::string> lines;
    std::istringstream stream(status);
    for (std::string line; std::getline(stream, line);) lines.push_back(std::move(line));
    std::istringstream messages(message);
    std::vector<std::string> reply;
    for (std::string line; std::getline(messages, line);) reply.push_back(std::move(line));
    // Keep complete command output visible on ordinary 80x24 terminals.
    // The live dashboard yields rows to longer replies such as workpiece show.
    if (rows >= reply.size() + 2 && lines.size() + reply.size() + 2 > rows) {
        const auto keep = rows - reply.size() - 2;
        const auto prefix = std::min(std::size_t{2}, keep);
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(prefix),
                    lines.end() - static_cast<std::ptrdiff_t>(keep - prefix));
    }
    lines.insert(lines.end(), reply.begin(), reply.end());
    if (rows < lines.size() + 2 || cols < 60) lines = {"Terminal too small; enlarge to at least 80 x 24."};
    lines.emplace_back();
    // Use an ASCII display of input (UTF-8 bytes remain intact in the actual command).
    std::string input = input_;
    const std::size_t room = cols > 12 ? cols - 12 : 0;
    if (input.size() > room) input = input.substr(input.size() - room);
    lines.push_back("Command > " + input);
    auto safe = [cols](std::string line) {
        for (char& c : line) if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) >= 127) c = '?';
        line.resize(std::min(line.size(), static_cast<std::size_t>(cols > 1 ? cols - 1 : 0)));
        return line;
    };
    std::ostringstream frame;
    frame << "\033[?25l";
    for (unsigned row = 0; row < rows; ++row)
        frame << "\033[" << row + 1 << ";1H\033[2K" << (row < lines.size() ? safe(lines[row]) : "");
    const auto prompt_row = std::min(rows, static_cast<unsigned>(lines.size()));
    frame << "\033[" << prompt_row << ';' << safe(lines.back()).size() + 1 << "H\033[?25h";
    std::cout << frame.str() << std::flush;
}
} // namespace cnc
