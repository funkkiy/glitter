#pragma once

#include <optional>
#include <string>

namespace Glitter::Util {

std::optional<std::string> ReadFile(const char* filePath);

} // namespace Glitter::Util
