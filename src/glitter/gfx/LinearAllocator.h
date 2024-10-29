#pragma once

#include <glad/glad.h>

#include <cstddef>
#include <vector>

namespace Glitter::Gfx {

class LinearAllocator {
public:
    LinearAllocator() = default;

    // Returns the offset after the object in the buffer.
    template <typename T> size_t Push(T& t)
    {
        InitializeAlignment();

        // Calculate total amount of bytes that will be pushed.
        size_t sizeAfterT = m_buffer.size() + sizeof(T);
        size_t paddingRequired = sizeAfterT % m_alignment == 0 ? 0 : m_alignment - (sizeAfterT % m_alignment);

        size_t offsetBeforePush = m_buffer.size();
        m_buffer.resize(sizeAfterT + paddingRequired);

        // Push the object.
        std::memcpy(m_buffer.data() + offsetBeforePush, &t, sizeof(T));

        // Push the padding.
        std::memset(m_buffer.data() + offsetBeforePush + sizeof(T), 0, paddingRequired);

        return offsetBeforePush;
    }

    std::byte* Data() { return m_buffer.data(); }
    size_t Size() { return m_buffer.size(); }
    size_t GetAlignment()
    {
        InitializeAlignment();
        return m_alignment;
    }
    void Clear() { m_buffer.clear(); }

private:
    void InitializeAlignment()
    {
        if (!m_initializedAlignment) {
            glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, reinterpret_cast<GLint*>(&m_alignment));
            m_initializedAlignment = true;
        }
    }

    std::vector<std::byte> m_buffer;

    bool m_initializedAlignment {false};
    size_t m_alignment {};
};

} // namespace Glitter::Gfx