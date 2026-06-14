#pragma once
#include <pybind11/pybind11.h>

#include <Core/Common/HString.h>

namespace py = pybind11;

namespace pybind11::detail
{
    template <>
    struct type_caster<hyp::HString> : public type_caster_base<hyp::HString>
    {
        using base = type_caster_base<hyp::HString>;
    public:
        using base::cast;

        bool load(handle src, bool convert)
        {
            if (isinstance<hyp::HString>(src))
            {
                return base::load(src, false);
            }

            if (convert && isinstance<str>(src))
            {
                value = &getOrCreateFromCache(src.ptr());
                return true;
            }

            return false;
        }

    private:
        static hyp::HString& getOrCreateFromCache(PyObject* obj);
    };
}

void bindCore(py::module_& m);

void bindEngine(py::module_& m);
