#!/bin/bash

# --- 基础路径配置 ---
export LOCAL_DEPS="$PWD/.localdeps/usr"
export PKG_CONFIG_PATH="$LOCAL_DEPS/lib/x86_64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$LOCAL_DEPS/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"
EXE_PATH="./build/src/app/friction"

show_menu() {
    echo "=========================================="
    echo "    Friction (AE 增强版) 开发管理工具"
    echo "=========================================="
    echo " 1) ⚡ 全量编译并启动 (清理缓存 + 补丁 + 运行)"
    echo " 2) 🚀 快速增量编译并启动 (只编译改动部分)"
    echo " 3) 🎬 直接启动现有的程序 (不编译)"
    echo " q) 退出"
    echo "=========================================="
    read -p "请输入选项 [1-3/q]: " opt
}

run_program() {
    if [ -f "$EXE_PATH" ]; then
        echo ">>> 正在启动 Friction..."
        ./$EXE_PATH
    else
        echo "❌ 错误: 找不到可执行程序，请先选择 1 进行编译。"
    fi
}

while true; do
    show_menu
    case $opt in
        1)
            echo "清理旧构建并重新配置..."
            rm -rf build
            CC=clang CXX=clang++ cmake -S . -B build -G Ninja \
              -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_CXX_STANDARD_LIBRARIES="-lsharpyuv" \
              -DCMAKE_PREFIX_PATH="$LOCAL_DEPS;$LOCAL_DEPS/lib/x86_64-linux-gnu/cmake" \
              -DQt5Qml_DIR="$LOCAL_DEPS/lib/x86_64-linux-gnu/cmake/Qt5Qml" \
              -DQSCINTILLA_INCLUDE_DIRS="$LOCAL_DEPS/include/x86_64-linux-gnu/qt5" \
              -DQSCINTILLA_LIBRARIES_DIRS="$LOCAL_DEPS/lib/x86_64-linux-gnu"
            
            ninja -C build -j$(nproc)
            run_program
            ;;
        2)
            if [ ! -d "build" ]; then
                echo "⚠️ 找不到 build 目录，正在执行全量配置..."
                $0 # 递归调用自己执行选项 1 
            else
                echo "正在执行增量编译..."
                # 再次确保链接补丁存在
                sed -i 's/&& :/ -lsharpyuv && :/g' build/build.ninja 2>/dev/null || true
                ninja -C build -j$(nproc)
                run_program
            fi
            ;;
        3)
            run_program
            ;;
        q)
            echo "再见！"
            break
            ;;
        *)
            echo "无效选项，请重新输入。"
            ;;
    esac
done
