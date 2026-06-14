#include "PyHyperEngine.h"

#include <Core/Common/HString.h>
#include <Core/Common/Guid.h>

#include <Core/Math/Vector.h>
#include <Core/Math/Quaternion.h>
#include <Core/Math/Matrix.h>

#include <Core/Object/Object.h>

#include <pybind11/operators.h>

using namespace hyp;


namespace pybind11::detail
{
    hyp::HString& type_caster<hyp::HString>::getOrCreateFromCache(PyObject* obj)
    {
        static std::unordered_map<PyObject*, hyp::HString> cache;

        PyObject* interned_obj = obj;
        PyUnicode_InternInPlace(&interned_obj);

        auto it = cache.find(interned_obj);
        if (it != cache.end())
        {
            return it->second;
        }

        Py_ssize_t size = 0;
        const char* data = PyUnicode_AsUTF8AndSize(obj, &size);

        auto result = cache.emplace(interned_obj, hyp::HString(std::string_view(data, static_cast<size_t>(size))));
        return result.first->second;
    }
}

void bindHString(py::module_& m)
{
    py::class_<HString>(m, "HString")
        .def(py::init<std::string_view>(), py::arg("string"))
        .def("__str__", &HString::toStringView)
        .def("__repr__", [](const HString& hs) { return "HString(\"" + std::string(hs.toStringView()) + "\")"; })
        .def(py::self == py::self)
        .def(py::self != py::self)
        .def(py::self < py::self)
        .def("__hash__", &HString::getTypeHash)
        .def_property("number", &HString::getNumber, &HString::setNumber)
        .def("empty", &HString::empty);

    //py::implicitly_convertible<std::string_view, HString>();
}

void bindGuid(py::module_& m)
{
    py::class_<Guid>(m, "Guid")
        .def(py::init<>())
        .def(py::init<uint32_t, uint32_t, uint32_t, uint32_t>(),
             py::arg("A"), py::arg("B"), py::arg("C"), py::arg("D"))
        .def_readonly("A", &Guid::A)
        .def_readonly("B", &Guid::B)
        .def_readonly("C", &Guid::C)
        .def_readonly("D", &Guid::D)
        .def("is_valid", &Guid::isValid)
        .def("to_string", &Guid::toString)
        .def_static("new_guid", &Guid::newGuid)
        .def(py::self == py::self)
        .def(py::self != py::self)
        .def("__str__", &Guid::toString)
        .def("__repr__", [](const Guid& g) {
            return "Guid(\"" + std::string(g.toString()) + "\")";
        })
        .def("__hash__", [](const Guid& g) {
            return std::hash<Guid>()(g);
        });
}

template <typename T>
void bindVector(py::module_& m, const std::string& typeName)
{
    using Vector2 = TVector2<T>;
    using Vector3 = TVector3<T>;
    using Vector4 = TVector4<T>;

    std::string vec2Name = typeName + "2";
    py::class_<Vector2>(m, vec2Name.c_str())
        .def(py::init<>())
        .def(py::init<T>(), py::arg("scalar"))
        .def(py::init<T, T>(), py::arg("x"), py::arg("y"))
        .def(py::self + py::self)
        .def(py::self - py::self)
        .def(py::self * py::self)
        .def(py::self / py::self)
        .def(py::self * T())
        .def(T() * py::self)
        .def("__str__", [](const Vector2& v) { return "[" + std::to_string(v.x) + ", " + std::to_string(v.y) + "]"; })
        .def_readwrite("x", &Vector2::x)
        .def_readwrite("y", &Vector2::y);

    std::string vec3Name = typeName + "3";
    py::class_<Vector3>(m, vec3Name.c_str())
        .def(py::init<>())
        .def(py::init<T>(), py::arg("scalar"))
        .def(py::init<T, T, T>(), py::arg("x"), py::arg("y"), py::arg("z"))
        .def(py::init<Vector2, T>(), py::arg("xy"), py::arg("z"))
        .def(py::init<T, Vector2>(), py::arg("x"), py::arg("yz"))
        .def(py::self + py::self)
        .def(py::self - py::self)
        .def(py::self * py::self)
        .def(py::self / py::self)
        .def(py::self * T())
        .def(T() * py::self)
        .def("__str__", [](const Vector3& v) {
            return "[" + std::to_string(v.x) + ", " +
                std::to_string(v.y) + ", " +
                std::to_string(v.z) + "]";
            }
        )
        .def_readwrite("x", &Vector3::x)
        .def_readwrite("y", &Vector3::y)
        .def_readwrite("z", &Vector3::z);

    std::string vec4Name = typeName + "4";
    py::class_<Vector4>(m, vec4Name.c_str())
        .def(py::init<>())
        .def(py::init<T>(), py::arg("scalar"))
        .def(py::init<T, T, T, T>(), py::arg("x"), py::arg("y"), py::arg("z"), py::arg("w"))
        .def(py::init<Vector2, Vector2>(), py::arg("xy"), py::arg("zw"))
        .def(py::init<Vector3, T>(), py::arg("xyz"), py::arg("w"))
        .def(py::init<T, Vector3>(), py::arg("x"), py::arg("yzw"))
        .def(py::self + py::self)
        .def(py::self - py::self)
        .def(py::self * py::self)
        .def(py::self / py::self)
        .def(py::self * T())
        .def(T() * py::self)
        .def("__str__", [](const Vector4& v) {
            return "[" + std::to_string(v.x) + ", " +
                std::to_string(v.y) + ", " +
                std::to_string(v.z) + ", " +
                std::to_string(v.w) + "]";
            }
        )
        .def_readwrite("x", &Vector4::x)
        .def_readwrite("y", &Vector4::y)
        .def_readwrite("z", &Vector4::z)
        .def_readwrite("w", &Vector4::w);
}

