#pragma once

namespace hyp
{
    enum class ProjectionType
    {
        Perspective,
        Orthographic,
    };

    /*template <> Type* Reflection::createType<ProjectionType>()
    {
        auto e = new TEnumeration<ProjectionType>("ProjectionType");
        e->addValue("Perspective", ProjectionType::Perspective);
        e->addValue("Orthographic", ProjectionType::Orthographic);
        return e;
    }*/

    enum class ProjectionAxis
    {
        Horizontal,
        Vertical,
    };

    /*template <> Type* Reflection::createType<ProjectionAxis>()
    {
        auto e = new TEnumeration<ProjectionAxis>("ProjectionAxis");
        e->addValue("Horizontal", ProjectionAxis::Horizontal);
        e->addValue("Vertical", ProjectionAxis::Vertical);
        return e;
    }*/

}
