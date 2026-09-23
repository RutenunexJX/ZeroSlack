# ZeroSlack 应用图标修复

2026-09-24，使用内置 imagegen 对原图进行局部修复。保留青色电路式 Z、左上和右下方形
端点、左下圆形端点、对角线上的橙色空心方形节点及深色圆角底板。仅修补底板左右两处
意外透明缺口，不采用重新设计的粗笔画图标。左下线路原有的开口保留。

源图为 `zeroslack_app.png`，Windows 多尺寸资源为 `../resources/windows/zeroslack.ico`。
执行 `../scripts/update-app-icon.ps1` 可导出 16、24、32、48、64、128、256 像素 ICO。
脚本仅缩放并封装格式，不按颜色抠图或删除内部像素。

首轮修复结果未保留外部透明通道，因此追加外部背景提取；最终四角 alpha 为 0，底板内部
不含透明孔洞。透明度检查同时覆盖外部留白和内部连续性，不能仅以“无内部孔洞”验收。

## 修复提示词

Precise restoration of the supplied ORIGINAL ZeroSlack app icon. This is NOT a redesign. Return the SAME original square image with only the two accidental transparent holes in the dark navy tile repaired. Hole 1 is centered near pixel (240,470); hole 2 near (800,495) in the original 1024x1024 image. Fill only these damaged patches and their dark halos with a seamless continuation of the surrounding navy tile. Preserve the original thin double-line cyan circuit Z, angular corners, top-left square terminal, lower-right square terminal, lower-left circular terminal, orange outlined square node, spacing, scale, placement, background and rounded tile boundary. Preserve the intentional gaps in the lower-left circuit trace. Do not thicken, simplify, recolor or replace the Z. Keep transparency outside the rounded square. No text, shortcut badge or design variants.

Background-extraction follow-up: Remove ONLY the black canvas outside the dark navy rounded-square tile. Return a genuinely transparent PNG, alpha=0 at all four corners and outside the tile, with antialiased edges. Keep the navy tile fully opaque. Preserve the cyan circuit Z, terminals, orange outlined square, proportions, positions and fill exactly. Do not render a black, white, colored or checkerboard backdrop.
