#pragma once
#include <Engine/Mesh/MeshUtility.h>

namespace hyp
{
    // IMeshUtility 唯一的具体实现——内部依赖 Embree(MeshBVH), 只存在于编辑期工具链里,
    // Engine 及其运行期消费方从不 #include 这个头文件、也从不链接这个 target
    class MeshUtility : public IMeshUtility
    {
    public:
        MeshSDFBakeResult bakeDistanceField(StaticMesh* mesh, const MeshSDFBakeParams& params) override;

        MeshCardBakeResult bakeCards(StaticMesh* mesh, const MeshCardBakeParams& params) override;
    };
}
