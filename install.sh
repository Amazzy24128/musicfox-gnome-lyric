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
