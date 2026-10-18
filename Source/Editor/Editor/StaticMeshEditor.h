#pragma once
#include "AssetEditorWindow.h"
#include "Viewport/SceneViewport.h"

#include <Core/Common/Guid.h>
#include <Engine/Framework/Scene.h>
#include <Engine/Mesh/StaticMesh.h>

namespace hyp::ed
{
    class ThumbnailProvider;
    class AssetBrowser;
    class UndoStack;

    class StaticMeshEditor final : public AssetEditorWindow
    {
    public:
        StaticMeshEditor(Asset& asset, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);

    protected:
        void onDraw() override;

    private:
        void buildPreviewScene();
        void frameMesh();
        void drawViewport();
        void drawDetails();
        void drawMaterialSlots();
        void drawSave();
        void applyMaterialSlot(uint32_t slotIndex, Guid materialGuid);

    private:
        ThumbnailProvider& mIconProvider;
        AssetBrowser& mAssetBrowser;
        TRef<StaticMesh> mMesh;
        TRef<Scene> mPreviewScene;
        SceneViewport mViewport;
    };
}
