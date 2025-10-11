import St from 'gi://St';
import Gio from 'gi://Gio';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';

// --- 关键修正：确保这个 XML 字符串是完整且正确的 ---
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
        this._proxy = null;
        this._signalId = null;
        this._indicator = null;
        this._label = null;
    }

    enable() {
        this._log('Enabling with FINAL correct code...');

        this._proxy = new MusicInfoProxy(
            Gio.DBus.session,
            'org.amazzy24128.MusicInfoService',
            '/org/amazzy24128/MusicInfoService/Player',
            (proxy, error) => {
                if (error) { this._logError(`D-Bus proxy creation error: ${error.message}`); return; }
                this._connectSignal();
                this._initialUpdate();
            }
        );
        
        this._indicator = new PanelMenu.Button(0.0, 'Musicfox Lyric', false);
        
        // 我们暂时不设置 y_align，以确保能运行
        this._label = new St.Label({ text: 'Loading...' });

        this._indicator.add_child(this._label);
        Main.panel.addToStatusArea(this.uuid, this._indicator, 0, 'left');
    }

    disable() {
        this._log('Disabling...');
        if (this._signalId) { this._proxy.disconnectSignal(this._signalId); this._signalId = null; }
        if (this._indicator) { this._indicator.destroy(); this._indicator = null; }
        this._label = null; this._proxy = null;
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
        let displayText = lyric || `${artist} - ${title}`;
        if (!artist && !title) { displayText = 'Musicfox Lyric'; }
        const icon = isPlaying ? '❚❚' : '▶';
        this._label.set_text(`${icon} ${displayText}`);
    }

    _log(message) { console.log(`[${this.uuid}] ${message}`); }
    _logError(message) { console.error(`[${this.uuid}] ${message}`); }
}