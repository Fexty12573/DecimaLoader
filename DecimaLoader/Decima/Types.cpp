#include "pch.h"
#include "Types.h"
#include "String.h"

#include "../PatternScan.h"

#include <spdlog/spdlog.h>


void DecimaTypeDb::initialize() {
    const auto string_view_assign = PatternScanner::find_first(
        Pattern::from_string("41 B8 FF FF FF FF F0 44 0F C1 03 41 0F BA F0 1F 41 83 F8 01")
    );

    if (!string_view_assign) {
        spdlog::error("Failed to find StringView::assign pattern");
        return;
    }

    StringView::assign = (decltype(StringView::assign))(string_view_assign - 70);

    const auto string_view_release = PatternScanner::find_first(
        Pattern::from_string("40 53 48 83 EC 20 48 8B 19 48 8D 05 ? ? ? ? 48 83 EB 10 48 3B D8")
    );

    if (!string_view_release) {
        spdlog::error("Failed to find StringView::release pattern");
        return;
    }

    StringView::release = (decltype(StringView::release))(string_view_release);

    const auto string_view_copy = PatternScanner::find_first(
        Pattern::from_string("48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 19 48 8B F2")
    );

    if (!string_view_copy) {
        spdlog::error("Failed to find StringView::copy pattern");
        return;
    }

    StringView::copy = (decltype(StringView::copy))(string_view_copy);
}
