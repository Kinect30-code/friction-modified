# Friction Modified

这是一个基于 [Friction](https://github.com/friction2d/friction) 2D动画软件的修改版本。

**注意：这不是原作者的官方版本，请勿将问题提交给原Friction项目维护者。**

![Screenshot of Friction 1.0.0](https://friction.graphics/assets/screenshots/100/friction-100rc2-screenshot.png?v=1)

## 本版本的修改内容

- WebM alpha通道支持（使用libvpx解码器）
- AE风格的蒙版创建功能（选中轨道时使用钢笔/方块/圆圈工具会自动创建蒙版）
- Mark快捷键从M键改为小键盘*键
- 修复了视频alpha通道处理的颜色格式问题

## 原项目信息

原项目：[Friction](https://friction.graphics) by Ole-André Rodlie and contributors

原项目GitHub：https://github.com/friction2d/friction

## Branches and versions

Friction uses `X.Y.Z` version numbers and `vX.Y` branches.

* `X` = Major
* `Y` = Minor
* `Z` = Patch

Branch `main` is always the current branch for the next `X` or `Y` release.

A new stable branch is cut from `main` on each `X` or `Y` release and is maintained until a new stable branch is created. Patch (`Z`) releases comes from the parent stable branch (`vX.Y`).

Important fixes added to `main` will be backported to active stable branches when possible (and within reason).

## Documentation

See https://friction.graphics/documentation for generic documentation.

### Build instructions

* [Linux](https://friction.graphics/documentation/source-linux.html)
* [Windows](https://friction.graphics/documentation/source-windows.html)
* [macOS](https://friction.graphics/documentation/source-macos.html)

## License

Friction is copyright &copy; Ole-André Rodlie and contributors.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.

**This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the [GNU General Public License](LICENSE.md) for more details.**

Friction is based on [enve](https://github.com/MaurycyLiebner/enve) - Copyright &copy; Maurycy Liebner and contributors.

Third-party software may contain other OSS licenses, see 'Help' > 'About' > 'Licenses' in Friction.

Source code for third-party software can be downloaded [here](https://download.friction.graphics/distfiles/).
