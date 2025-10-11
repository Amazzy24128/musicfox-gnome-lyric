import St from 'gi://St';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib'; // 1. 引入 GLib
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';

const MusicInfoInterface = `
<node>
  <interface name="org.amazzy24128.MusicInfoService.Player">
    <property name="Artist" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="IsPlaying" type="b" access="read"/>
    <property name="CurrentLyric" type="s" access="read"/>
    <property name="Duration" type="d" access="read"/>
    <property name="Position" type="d" access="read"/>
    <signal name="StateChanged">
      <arg name="artist" type="s"/>
      <arg name="title" type="s"/>
      <arg name="is_playing" type="b"/>
      <arg name="current_lyric" type="s"/>
      <arg name="duration" type="d"/>
      <arg name="position" type="d"/>
    </signal>
  </interface>
</node>`;

const MusicInfoProxy = Gio.DBusProxy.makeProxyWrapper(MusicInfoInterface);

export default class MusicfoxLyricExtension extends Extension {
    constructor(metadata) {
        super(metadata);
        
        // UI 和 D-Bus 相关属性
        this._proxy = null;
        this._signalId = null;
        this._indicator = null;
        this._label = null;

        // --- 新增：后端进程管理属性 ---
        this._backendPid = null; // 用于存储后端进程的PID
        
        // --- 核心修改：使用 this.path 动态构建后端可执行文件的路径 ---
        // this.path 是插件的安装目录，我们用它来定位同目录下的可执行文件。
        this._backendPath = GLib.build_filenamev([this.path, 'music-info-service']);
    }

    enable() {
        this._log('Enabling extension...');
        
        // --- 新增：在插件启用时，首先启动后端服务 ---
        this._startBackend();

        // 创建D-Bus代理（保持不变）
        this._proxy = new MusicInfoProxy(
            Gio.DBus.session,
            'org.amazzy24128.MusicInfoService',
            '/org/amazzy24128/MusicInfoService/Player',
            (proxy, error) => {
                if (error) { this._logError(`D-Bus proxy creation error: ${error.message}`); return; }
                this._log('D-Bus proxy created successfully.');
                this._connectSignal();
                this._initialUpdate();
            }
        );
        
        // 创建UI元素（保持不变）
        this._indicator = new PanelMenu.Button(0.0, 'Musicfox Lyric', false);
        this._label = new St.Label({ text: 'Loading...' });
        this._indicator.add_child(this._label);
        Main.panel.addToStatusArea(this.uuid, this._indicator, 0, 'left');
    }

    disable() {
        this._log('Disabling extension...');
        
        // --- 新增：在插件禁用时，首先停止后端服务 ---
        this._stopBackend();

        // 清理UI和D-Bus连接（保持不变）
        if (this._signalId && this._proxy) { this._proxy.disconnectSignal(this._signalId); }
        if (this._indicator) { this._indicator.destroy(); }
        
        // 重置所有属性
        this._signalId = null;
        this._indicator = null;
        this._label = null;
        this._proxy = null;
    }

    // --- 新增：启动后端服务的方法 ---
    _startBackend() {
        // 检查文件是否存在且可执行
        const backendFile = Gio.File.new_for_path(this._backendPath);
        if (!backendFile.query_exists(null)) {
            this._logError(`Backend executable not found at: ${this._backendPath}`);
            Main.notify('Musicfox Lyric Error', 'Backend executable not found.');
            return;
        }

        try {
            // 使用 GLib.spawn_async_with_pipes 启动一个非阻塞的子进程
            let [success, pid, stdin, stdout, stderr] = GLib.spawn_async_with_pipes(
                this.path, // 将插件目录设置为工作目录
                [this._backendPath], // 命令和参数
                null, // 环境变量
                GLib.SpawnFlags.DO_NOT_REAP_CHILD, // Flags
                null  // 子进程设置函数
            );

            if (success) {
                this._backendPid = pid; // 保存PID
                this._log(`Backend service started with PID: ${this._backendPid}`);

                // 监听进程退出事件，以便处理意外崩溃
                GLib.child_watch_add(GLib.PRIORITY_DEFAULT, this._backendPid, (pid, status) => {
                    if (this._backendPid && this._backendPid === pid) {
                        this._log(`Backend service (PID: ${pid}) has exited unexpectedly.`);
                        this._backendPid = null; // 清理PID
                    }
                });

                // (强烈推荐) 读取后端的输出，用于调试
                this._setupStreamReader(stdout, 'Backend-STDOUT');
                this._setupStreamReader(stderr, 'Backend-STDERR');
            } else {
                throw new Error("GLib.spawn_async_with_pipes failed to start the process.");
            }
        } catch (e) {
            this._logError(`Error starting backend service: ${e.message}`);
            Main.notify('Musicfox Lyric Error', `Failed to start backend: ${e.message}`);
        }
    }

    // --- 新增：停止后端服务的方法 ---
    _stopBackend() {
        if (this._backendPid) {
            this._log(`Stopping backend service with PID: ${this._backendPid}`);
            try {
                // 发送 SIGTERM 信号，让程序有机会优雅退出
                GLib.spawn_command_line_sync(`kill ${this._backendPid}`);
            } catch (e) {
                this._logError(`Failed to stop backend PID ${this._backendPid}: ${e.message}. It might have already exited.`);
            } finally {
                this._backendPid = null; // 清理PID
            }
        }
    }

    _connectSignal() {
        this._signalId = this._proxy.connectSignal('StateChanged', (proxy, sender, [artist, title, isPlaying, lyric]) => {
            this._updateUI(artist, title, isPlaying, lyric);
        });
    }

    _initialUpdate() {
        try {
            this._updateUI(this._proxy.Artist, this._proxy.Title, this._proxy.IsPlaying, this._proxy.CurrentLyric);
        } catch (e) { this._logError(`Error on initial update: ${e}. Waiting for signal.`); }
    }

    _updateUI(artist, title, isPlaying, lyric) {
        if (!this._label) return;
        let displayText = lyric || (artist && title ? `${artist} - ${title}` : 'Music Ready');
        if (!artist && !title && !lyric) { displayText = 'Musicfox Lyric'; }
        const icon = isPlaying ? '❚❚' : '▶';
        this._label.set_text(`${icon} ${displayText}`);
    }
    
    // --- 新增：用于调试的日志读取器 ---
    _setupStreamReader(stream, prefix) {
        let dataInputStream = new Gio.DataInputStream({
            base_stream: new Gio.UnixInputStream({ fd: stream })
        });

        const readLine = () => {
            dataInputStream.read_line_async(GLib.PRIORITY_DEFAULT, null, (source, res) => {
                try {
                    if (!source) return;
                    const [lineBytes, length] = source.read_line_finish(res);
                    if (lineBytes !== null) {
                        const line = new TextDecoder().decode(lineBytes);
                        this._log(`[${prefix}] ${line}`);
                        readLine(); // 继续读取下一行
                    }
                } catch (e) {
                    this._log(`Stream reader for [${prefix}] closed.`);
                }
            });
        };
        readLine();
    }

    _log(message) { console.log(`[${this.uuid}] ${message}`); }
    _logError(message) { console.error(`[${this.uuid}] ${message}`); }
}