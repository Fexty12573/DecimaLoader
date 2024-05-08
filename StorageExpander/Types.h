#pragma once
#include <cstdint>

#include "RTTIObject.h"

#ifdef __cplusplus

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

    const char* c_str() const { return m_data; }

    size_t size() const { return m_length; }
    size_t length() const { return m_length; }

    uint32_t crc() const { return m_crc; }

    const_iterator begin() const { return m_data; }
    const_iterator end() const { return m_data + m_length; }

    const_reference operator[](size_t pos) const { return m_data[pos]; }

    bool empty() const { return m_length == 0; }
};

template<typename T> struct Array {
private:
    uint32_t m_size = 0;
    uint32_t m_capacity = 0;
    T* m_data = nullptr;

public:
    using value_type = T;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = T*;
    using const_iterator = const T*;

    const_pointer data() const { return m_data; }

    size_t size() const { return m_size; }
    size_t capacity() const { return m_capacity; }

    const_iterator begin() const { return m_data; }
    const_iterator end() const { return m_data + m_size; }

    const_reference operator[](size_t pos) const { return m_data[pos]; }

    bool empty() const { return m_size == 0; }

    iterator begin() { return m_data; }
    iterator end() { return m_data + m_size; }

    reference operator[](size_t pos) { return m_data[pos]; }
};

struct LocalizedTextResource : RTTIRefObject {
    const char* Text; //0x0020
    uint16_t Length; //0x0028
};

struct InventoryItemResource : RTTIRefObject {
    void* N00000090; //0x0020
    void* EntityResourceType; //0x0028
    char pad_0030[24]; //0x0030
    LocalizedTextResource* ItemName; //0x0048
    LocalizedTextResource* ItemShortName; //0x0050
    LocalizedTextResource* ItemDescription; //0x0058
    char pad_0060[80]; //0x0060
    uint8_t N00000196; //0x00B0
    bool IsStackable; //0x00B1
    char pad_00B2[6]; //0x00B2
    Array<int> MaxStackSize; //0x00B8
    Array<int> MaxStackSizeLevel; //0x00C8
    char pad_00D8[136]; //0x00D8

    virtual void Function0();
    virtual void Function1();
    virtual void Function2();
    virtual void Function3();
    virtual void Function4();
    virtual void Function5();
    virtual void Function6();
    virtual void Function7();
    virtual void Function8();
    virtual void Function9();
}; //Size: 0x0160
ASSERT_OFFSET(InventoryItemResource, ItemName, 0x48);
ASSERT_OFFSET(InventoryItemResource, MaxStackSize, 0xB8);

struct MsgGetMaxFitAmount : RTTIObject {
    bool N00000059; //0x0008
    char pad_0009[7]; //0x0009
    InventoryItemResource* ItemResource; //0x0010
    int32_t Amount; //0x0018
    char pad_001C[4]; //0x001C

    const char* item_name() const {
        return ItemResource && ItemResource->ItemName && ItemResource->ItemName->Text
            ? ItemResource->ItemName->Text
            : "N/A";
    }
}; //Size: 0x0020

#else

typedef void* String;
typedef void* StringView;

#endif
