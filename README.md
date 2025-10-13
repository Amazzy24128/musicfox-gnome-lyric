# musicfox-gnome-extension

一个适用于 GNOME 桌面环境的歌词扩展，支持从 [musicfox](https://github.com/anythink/musicfox) 获取当前播放的歌曲信息，并在桌面上实时显示歌词。我的第一个项目，很多不足之处，欢迎各路大佬指正QWQ！
目前项目仅在 GNOME 46 + Ubuntu24.04LTS(x86)下测试通过，其他环境未测试，欢迎反馈。

## 安装方法
克隆本项目至合适位置，进入项目根目录，运行：
```bash
./install.sh
```
即可在路径 `~/.local/share/gnome-shell/extensions/musicfox-lyric@amazzy24128/` 下安装扩展。
如需更新，重新运行上述命令即可覆盖安装，安装内容仅为三个文件，分别为：
- `metadata.json`：扩展元数据
- `extension.js`：扩展主脚本
- `music-info-service`：后端服务脚本

如项目有更新，重新执行上述命令即可覆盖安装。

## 功能

- 实时获取 musicfox 播放信息
- 桌面歌词显示，支持自定义样式
- 与 GNOME 桌面环境无缝集成
- 支持多种歌词源和解析
- 简单配置，开箱即用

## 使用方法

1. 确保已安装并运行 [musicfox](https://github.com/anythink/musicfox)。
2. 安装本扩展，重启 GNOME Shell 或注销后重新登录。
3. 在扩展设置中进行自定义（如有）。
4. 播放音乐后，桌面即会显示歌词。

## 依赖

- GNOME Shell 45–47
- bash
- musicfox（需提前安装并配置）

## 贡献

欢迎提交 issue 或 PR。  
如有建议或 bug，欢迎反馈！

## License

MIT License