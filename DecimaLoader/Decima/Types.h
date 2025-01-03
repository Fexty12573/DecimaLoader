#pragma once

#include "String.h"
#include "Array.h"

#include <fmt/format.h>
#include <format>
#include <ranges>

class DecimaTypeDb {
public:
    static void initialize();

};

template<>
struct std::hash<GUID> {
    std::size_t operator()(const GUID& g) const noexcept {
        using std::size_t;
        using std::hash;

        return hash<uint32_t>()(g.Data1) ^ hash<uint16_t>()(g.Data2) ^ hash<uint16_t>()(g.Data3) ^ hash<uint64_t>()(*(uint64_t*)&g.Data4);
    }
};

template<>
struct fmt::formatter<GUID> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
    template<typename FormatContext>
    auto format(const GUID& g, FormatContext& ctx) {
        return fmt::format_to(ctx.out(), "{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}",
            g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    }
};

template<>
struct std::formatter<GUID> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
    template<typename FormatContext>
    auto format(const GUID& g, FormatContext& ctx) const {
        return format_to(ctx.out(), "{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}",
            g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    }
};

//template<typename T> requires std::integral<std::ranges::range_value_t<T>>
//struct fmt::formatter<T> {
//    char presentation = 'd';
//
//    constexpr auto parse(fmt::format_parse_context& ctx) -> decltype(ctx.begin()) {
//        auto it = ctx.begin();
//        const auto end = ctx.end();
//        if (it != end && (*it == 'd' || *it == 'x' || *it == 'o')) {
//            presentation = *it++;
//        }
//        if (it != end && *it != '}') {
//            throw fmt::format_error("invalid format");
//        }
//        return it;
//    }
//
//    template <typename FormatContext>
//    auto format(const T& view, FormatContext& ctx) -> decltype(ctx.out()) {
//        auto out = ctx.out();
//        out = fmt::format_to(out, "[");
//        bool first = true;
//
//        for (const auto& val : view) {
//            if (!first) {
//                out = fmt::format_to(out, ", ");
//            }
//            first = false;
//            // Format each integer based on the presentation.
//            switch (presentation) {
//            case 'd': out = fmt::format_to(out, "{}", val); break;
//            case 'x': out = fmt::format_to(out, "{:x}", val); break;
//            case 'o': out = fmt::format_to(out, "{:o}", val); break;
//            default: throw fmt::format_error("invalid format");
//            }
//        }
//
//        out = fmt::format_to(out, "]");
//        return out;
//    }
//};
