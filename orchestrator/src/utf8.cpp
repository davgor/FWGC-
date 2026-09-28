#include "utf8.hpp"

#include <cstddef>
#include <cstdint>

namespace orchestrator {

bool is_utf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        std::size_t follow = 0;
        std::uint32_t point = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            follow = 1;
            point = lead & 0x1Fu;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            follow = 2;
            point = lead & 0x0Fu;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            follow = 3;
            point = lead & 0x07u;
        } else {
            return false;  // a continuation byte first, an overlong two-byte lead, or past U+10FFFF
        }
        if (text.size() - i <= follow) return false;
        for (std::size_t k = 1; k <= follow; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0u) != 0x80u) return false;
            point = (point << 6) | (next & 0x3Fu);
        }
        if ((follow == 2 && point < 0x800u) || (follow == 3 && (point < 0x10000u || point > 0x10FFFFu))) return false;
        if (point >= 0xD800u && point <= 0xDFFFu) return false;
        i += follow + 1;
    }
    return true;
}

}  // namespace orchestrator
