#pragma once
#include <Core/Object/Object.h>

#include <optional>
#include <string>
#include <vector>

namespace hyp::ed
{
    // "选文件 -> 配置属性 -> Import/Cancel"导入流程的公共接口, 供 AssetBrowser 统一驱动。
    class ImportDialog
    {
    public:
        struct Result
        {
            struct Entry
            {
                // 不含路径分隔符/扩展名的资产名; 可以带一层子目录前缀(比如 "Meshes/Cube"),
                // AssetBrowser::importResult() 落盘前会按需建好中间目录。
                std::string name;
                TRef<Object> rootObject;
            };

            // 空 = 直接在当前目录下逐个创建 entries; 非空 = 先在当前目录下新建(去重后的)这个
            // 名字的子文件夹, entries 都落在它下面——多对象导入(比如 glTF 一次产出好几个
            // mesh/material/texture)用这个字段把它们归到同一个文件夹里。
            std::string folderName;

            // 至少一个。单对象导入(比如贴图)就是一个 Entry, folderName 留空。
            std::vector<Entry> entries;
        };

        virtual ~ImportDialog() = default;

        // 为 true 期间必须每帧调 draw(), 否则 ImGui 弹窗那一帧不会被提交进绘制列表。
        virtual bool hasPendingImport() const noexcept = 0;

        // 只有点了 Import 的那一帧返回非空; 其余情况(包括 Cancel)返回空。
        virtual std::optional<Result> draw() = 0;
    };
}
