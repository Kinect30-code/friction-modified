# AI 交接文档（2026-03-27）

## 项目上下文
- 项目路径：`D:\friction-1.0.0-rc.3`
- 目标平台：Linux（用户明确表示不需要 Windows 平台）
- 当前主题：AE 工作流对齐（蒙版、多层级/预合成体验、轨道显示、特效交互）

## 今日已完成（可直接接手继续）

### 1) 轨道/时间线与层级流程（AE 风格）
- `M` 键：已接入“显示蒙版相关轨道”逻辑。
- `Tab` 键：已接入小型组层级流程弹窗（用于在组层级之间快速切换）。
- 组内展开时的显示行为：已实现“进入组后仅显示组内内容”的预合成式工作流。
- 修复偶发“只显示选中轨道”问题（与 frame remapping reveal 干扰相关的逻辑已调整）。

### 2) 蒙版体系（首版）
- 画 shape 作为蒙版路径：已支持，并自动创建关联。
- 一个 layer 可挂多个蒙版路径：已支持（每次创建会新增对应 Layer Mask 关系）。
- 新增自动命名：`<目标层名> Mask N`。
- 之前仅允许部分层作为蒙版目标；现在已放开为所有 `BoundingBox`（含 group/layer）。
- `M` 展示逻辑增强：不仅显示同级 `DstIn/DstOut` 形状，还会展开 layer 上 `LayerMaskEffect` 及其子属性。

### 3) Blend / Matte 相关修复
- `LayerMaskEffect` 已完成菜单和工厂注册修复。
- `TrackMatteEffect` 的类型 id 错误已修复（`targeted` -> `trackMatte`）。

### 4) 栅格特效能力扩展
- 新增 `Displacement Map`（可引用其他轨道作为位移图，CPU 路径）。
- 新增 `Corner Pin`（CPU 路径）：
  - 4 点可在画布直接拖拽（不是纯数值输入）。
  - 已注册到 Distort 菜单、枚举、工厂、CMake。

### 5) 效果菜单“重名合并”
- 针对 core/custom/shader 效果列表做了按名称归并。
- 主入口只保留一个；重复项进入 `Variants` 子菜单，避免 UI 上重复刷屏。

### 6) 其他修复
- 导入 WebM alpha 问题：已修过一版（按解码后的像素格式处理）。
- Adjustment layer：已接入一条可工作的基础路径（CPU effect pass），但仍是“可用首版”，不是完整 AE 语义。

## 本日新增/修改的关键文件
- `src/core/canvasmouseinteractions.cpp`
- `src/app/GUI/BoxesList/boxscrollwidget.cpp`
- `src/core/RasterEffects/cornerpineffect.h`
- `src/core/RasterEffects/cornerpineffect.cpp`
- `src/core/RasterEffects/displacementmapeffect.h`
- `src/core/RasterEffects/displacementmapeffect.cpp`
- `src/core/RasterEffects/rastereffect.h`
- `src/core/RasterEffects/rastereffectsinclude.h`
- `src/core/RasterEffects/rastereffectcollection.cpp`
- `src/core/RasterEffects/rastereffectmenucreator.cpp`
- `src/core/RasterEffects/rastereffectmenucreator.h`
- `src/core/CMakeLists.txt`
- `src/core/BlendEffects/blendeffectcollection.cpp`

## 还没完成（下一个 AI 优先做）

### P0（用户感知最强）
1. `LayerMaskEffect` 参数补齐到可用版本：
- 当前只有 Add/Subtract（mode）。
- 建议下一步最少加：`opacity`、`expand`（先做），`feather`（后做）。

2. `Corner Pin` 交互细化：
- 已有四点拖拽，但还缺 UI 细节（点位标签、重置、约束/吸附、可视反馈优化）。

3. 跨轨采样能力规范化（为 Time Displacement / Turbulent Displace 做基础）：
- 现在只有 `Displacement Map` 是单效果内实现。
- 需要评估是否沉淀为可复用“轨道输入”协议，供更多 GLSL/CPU 效果统一复用。

### P1
1. 检查“所有 layer 均支持蒙版”边界：
- 逻辑已放开到所有 `BoundingBox`，但建议验证特殊类型（链接层、预合成/组内层、文本/形状/视频层）在渲染与 UI 展开上的一致性。

2. 重名效果合并策略回归：
- 当前按名称归并 + `Variants`；需确认对已有用户习惯是否有负面影响（例如预设路径认知、搜索行为）。

## 已知风险/注意点
- 本机环境缺少 `cmake` 命令，今天无法本地编译验证。
- `Corner Pin` 与 `Displacement Map` 当前走 CPU，性能和边缘质量需在 Linux 目标机实测。
- `Adjustment layer` 是工作版，不是完整 AE 级语义。

## 建议下一个 AI 的执行顺序
1. 先实现 `LayerMaskEffect.opacity + expand`（最短路径让用户直观看到“蒙版轨道可调参数”）。
2. 再补 `Corner Pin` 交互细节（保持当前架构，不要重做）。
3. 最后做“跨轨输入通道抽象”设计，再接 `Time Displacement / Turbulent Displace`。

## 验收建议（Linux）
1. 单层多个 mask：创建 3 个路径，`M` 键应能稳定展开 mask 相关轨道。
2. 组内预合成体验：进入组后只显示组内内容；Tab 流程图能返回上级。
3. Corner Pin：拖动 4 点时画布实时变化，关键帧可记录。
4. Displacement Map：引用另一个 layer 作为 map source，时间轴播放结果稳定。
5. WebM alpha：导入含透明通道素材，边缘不应黑边/不透明。

