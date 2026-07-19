#pragma once

namespace hyp::editor
{
    // DCC(glTF/FBX/OBJ 等)资产的导入/导出——只在 Editor 里用, 不是 Runtime 关心的东西:
    // 玩家运行时只加载引擎自己的 Mesh/Texture/MaterialTemplate 资产格式, 不直接解析 DCC 格式。
    // cook 是另一套流程(把已经制作好的引擎资产打包), 和这里的导入/导出无关
    class AssetInterchange
    {
    public:

    };
}
