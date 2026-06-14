#include "PyHyperEngine.h"

void registerEngineAPI(py::module_& m)
{
    bindCore(m);
    bindEngine(m);
}

#ifdef BUILDING_EMBEDDED
#include <pybind11/embed.h>
PYBIND11_EMBEDDED_MODULE(PyHyperEngine, m)
{
    registerEngineAPI(m);
}
#else

PYBIND11_MODULE(PyHyperEngine, m)
{
    registerEngineAPI(m);
}
#endif
