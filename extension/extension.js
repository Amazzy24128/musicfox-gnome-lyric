
const { GObject, St, Clutter, Gio, GLib } = imports.gi;
const Main = imports.ui.main;
const PanelMenu = imports.ui.panelMenu;
const ExtensionUtils = imports.misc.extensionUtils;

const LyricIndicator = GObject.registerClass(
    class LyricIndicator extends PanelMenu.Button {
        _init() {
            super._init(0.0, 'MusicFox Lyrics');
            
            this._label = new St.Label({
                text: '♪ Starting...',
                style_class: 'musicfox-lyric-label',
                y_align: Clutter.ActorAlign.CENTER
            });
            
            this.add_child(this._label);
            
            this._backendProcess = null;
            this._dataStream = null;
            this._backendPid = 0;
            
            this._startBackend();
        }
        
        _startBackend() {
            const extension = ExtensionUtils.getCurrentExtension();
            const backendPath = extension.dir.get_child('backend').get_child('build').get_child('musicfox-lyrics-backend').get_path();
            
            try {
                let [success, pid, stdin, stdout, stderr] = GLib.spawn_async_with_pipes(
                    null, // working directory
                    [backendPath], // command
                    null, // environment
                    GLib.SpawnFlags.SEARCH_PATH | GLib.SpawnFlags.DO_NOT_REAP_CHILD,
                    null // child setup
                );
                
                if (success) {
                    this._backendPid = pid;
                    this._setupDataStream(stdout);
                    this._setupErrorStream(stderr);
                    
                    // 监听进程退出
                    GLib.child_watch_add(GLib.PRIORITY_DEFAULT, pid, (pid, status) => {
                        this._onBackendExit(status);
                    });
                    
                    this._label.set_text('♪ Connected');
                } else {
                    this._showError('Failed to start C++ backend');
                }
            } catch (e) {
                this._showError(`Backend error: ${e.message}`);
            }
        }
        
        _setupDataStream(stdout) {
            this._dataStream = new Gio.DataInputStream({
                base_stream: new Gio.UnixInputStream({ fd: stdout })
            });
            
            this._readNextLine();
        }
        
        _setupErrorStream(stderr) {
            const errorStream = new Gio.DataInputStream({
                base_stream: new Gio.UnixInputStream({ fd: stderr })
            });
            
            // 读取错误输出
            this._readErrorOutput(errorStream);
        }
        
        _readNextLine() {
            if (!this._dataStream) return;
            
            this._dataStream.read_line_async(GLib.PRIORITY_DEFAULT, null, (stream, result) => {
                try {
                    const [line] = stream.read_line_finish(result);
                    if (line) {
                        const dataStr = new TextDecoder().decode(line);
                        const data = JSON.parse(dataStr);
                        this._updateDisplay(data);
                    }
                    
                    // 继续读取下一行
                    this._readNextLine();
                } catch (e) {
                    log(`Error parsing backend data: ${e.message}`);
                    this._showError('Data parsing error');
                }
            });
        }
        
        _readErrorOutput(errorStream) {
            errorStream.read_line_async(GLib.PRIORITY_DEFAULT, null, (stream, result) => {
                try {
                    const [line] = stream.read_line_finish(result);
                    if (line) {
                        const errorMsg = new TextDecoder().decode(line);
                        log(`Backend error: ${errorMsg}`);
                    }
                    
                    // 继续读取错误输出
                    this._readErrorOutput(errorStream);
                } catch (e) {
                    // 忽略错误流的读取错误
                }
            });
        }
        
        _updateDisplay(data) {
            if (!data.is_playing) {
                this._label.set_text('♪ Not playing');
                return;
            }
            
            if (data.current_lyric) {
                let displayText = data.current_lyric;
                
                // 限制显示长度
                const maxLength = 60;
                if (displayText.length > maxLength) {
                    displayText = displayText.substring(0, maxLength) + '...';
                }
                
                this._label.set_text(`♪ ${displayText}`);
                
                // 设置 tooltip 显示完整信息
                const tooltipText = [
                    data.song_title ? `🎵 ${data.song_title}` : '',
                    data.artist ? `🎤 ${data.artist}` : '',
                    `📝 ${data.current_lyric}`,
                    data.next_lyric ? `⏭️ ${data.next_lyric}` : ''
                ].filter(Boolean).join('\n');
                
                this.set_accessible_description(tooltipText);
            } else if (data.song_title) {
                let displayText = data.song_title;
                if (data.artist) {
                    displayText = `${data.artist} - ${data.song_title}`;
                }
                
                if (displayText.length > 50) {
                    displayText = displayText.substring(0, 47) + '...';
                }
                
                this._label.set_text(`♪ ${displayText}`);
            } else {
                this._label.set_text('♪ No lyrics');
            }
        }
        
        _showError(message) {
            this._label.set_text(`♪ ${message}`);
        }
        
        _onBackendExit(status) {
            this._label.set_text('♪ Backend stopped');
            this._backendPid = 0;
        }
        
        destroy() {
            if (this._dataStream) {
                this._dataStream.close(null);
                this._dataStream = null;
            }
            
            if (this._backendPid > 0) {
                GLib.spawn_close_pid(this._backendPid);
                this._backendPid = 0;
            }
            
            super.destroy();
        }
    }
);

let lyricIndicator;

function init() {
    log('Initializing MusicFox Lyrics extension');
}

function enable() {
    log('Enabling MusicFox Lyrics extension');
    lyricIndicator = new LyricIndicator();
    Main.panel.addToStatusArea('musicfox-lyrics', lyricIndicator);
}

function disable() {
    log('Disabling MusicFox Lyrics extension');
    if (lyricIndicator) {
        lyricIndicator.destroy();
        lyricIndicator = null;
    }
}
