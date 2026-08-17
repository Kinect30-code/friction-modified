# Friction Modified - Like After Effects

这是一个基于 [Friction](https://github.com/friction2d/friction) 深度修改的2D动画软件，目标是打造Linux平台上的After Effects替代品。

**⚠️ 重要提示：这不是Friction的官方版本，所有问题请勿提交给原项目维护者！**

![Friction Modified Screenshot](screenshot.png)

## ✨ 本版本的主要功能

## 🤝 贡献

We accept any contributions, big or small. Before submitting a pull request it's recommended that you communicate with the developers first (on [GitHub](https://github.com/friction2d/friction/issues) or [Codeberg](https://codeberg.org/friction/friction/issues)).

## 🆕 本次更新 (2026-08-16)

### 同步上游 Friction 主干更新（v1.0.0-rc.3 → main @ 548d46e）
- **QuickSetup / InstallPresets 向导**：全新安装引导与预设管理向导（含 WebM Alpha 预设选项）
- **SmartPath 节点加固**：用空检查与范围钳制替代 RuntimeThrow，修复路径插值/合并崩溃
- **OpenGL ES 3.0 支持**：新增 GLES3 渲染后端选项
- **HiDPI PassThrough 选项**：界面缩放策略可配置
- **视频编码修复**：WebM alpha 非预乘处理、编码器崩溃防护、flush buffers 修正
- **SVG 导入大幅改进**：文本/渐变/透明度/剪贴板粘贴重构
- **父子级效果重构**：官方 bind-state 系统（更稳定的父级跟随与旋转补偿）
- **UX 改进**：Duplicate 重构、step rotation（Ctrl/Shift）、时间轴图层图标、AskDialog、macOS fit canvas 修复
- **保留全部本地特性**：AE 遮罩模块、运动模糊 V2、调整层、GLTF/ORA/粒子模块、WebM Alpha 预设、崩溃日志、插件管理、快捷键系统

## 🕘 上次更新 (2026-06-09)

### AE合成与遮罩系统修复
- **合并塌陷变换按钮逻辑**：将原本分离的 Collapse Transformations / Promote-Demote 行为整合为更接近 After Effects 的单一按钮逻辑，按钮点亮表示塌陷变换启用
- **修复嵌套合成黑底问题**：不塌陷模式下嵌套合成使用透明底渲染，避免目标合成的黑色空背景被带到主合成
- **修复嵌套合成轨道遮罩失效**：不塌陷模式下强制展开子层渲染，确保内部 Track Matte 在外层合成中仍然生效
- **修复轨道遮罩预览缩放错误**：区分预览显示坐标和内部渲染坐标，避免 75% / 50% 预览时遮罩内容越缩越大的问题
- **修复切换塌陷时崩溃**：修复 Track Matte 延迟绘制路径中的空指针访问，避免点击塌陷按钮触发 SIGSEGV
- **修复图层蒙版保存/加载**：Layer Mask 的路径现在保存到真实目标图层，重新打开工程后蒙版路径不再丢失

### AE工作流增强
- **Project 面板行为优化**：继续完善合成、素材和 ORA 导入后的项目组织逻辑
- **Tab 导航与合成链修复**：优化嵌套合成/ORA 合成之间的导航链显示，减少返回普通合成后的层级残留
- **AE快捷键与界面细节调整**：补充常用 AE 风格快捷键，优化时间轴、画布窗口和工具按钮交互
- **新增/更新图标资源**：更新工具栏、文件操作、导入、透明网格等图标资源

### 稳定性与渲染修复
- **修复缓存与嵌套合成状态传播**：嵌套合成内容变化会正确通知外层画布更新，避免外层显示旧缓存
- **修复运动模糊、调整层、效果渲染相关问题**：改进渲染模块中多类效果的更新和缓存失效逻辑
- **修复 ORA / 素材导入细节**：改善 ORA 合成导入、素材命名、剪贴板复制等相关行为

## 🕘 上次更新 (2026-04-28)

### 预览缓存系统重写
- **修复缓存方向反转问题**：嵌套合成/ORA缓存不再"从右往左填"，严格按帧号递增顺序 0→1→2→3... 正向缓存
- **修复缓存提前停止问题**：移除 `previewHasBufferedAhead` 稳态检查，缓存持续进行直到全部帧完成，不再每60帧自动暂停
- **修复缓存范围收缩问题**：`renderDataFinished` 中宽范围缓存条目不再被窄范围替换，静态合成的绿色缓存条一次性填满
- **修复渲染游标回退问题**：移除 `renderCursorAheadOfNeed` 导致的游标暴力拉回逻辑，保持渲染方向一致
- **DI（Dependency Injection）缓存架构**：为 `HddCachableCacheHandler` 引入世代标记机制(`mCacheGeneration`)，支持场景变化时惰性驱逐旧缓存，避免全量清理
- **空闲缓存跨度扩展**：从2s/1s扩展到5s/3s，暂停时预缓存更多帧

### 保存/加载修复
- **修复视频导入后保存再打开只剩一帧**：`VideoBox`/`ImageSequenceBox` 在文件数据异步加载完成前不再调用 `animationDataChanged()`，等待 `reloaded` 信号再更新动画范围
- **修复素材命名问题**：`prp_sFixName()` 不再剥离前导数字，保留 `-` `.` 等合法字符，文件名不再变成"N/A"前缀

### 多选编辑增强
- **多选盒子同步关键帧**：选中多个盒子后，在属性上 Add/Delete Key 会同步应用到所有选中盒子的对应属性（类似AE）

### Tab键导航重写
- **合并场景链+组层级**：多场景时Tab弹出同时显示场景路径和当前场景的组层级，不再丢失上下文
- **修复ORA残留链**：从ORA合成返回普通合成时，导航链正确清除ORA层级，不再显示错误的层级关系
- **根节点使用实际合成名**：不再显示硬编码的"Composition"替代实际合成名

### Project面板重组
- **系统文件夹分组**：合成自动归入 `Compositions` 文件夹，素材自动归入 `Footage` 文件夹
- **ORA包保持层级**：ORA导入的素材包保持原有的包/Compositions/Assets层级结构

### 音频修复
- **修复轨道清空后音频仍播放**：`eSound` 析构函数增加 `removeSound` 调用，确保盒子删除时音频立即停止

### 嵌套合成缓存传播
- **StateChanged信号**：`BoundingBox` content变化时emit `stateChanged`，`InternalLinkBox` 监听并传播 `planUpdate` 到主画布，确保修改嵌套合成后主画布缓存正确失效

### 代码清理
- 清理 `canvas.cpp` / `renderhandler.cpp` / `assetswidget.cpp` 中大量冗余代码和注释

### AE风格界面
- 重新设计的UI布局，模仿After Effects的工作流程
- 更直观的层级管理和时间轴操作

### 视频格式支持
- **WebM导入与Alpha通道支持** - 使用libvpx解码器正确处理带透明通道的WebM视频

### 图像格式支持
- **ORA合成导入** - 支持OpenRaster格式以合成形式导入，并支持热更新

### 层级关系系统
- **父子级关系** - 完整的父子级绑定系统
- **Whip连接** - 使用whip工具快速建立层级关联

### 蒙版与遮罩
- **AE轨道遮罩** - 支持类似After Effects的轨道遮罩功能
- **AE图层蒙版** - 选中轨道时使用钢笔/矩形/椭圆工具可直接创建蒙版

### 合成管理
- **Scene切换** - 像After Effects切换合成一样快速切换Scene

### 动画工具
- **AE木偶功能** - 加入木偶工具用于角色动画（可能存在稳定性问题，但基本可用）

### 快捷键优化
- 添加了一系列类似After Effects的快捷键
- Mark快捷键从M键改为小键盘*键

## ⚠️ 免责声明

**本项目代码完全由AI生成，处于"黑盒"状态，可能对Friction核心代码进行了深度修改。**

- 请勿将本版本的问题提交给Friction官方作者
- 欢迎提交Issue，但请附带详细的报错信息
- 欢迎其他开发者参与维护和优化

## 📋 原项目信息

本修改版基于：
- **原项目**：[Friction](https://friction.graphics) 
- **原作者**：Ole-André Rodlie and contributors
- **原项目GitHub**：https://github.com/friction2d/friction

## 📖 构建说明

* [Linux](https://friction.graphics/documentation/source-linux.html)
* [Windows](https://friction.graphics/documentation/source-windows.html)
* [macOS](https://friction.graphics/documentation/source-macos.html)

## 📄 许可证 (GPL-3.0-only)

本项目保持与原项目相同的许可证：

Friction is copyright &copy; Ole-André Rodlie and contributors.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.

**This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the [GNU General Public License](LICENSE.md) for more details.**

Friction is based on [enve](https://github.com/MaurycyLiebner/enve) - Copyright &copy; Maurycy Liebner and contributors.

Third-party software may contain other OSS licenses, see 'Help' > 'About' > 'Licenses' in Friction.

Source code for third-party software can be downloaded [here](https://download.friction.graphics/distfiles/).
