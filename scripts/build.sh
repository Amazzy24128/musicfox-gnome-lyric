
#!/bin/bash

set -e

echo "Building MusicFox GNOME Extension (C++ Backend)..."

# 检查依赖
echo "Checking dependencies..."
if ! command -v cmake &> /dev/null; then
    echo "Error: cmake is required but not installed."
    exit 1
fi

if ! pkg-config --exists glib-2.0 gio-2.0; then
    echo "Error: GLib development libraries are required."
    echo "Install with: sudo apt install libglib2.0-dev (Ubuntu/Debian)"
    echo "Or: sudo dnf install glib2-devel (Fedora)"
    exit 1
fi

# 构建 C++ 后端
echo "Building C++ backend..."
cd /home/amz/musicfox-gnome-extension/backend
mkdir -p build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

cd ../..

# 创建扩展安装目录
EXTENSION_DIR="$HOME/.local/share/gnome-shell/extensions/go-musicfox-lyrics@amazzy24128.github.io"
mkdir -p "$EXTENSION_DIR"

# 复制扩展文件
echo "Installing extension files..."
cp extension/extension.js "$EXTENSION_DIR/"
cp extension/metadata.json "$EXTENSION_DIR/"

# 复制 C++ 后端
mkdir -p "$EXTENSION_DIR/backend/build"
cp backend/build/musicfox-lyrics-backend "$EXTENSION_DIR/backend/build/"

echo "Build completed! Extension installed to: $EXTENSION_DIR"
echo ""
echo "To enable the extension:"
echo "1. Restart GNOME Shell (Alt+F2, type 'r', press Enter)"
echo "2. Enable extension: gnome-extensions enable go-musicfox-lyrics@amazzy24128.github.io"
