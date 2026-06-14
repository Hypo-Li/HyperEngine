#include "PyHyperEngine.h"

#include <Engine/Framework/Entity.h>
#include <Engine/Framework/Scene.h>

using namespace hyp;

PYBIND11_DECLARE_HOLDER_TYPE(T, TRef<T>);

void bindTransform(py::module_& m)
{
    py::enum_<ReparentMode>(m, "ReparentMode")
        .value("Keep_World", ReparentMode::Keep_World)
        .value("Keep_Local", ReparentMode::Keep_Local)
        .value("Reset_Local", ReparentMode::Reset_Local);

    py::class_<Transform, TRef<Transform>, Object>(m, "Transform")
        .def_property("local_position", &Transform::getLocalPosition, &Transform::setLocalPosition)
        .def_property("local_rotation", &Transform::getLocalRotation, &Transform::setLocalRotation)
        .def_property("local_scale", &Transform::getLocalScale, &Transform::setLocalScale)
        .def_property("world_position", &Transform::getWorldPosition, &Transform::setWorldPosition)
        .def_property("world_rotation", &Transform::getWorldRotation, &Transform::setWorldRotation)
        .def_property("world_scale", &Transform::getWorldScale, &Transform::setWorldScale)
        .def_property_readonly("local_forward_vector", &Transform::getLocalForwardVector)
        .def_property_readonly("local_right_vector", &Transform::getLocalRightVector)
        .def_property_readonly("local_up_vector", &Transform::getLocalUpVector)
        .def_property_readonly("world_forward_vector", &Transform::getWorldForwardVector)
        .def_property_readonly("world_right_vector", &Transform::getWorldRightVector)
        .def_property_readonly("world_up_vector", &Transform::getWorldUpVector)
        .def_readonly_static("forward_vector", &Transform::ForwardVector)
        .def_readonly_static("right_vector", &Transform::RightVector)
        .def_readonly_static("up_vector", &Transform::UpVector);
}

void bindSceneAndEntity(py::module_& m)
{
    auto scene = py::class_<Scene, TRef<Scene>, Object>(m, "Scene");
    auto entity = py::class_<Entity, TRef<Entity>, Object>(m, "Entity");

    scene.def("create_entity", &Scene::createEntity, py::arg("name"))
        .def("destroy_entity", &Scene::destroyEntity, py::arg("entity"), py::arg("destroy_children") = false);

    entity.def_property("name", &Entity::getName, &Entity::setName)
        .def_property_readonly("transform", [](Entity* entity) { return &entity->getTransform(); }, py::return_value_policy::reference_internal)
        .def("get_scene", &Entity::getScene)
        .def("set_parent", &Entity::setParent, py::arg("parent"), py::arg("mode") = ReparentMode::Keep_World)
        .def("get_parent", &Entity::getParent)
        .def("add_child", &Entity::addChild, py::arg("child"), py::arg("mode") = ReparentMode::Keep_World)
        .def("get_child_count", &Entity::getChildCount)
        .def("get_child", &Entity::getChild, py::arg("index"))
        .def("remove_child", &Entity::removeChild, py::arg("child"), py::arg("mode") = ReparentMode::Keep_World)
        .def("remove_children", &Entity::removeChildren, py::arg("start"), py::arg("count"), py::arg("mode") = ReparentMode::Keep_World);
}

void bindEngine(py::module_& m)
{
    bindTransform(m);
    bindSceneAndEntity(m);
}
