#include "MeshUtility.h"

#include "MeshBVH.h"
#include "MeshCard.h"
#include "MeshDistanceField.h"

namespace hyp
{
    MeshSDFBakeResult MeshUtility::bakeDistanceField(StaticMesh* mesh, const MeshSDFBakeParams& params)
    {
        std::unique_ptr<ed::MeshBVH> bvh = ed::createMeshBVH(mesh);
        return ed::bakeMeshDistanceFieldCore(mesh, *bvh, params);
    }

    MeshCardBakeResult MeshUtility::bakeCards(StaticMesh* mesh, const MeshCardBakeParams& params)
    {
        std::unique_ptr<ed::MeshBVH> bvh = ed::createMeshBVH(mesh);
        return ed::bakeMeshCardsCore(mesh, *bvh, params);
    }
}
