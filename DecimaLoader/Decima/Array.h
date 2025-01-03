#pragma once

#include <cstdint>

#include "../RTTI/RTTIContainer.h"


template<typename T, RTTIContainer* RTTI = nullptr> struct Array {
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

#pragma region RTTI Functions (Requires RTTI template parameter)
private:
    using Resize = bool(*)(RTTIContainer*, Array<T>*, size_t, bool);
    using Insert = bool(*)(RTTIContainer*, Array<T>*, size_t, T*);
    using Erase = bool(*)(RTTIContainer*, Array<T>*, size_t);
    using GetItem = pointer(*)(RTTIContainer*, Array<T>*, size_t);
    using Clear = bool(*)(RTTIContainer*, Array<T>*);

public:

    bool resize(size_t new_size, bool instantiateItems = false) {
        if (!RTTI) { return false; }
        const auto resize = (Resize)RTTI->Data->Resize;
        return resize(RTTI, this, new_size, instantiateItems);
    }

    bool insert(size_t index, T* item) {
        if (!RTTI) { return false; }
        const auto insert = (Insert)RTTI->Data->Insert;
        return insert(RTTI, this, index, item);
    }

    bool erase(size_t index) {
        if (!RTTI) { return false; }
        const auto erase = (Erase)RTTI->Data->Erase;
        return erase(RTTI, this, index);
    }

    pointer get(size_t index) {
        if (!RTTI) { return nullptr; }
        const auto get = (GetItem)RTTI->Data->GetItem;
        return get(RTTI, this, index);
    }

    bool clear() {
        if (!RTTI) { return false; }
        const auto clear = (Clear)RTTI->Data->Clear;
        return clear(RTTI, this);
    }
#pragma endregion
};