template <typename T>
void bindMatrix(py::module_& m, const std::string& typeName)
{
    using Vector3 = TVector3<T>;
    using Vector4 = TVector4<T>;
    using Matrix3 = TMatrix3<T>;
    using Matrix4 = TMatrix4<T>;

    std::string mat3Name = typeName + "3x3";
    py::class_<Matrix3>(m, mat3Name.c_str())
        .def(py::init<>())
        .def(py::init<T>(), py::arg("scalar"))
        .def(py::self * py::self)
        .def(py::self * T())
        .def(T() * py::self)
        .def(py::self * Vector3())
        .def(Vector3() * py::self)
        .def("__getitem__",
            [](const Matrix3& m, std::pair<int, int> i) {
                if (i.first >= 3 || i.second >= 3) throw py::index_error();
                return m[i.first][i.second];
            }
        );

    std::string mat4Name = typeName + "4x4";
    py::class_<Matrix4>(m, mat4Name.c_str())
        .def(py::init<>())
        .def(py::init<T>(), py::arg("scalar"))
        .def(py::self * py::self)
        .def(py::self * T())
        .def(T() * py::self)
        .def(py::self * Vector4())
        .def(Vector4() * py::self)
        .def("__getitem__",
            [](const Matrix4& m, std::pair<int, int> i) {
                if (i.first >= 4 || i.second >= 4) throw py::index_error();
                return m[i.first][i.second];
            }
        );
}

void bindQuaternion(py::module& m)
{
    py::class_<Quaternion>(m, "Quaternion")
        .def(py::init<>())
        .def(py::init<float, float, float, float>(), py::arg("w"), py::arg("x"), py::arg("y"), py::arg("z"))
        .def_readwrite("w", &Quaternion::w)
        .def_readwrite("x", &Quaternion::x)
        .def_readwrite("y", &Quaternion::y)
        .def_readwrite("z", &Quaternion::z);
}

void bindMath(py::module_& m)
{
    bindVector<float>(m, "float");
    bindVector<double>(m, "double");
    bindVector<int>(m, "int");
    bindVector<uint32_t>(m, "uint");
    bindMatrix<float>(m, "float");
    bindMatrix<double>(m, "double");
    bindQuaternion(m);
}

PYBIND11_DECLARE_HOLDER_TYPE(T, TRef<T>);

void bindObject(py::module_& m)
{
    py::enum_<ObjectFlags>(m, "ObjectFlags", py::arithmetic())
        .value("NoFlag", ObjectFlags::None)
        .value("Default_Object", ObjectFlags::Default_Object);

    py::class_<Object, TRef<Object>>(m, "Object")
        .def_property_readonly("guid", &Object::getGuid)
        .def_property_readonly("owner", &Object::getOwner, py::return_value_policy::reference)
        .def_property_readonly("flags", &Object::getFlags)
        .def("get_root", static_cast<Object * (Object::*)()>(&Object::getRoot), py::return_value_policy::reference);

    m.def("create_object",
        [](HString className, Object* owner, ObjectFlags flags) { return createObject(className, owner, flags); },
        py::arg("class_name"), py::arg("owner") = nullptr, py::arg("flags") = ObjectFlags::None
    );
}

void bindCore(py::module_& m)
{
    bindHString(m);
    bindGuid(m);
    bindMath(m);
    bindObject(m);
}
