#!/bin/bash

# 脚本出错时立即退出
set -e

echo "🚀 开始安装 Musicfox GNOME 扩展..."

# --- 1. 定义变量 ---
# 项目根目录 (脚本所在的目录)
SOURCE_DIR=$(dirname "$(readlink -f "$0")")
# GNOME 扩展安装目录
DEST_DIR="$HOME/.local/share/gnome-shell/extensions/musicfox-lyric@amazzy24128"

# --- 2. 编译后端 (如果需要的话) ---
# 如果你的后端代码需要编译，取消下面几行的注释
# echo "⚙️  正在编译后端服务..."
# cd "$SOURCE_DIR/backend/my_backend"
# make # 或者你的编译命令，例如 g++ ...
# cd "$SOURCE_DIR" # 返回项目根目录

# --- 3. 安装文件 ---
echo "🧩 准备安装目录..."
# 确保目标目录存在
mkdir -p "$DEST_DIR"

echo "📄 正在复制文件..."
# 在复制后端可执行文件前，确保任何正在运行的同名进程已被终止，避免“文本文件忙”
echo "🔎 检查并终止正在运行的后端进程 (music-in"shell-version": [ "46" ],fo-service) ..."

# 尝试找到运行中的进程（匹配可执行名或路径）
PIDS=$(pgrep -f "music-info-service" || true)
if [[ -n "$PIDS" ]]; then
    echo "⚠️ 发现正在运行的后端进程，PID: $PIDS。尝试优雅终止（SIGTERM）..."
    # 发送 SIGTERM
    kill -TERM $PIDS 2>/dev/null || sudo kill -TERM $PIDS 2>/dev/null || true

    # 等待进程退出（最多等待 5 秒）
    for i in {1..5}; do
        sleep 1
        STILL=$(pgrep -f "music-info-service" || true)
        if [[ -z "$STILL" ]]; then
            echo "✅ 后端进程已优雅退出。"
            break
        fi
    done

    # 如果仍在运行，强制终止（SIGKILL）
    STILL=$(pgrep -f "music-info-service" || true)
    if [[ -n "$STILL" ]]; then
        echo "❌ 后端进程未响应，强制终止（SIGKILL） PID: $STILL"
        kill -KILL $STILL 2>/dev/null || sudo kill -KILL $STILL 2>/dev/null || true
        sleep 1
    fi
else
    echo "ℹ️ 未发现正在运行的后端进程。"
fi

# 复制前端文件
sudo cp "$SOURCE_DIR/frontend/extension.js" "$DEST_DIR/"
sudo cp "$SOURCE_DIR/frontend/metadata.json" "$DEST_DIR/"

# 复制后端编译好的二进制文件
sudo cp "$SOURCE_DIR/backend/my_backend/music-info-service" "$DEST_DIR/"

# --- 4. 完成 ---
echo "✅ 安装成功！"
echo "扩展已安装到: $DEST_DIR"

# --- 5. (可选) 重启 GNOME Shell ---
# 如果你想让脚本自动重启GNOME Shell来加载新版本，取消下面的注释
# 注意：这会瞬间黑屏一下，是正常现象
# read -p "是否立即重启 GNOME Shell (y/n)? " -n 1 -r
# echo
# if [[ $REPLY =~ ^[Yy]$ ]]; then
#     echo "🔄 正在重启 GNOME Shell..."
#     gnome-shell --replace &
# fi

echo "🎉 完成！"
