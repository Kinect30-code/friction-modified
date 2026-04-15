# Friction Modified - Like After Effects

这是一个基于 [Friction](https://github.com/friction2d/friction) 深度修改的2D动画软件，目标是打造Linux平台上的After Effects替代品。

**⚠️ 重要提示：这不是Friction的官方版本，所有问题请勿提交给原项目维护者！**

![Friction Modified Screenshot](screenshot.png)

## ✨ 本版本的主要功能

## 🆕 本次更新

- **缓存优化与架构调整**
  - 对预览缓存逻辑进行了优化。
  - 调整了一部分内部架构，并清理了一些历史脏逻辑。
  - 修复了切换帧率时容易卡死的问题。
  - 修复了播放到 `Out` 位置时出现减速的问题。

- **预览与快捷键工作流**
  - 空格键现在只负责播放/停止，不再等待缓存。
  - 增加一套更接近 After Effects 的快捷键支持。
  - 修复了由于鼠标上下文判定错误导致的快捷键失效问题。
  - 该部分目前仍有少量残余问题，后续继续修复。

- **动画与时间线编辑**
  - 增加参考 Blender 的关键帧曲线速度编辑能力。

- **父子级与坐标/Gizmo 修复**
  - 修复父子级绑定/解绑相关问题。
  - 修复全局坐标与局部坐标模式下的 Gizmo 变换问题。

- **图层、遮罩与编辑体验**
  - 修改图层蒙版逻辑。
  - 当前支持使用 whip 链接单个层级作为遮罩源。
  - 仍然偶发左侧小框闪烁的问题，后续继续处理。
  - 修复固态层无法被选中高亮的问题。
  - 修复无法切换预览质量的问题。

- **新功能与新效果**
  - 新增百叶窗效果。
  - 新增 `Motion Tile` 效果，目前尚未完全可用。
  - 新增 `Deep Glow` 效果，目前仅支持 CPU。
  - 增加 `glb` 导入支持。
  - 加入初步的粒子系统。
  - 添加了可绘制六边形以上多边形的形状工具。

- **插件与资源系统**
  - 添加插件管理器，尽量避免外部插件以破坏性的方式进入核心逻辑。
  - 替换了一套新的图标资源。

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

## 📄 许可证

本项目保持与原项目相同的许可证：

Friction is copyright &copy; Ole-André Rodlie and contributors.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.

**This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the [GNU General Public License](LICENSE.md) for more details.**

Friction is based on [enve](https://github.com/MaurycyLiebner/enve) - Copyright &copy; Maurycy Liebner and contributors.

Third-party software may contain other OSS licenses, see 'Help' > 'About' > 'Licenses' in Friction.

Source code for third-party software can be downloaded [here](https://download.friction.graphics/distfiles/).
