#pragma once

namespace hyp::ed
{
    class Editor;

    // 常驻钉在菜单栏正下方的一行工具栏, 目前只有播放控制。独立开窗绘制(不借助 DockSpace 的
    // 宿主窗口)——播放控制要用图标字体, 字形比正文字体大, 放不进菜单栏那一行, 见
    // MainEditorTab 的注释。
    class EditorToolbar
    {
    public:
        explicit EditorToolbar(Editor& editor);

        // topReserved: 视口顶部已经被菜单栏(+标签条, 两者同一行)占掉的高度。返回这一帧
        // 实际画出来的高度(像素, 已按 DPI 缩放)——图标字形比正文字体大, 实际占用高度不是
        // GetFrameHeight() 能算出来的定值, 调用方(MainEditorTab)拿它去告诉 DockSpace 该
        // 往下再让多少。
        float draw(float topReserved);

    private:
        Editor& mEditor;
    };
}
