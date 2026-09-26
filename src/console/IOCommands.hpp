#pragma once
#include <string>
#include <vector>
#include "io/VirtualSensors.hpp"

namespace cnc {
IOCommand parse_io_command(const std::string& line);
std::vector<IOCommand> load_io_config(const std::string& filename);
std::string describe_io(const IOCommand& command, const IOStatus& status);
std::string compact_io(const IOStatus& status);
} // namespace cnc
