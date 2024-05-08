#include "pch.h"
#include "Memory.h"

#include <cstdint>
#include <cstdlib>

#include <windows.h>

bool FindSection(void* module, const char* name, Section* result) {
    PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)module;
    PIMAGE_NT_HEADERS64 nt_header = (PIMAGE_NT_HEADERS)((uint8_t*)module + dos_header->e_lfanew);
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt_header);

    for (WORD index = 0; index < nt_header->FileHeader.NumberOfSections; index++) {
        if (memcmp(section->Name, name, sizeof(section->Name)) == 0) {
            result->start = (uint8_t*)module + section->VirtualAddress;
            result->end = (uint8_t*)module + section->VirtualAddress + section->Misc.VirtualSize;
            return true;
        }

        section++;
    }

    return false;
}

static int ScanPattern(uint8_t* start, const uint8_t* end, const char* pattern, uint8_t** position) {
    while (start < end && *pattern) {
        if (*pattern == '?') {
            pattern += 2;
        } else if (*start == strtol(pattern, NULL, 16)) {
            pattern += 3;
        } else {
            *position = start + 1;
            return 0;
        }
        start++;
    }
    return !*pattern;
}

bool FindPattern(void* start, const void* end, const char* pattern, void** position) {
    void* current = start;
    while (current < end) {
        if (ScanPattern((uint8_t*)current, (const uint8_t*)end, pattern, (uint8_t**)&current)) {
            *position = current;
            return true;
        }
    }
    return false;
}
