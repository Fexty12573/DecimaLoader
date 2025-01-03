#pragma once
#include <cstdint>
#include <string_view>

class DecimaTypeDb;
struct String;
struct StringView;

struct String {
private:
    uint32_t m_ref_count = 0;
    uint32_t m_crc = 0;
    uint32_t m_length = 0;
    char* alignas(16) m_data = nullptr;

public:
    using value_type = char;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using pointer = char*;
    using const_pointer = const char*;
    using reference = char&;
    using const_reference = const char&;
    using iterator = char*;
    using const_iterator = const char*;

    const char* c_str() const { return m_data; }
    char* data() const { return m_data; }

    size_t size() const { return m_length; }
    size_t length() const { return m_length; }

    uint32_t crc() const { return m_crc; }

    const_iterator begin() const { return m_data; }
    const_iterator end() const { return m_data + m_length; }

    const_reference operator[](size_t pos) const { return m_data[pos]; }

    bool empty() const { return m_length == 0; }

    iterator begin() { return m_data; }
    iterator end() { return m_data + m_length; }

    reference operator[](size_t pos) { return m_data[pos]; }
};

struct StringView {
private:
    const char* m_data = nullptr;
    uint32_t m_length = 0;
    uint32_t m_crc = 0;

    friend class DecimaTypeDb;

public:
    using value_type = char;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using pointer = char*;
    using const_pointer = const char*;
    using reference = char&;
    using const_reference = const char&;
    using iterator = const char*;
    using const_iterator = const char*;

    StringView() = default;
    StringView(const StringView& other) {
        copy(this, &other);
        m_length = other.m_length;
    }
    explicit StringView(std::string_view other) {
        assign(this, other.data(), (uint32_t)other.size());
        m_length = (uint32_t)other.size();
    }
    ~StringView() {
        release(this);
    }

    const char* c_str() const { return m_data; }

    size_t size() const { return m_length; }
    size_t length() const { return m_length; }

    uint32_t crc() const { return m_crc; }

    const_iterator begin() const { return m_data; }
    const_iterator end() const { return m_data + m_length; }

    const_reference operator[](size_t pos) const { return m_data[pos]; }

    bool empty() const { return m_length == 0; }

    StringView& operator=(const StringView& other) {
        if (this == &other) {
            return *this;
        }

        assign(this, other.m_data, other.m_length);
        return *this;
    }

    StringView& operator=(std::string_view other) {
        assign(this, other.data(), (uint32_t)other.size());
        return *this;
    }

private:
    static inline void(*assign)(StringView*, const_pointer, uint32_t len) = nullptr;
    static inline void(*release)(StringView*) = nullptr;
    static inline void(*copy)(StringView*, const StringView*) = nullptr;
};
