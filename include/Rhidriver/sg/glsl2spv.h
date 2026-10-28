#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rhi {
namespace sg {

bool GlslToSpirv(unsigned stage, const std::string& source, std::vector<uint32_t>& out, std::string& log);

}
}
