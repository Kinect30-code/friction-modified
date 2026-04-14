#!/bin/bash

# 上传 AppImage 到 GitHub Release 的辅助脚本

APPIMAGE_FILE="Friction-Modified-1.0.0-AE-x86_64.AppImage"
REPO="Kinect30-code/friction-modified"
VERSION="v1.0.0-AE"

echo "=========================================="
echo "  GitHub Release 上传工具"
echo "=========================================="
echo ""
echo "由于环境限制，无法自动上传。"
echo ""
echo "请按以下步骤手动上传:"
echo ""
echo "1. 打开浏览器访问:"
echo "   https://github.com/$REPO/releases/new"
echo ""
echo "2. 创建新 Release:"
echo "   - Tag version: $VERSION"
echo "   - Release title: Friction Modified 1.0.0 AE Edition"
echo "   - 描述: 复制下面的内容"
echo ""
echo "3. 上传文件:"
echo "   拖拽或选择文件: $APPIMAGE_FILE"
echo ""
echo "=========================================="
echo "Release 描述模板:"
echo "=========================================="
cat << 'EOF'
## Friction Modified 1.0.0 AE Edition

这是一个基于 Friction 深度修改的 2D 动画软件，目标是打造 Linux 平台上的 After Effects 替代品。

### ✨ 主要功能

- **AE风格界面** - 重新设计的UI布局，模仿After Effects的工作流程
- **WebM导入与Alpha通道支持** - 使用libvpx解码器正确处理带透明通道的WebM视频
- **ORA合成导入** - 支持OpenRaster格式以合成形式导入，并支持热更新
- **父子级关系** - 完整的父子级绑定系统，使用whip工具快速建立层级关联
- **AE轨道遮罩与图层蒙版** - 支持类似After Effects的遮罩功能
- **Scene切换** - 像After Effects切换合成一样快速切换Scene
- **AE木偶功能** - 加入木偶工具用于角色动画（可能存在稳定性问题）
- **AE风格快捷键** - 添加了一系列类似After Effects的快捷键

### ⚠️ 免责声明

本项目代码完全由AI生成，处于"黑盒"状态，可能对Friction核心代码进行了深度修改。

- 请勿将本版本的问题提交给Friction官方作者
- 欢迎提交Issue，但请附带详细的报错信息
- 欢迎其他开发者参与维护和优化

### 📋 系统要求

- Linux x86_64 系统
- 支持 FUSE (用于运行 AppImage)

### 🚀 使用方法

```bash
# 赋予执行权限
chmod +x Friction-Modified-1.0.0-AE-x86_64.AppImage

# 运行
./Friction-Modified-1.0.0-AE-x86_64.AppImage
```

### 📄 许可证

GPL-3.0 - 与原Friction项目保持一致

原项目：https://github.com/friction2d/friction
EOF
echo ""
echo "=========================================="
echo ""
echo "文件信息:"
ls -lh "$APPIMAGE_FILE"
echo ""
echo "SHA256: $(sha256sum "$APPIMAGE_FILE" | cut -d' ' -f1)"
echo ""
echo "=========================================="
