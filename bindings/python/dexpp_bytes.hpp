#pragma once

/** Reading the bytes of a Python bytes-like object, shared by the two binding
 *  files (Apk.from_bytes, Model.read). */

#include <pybind11/pybind11.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace dexpp_py {

namespace py = pybind11;

/** The bytes of a Python bytes-like object, in order, in one block of memory.
 *
 *  A buffer can be non-contiguous: a reversed or strided memoryview has its
 *  bytes spread out, and reading size bytes forward from its start reads past
 *  the end.  So ask for a C-contiguous buffer first (bytes, bytearray, a plain
 *  memoryview and array.array give one without copying), and when the object
 *  cannot give one, copy it into a bytes object, which keeps the logical
 *  order.  Never read a py::buffer from info.ptr with size * itemsize. */
class ContiguousBytes
{
  public:
    explicit ContiguousBytes(const py::handle &data)
    {
        if (PyObject_GetBuffer(data.ptr(), &view_, PyBUF_C_CONTIGUOUS) == 0) {
            has_view_ = true;
            bytes_ = std::span<const uint8_t>(static_cast<const uint8_t *>(view_.buf),
                                              static_cast<std::size_t>(view_.len));
            return;
        }
        PyErr_Clear(); // not C-contiguous: fall back to a copy
        copy_ = py::reinterpret_steal<py::object>(PyBytes_FromObject(data.ptr()));
        if (!copy_)
            throw py::error_already_set();
        bytes_ = std::span<const uint8_t>(
            reinterpret_cast<const uint8_t *>(PyBytes_AS_STRING(copy_.ptr())),
            static_cast<std::size_t>(PyBytes_GET_SIZE(copy_.ptr())));
    }
    ~ContiguousBytes()
    {
        if (has_view_)
            PyBuffer_Release(&view_);
    }
    ContiguousBytes(const ContiguousBytes &) = delete;
    ContiguousBytes &operator=(const ContiguousBytes &) = delete;

    std::span<const uint8_t> bytes() const { return bytes_; }

  private:
    Py_buffer view_{};
    bool has_view_ = false;
    py::object copy_;
    std::span<const uint8_t> bytes_;
};

} // namespace dexpp_py
