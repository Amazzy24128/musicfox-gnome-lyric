#include <iostream>
#include <gio/gio.h>
#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <chrono>
#include <unistd.h>
#include <sstream> // 确保包含 sstream

#include "music-info-service-generated.h"

// --- 数据结构、全局变量 (与之前相同) ---
struct LyricLine { gint64 timestamp_us; std::string text; };
typedef struct { std::string trackid; std::string artist; std::string title; gint64 duration_us; bool is_playing; } music_t;
typedef struct { GDBusConnection *connection; std::string bus_name; } AppContext;

static music_t g_current_music = {};
static std::vector<LyricLine> g_parsed_lyrics;
static std::string g_current_lyric_text = "";
static std::chrono::steady_clock::time_point g_last_sync_time;
static gint64 g_last_sync_position_us = 0;
static MusicInfoServicePlayer *g_player_skeleton = nullptr;
static GDBusObjectManagerServer *g_object_manager = nullptr;

// --- 函数声明 (与之前相同) ---
static std::vector<LyricLine> parse_lrc(const std::string &lrc_text);
static void update_and_emit_signal(gint64 display_position_us);
static gboolean sync_position_from_dbus(gpointer user_data);
static gboolean predictive_update(gpointer user_data);
std::string find_musicfox_bus_name();

// (find_musicfox_bus_name, parse_lrc, update_and_emit_signal 函数与之前版本完全相同, 为简洁省略)
std::string find_musicfox_bus_name() {
    GError *error = nullptr; GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection) { return ""; }
    GVariant *result = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", nullptr, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    std::string bus_name;
    if (result) {
        GVariantIter *iter; g_variant_get(result, "(as)", &iter); gchar *name;
        while (g_variant_iter_next(iter, "s", &name)) {
            if (std::string(name).find("org.mpris.MediaPlayer2.musicfox") == 0) { bus_name = name; g_free(name); break; }
            g_free(name);
        }
        g_variant_iter_free(iter); g_variant_unref(result);
    }
    g_object_unref(connection); return bus_name;
}
std::vector<LyricLine> parse_lrc(const std::string &lrc_text) {
    std::vector<LyricLine> lyrics; std::regex lrc_regex(R"(\[(\d{2}):(\d{2})\.(\d{2,3})\](.*))"); std::smatch match; std::stringstream ss(lrc_text); std::string line;
    while (std::getline(ss, line)) {
        if (std::regex_match(line, match, lrc_regex)) {
            gint64 minutes = std::stoll(match[1].str()); gint64 seconds = std::stoll(match[2].str()); gint64 milliseconds = (match[3].str().length() == 2) ? std::stoll(match[3].str()) * 10 : std::stoll(match[3].str());
            gint64 total_microseconds = (minutes * 60 + seconds) * 1000000 + milliseconds * 1000;
            std::string text = match[4].str(); text.erase(0, text.find_first_not_of(" \t\r\n")); text.erase(text.find_last_not_of(" \t\r\n") + 1);
            if (!text.empty()) { lyrics.push_back({total_microseconds, text}); }
        }
    }
    std::sort(lyrics.begin(), lyrics.end(), [](const LyricLine& a, const LyricLine& b){ return a.timestamp_us < b.timestamp_us; });
    return lyrics;
}
void update_and_emit_signal(gint64 display_position_us) {
    if (!g_player_skeleton) return;
    music_info_service_player_set_artist(g_player_skeleton, g_current_music.artist.c_str());
    music_info_service_player_set_title(g_player_skeleton, g_current_music.title.c_str());
    music_info_service_player_set_is_playing(g_player_skeleton, g_current_music.is_playing);
    music_info_service_player_set_current_lyric(g_player_skeleton, g_current_lyric_text.c_str());
    music_info_service_player_set_duration(g_player_skeleton, static_cast<double>(g_current_music.duration_us) / 1000000.0);
    music_info_service_player_set_position(g_player_skeleton, static_cast<double>(display_position_us) / 1000000.0);
    music_info_service_player_emit_state_changed(g_player_skeleton, g_current_music.artist.c_str(), g_current_music.title.c_str(), g_current_music.is_playing, g_current_lyric_text.c_str(), static_cast<double>(g_current_music.duration_us) / 1000000.0, static_cast<double>(display_position_us) / 1000000.0);
}


// --- 关键修正：移植自 mpris_listener.cpp 的健壮逻辑 ---
extern "C" void on_any_signal(GDBusConnection *connection, const gchar *sender, const gchar *path, const gchar *iface_name, const gchar *signal, GVariant *params, gpointer data) {
    if (g_strcmp0(signal, "PropertiesChanged") != 0 || !params) return;

    const char *prop_iface = nullptr;
    GVariant *changed_props = nullptr;
    g_variant_get(params, "(&s@a{sv}@as)", &prop_iface, &changed_props, nullptr);

    // 1. 创建全新的、干净的临时变量
    music_t temp_music = {};
    std::vector<LyricLine> temp_lyrics;

    // 2. 用信号中的新数据填充临时变量
    GVariant *status_variant = g_variant_lookup_value(changed_props, "PlaybackStatus", G_VARIANT_TYPE_STRING);
    if (status_variant) {
        temp_music.is_playing = (g_strcmp0(g_variant_get_string(status_variant, nullptr), "Playing") == 0);
        g_variant_unref(status_variant);
    } else {
        temp_music.is_playing = g_current_music.is_playing; // 如果信号没给，继承旧值
    }

    GVariant *meta_variant = g_variant_lookup_value(changed_props, "Metadata", G_VARIANT_TYPE("a{sv}"));
    if (meta_variant) {
        GVariantIter miter; gchar *mkey; GVariant *mval;
        g_variant_iter_init(&miter, meta_variant);
        while (g_variant_iter_next(&miter, "{sv}", &mkey, &mval)) {
            if (g_strcmp0(mkey, "mpris:trackid") == 0) temp_music.trackid = g_variant_get_string(mval, nullptr);
            else if (g_strcmp0(mkey, "xesam:title") == 0) temp_music.title = g_variant_get_string(mval, nullptr);
            else if (g_strcmp0(mkey, "xesam:artist") == 0 && g_variant_is_of_type(mval, G_VARIANT_TYPE("as")) && g_variant_n_children(mval) > 0) temp_music.artist = g_variant_get_string(g_variant_get_child_value(mval, 0), nullptr);
            else if (g_strcmp0(mkey, "mpris:length") == 0) temp_music.duration_us = g_variant_get_int64(mval);
            else if (g_strcmp0(mkey, "xesam:asText") == 0) {
                const char* lrc = g_variant_get_string(mval, nullptr);
                if (lrc) temp_lyrics = parse_lrc(lrc);
            }
            g_free(mkey); g_variant_unref(mval);
        }
        g_variant_unref(meta_variant);
    } else {
        // 如果信号没给元数据，继承旧的元数据和歌词
        temp_music.trackid = g_current_music.trackid;
        temp_music.artist = g_current_music.artist;
        temp_music.title = g_current_music.title;
        temp_music.duration_us = g_current_music.duration_us;
        temp_lyrics = g_parsed_lyrics;
    }

    // 3. 判断是否是新歌
    bool is_new_track = (!temp_music.trackid.empty() && temp_music.trackid != g_current_music.trackid);

    // 4. 无条件用临时数据整体覆盖全局数据，保证状态原子性更新
    g_current_music = temp_music;
    g_parsed_lyrics = temp_lyrics;

    // 5. 如果是新歌，重置当前歌词文本并立即同步时间
    if (is_new_track) {
        g_current_lyric_text = "";
        sync_position_from_dbus(data);
    } else {
        // 如果不是新歌，但播放状态变了（例如从暂停到播放），也同步一次时间
        bool playback_state_changed = temp_music.is_playing != g_current_music.is_playing;
        if(playback_state_changed) {
             sync_position_from_dbus(data);
        }
    }
    
    if (changed_props) g_variant_unref(changed_props);
}


static gboolean sync_position_from_dbus(gpointer user_data) {
    AppContext* context = static_cast<AppContext*>(user_data);
    GError *error = nullptr;
    GVariant *result = g_dbus_connection_call_sync(context->connection, context->bus_name.c_str(), "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "Position"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    if (result) {
        GVariant *inner_variant; g_variant_get(result, "(v)", &inner_variant);
        g_last_sync_position_us = g_variant_get_int64(inner_variant);
        g_last_sync_time = std::chrono::steady_clock::now();
        g_variant_unref(inner_variant); g_variant_unref(result);
    } else if (error) { g_error_free(error); }
    return G_SOURCE_CONTINUE; 
}
static gboolean predictive_update(gpointer user_data) {
    gint64 predicted_position_us = g_last_sync_position_us;
    if (g_current_music.is_playing) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - g_last_sync_time).count();
        predicted_position_us += elapsed_us;
    }
    std::string new_lyric = "";
    if (!g_parsed_lyrics.empty()) {
        int lyric_index = -1;
        for (size_t i = 0; i < g_parsed_lyrics.size(); ++i) {
            if (predicted_position_us >= g_parsed_lyrics[i].timestamp_us) { lyric_index = i; } 
            else { break; }
        }
        if (lyric_index != -1) { new_lyric = g_parsed_lyrics[lyric_index].text; }
    }
    g_current_lyric_text = new_lyric;
    update_and_emit_signal(predicted_position_us);
    return G_SOURCE_CONTINUE; 
}
static void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    std::cout << "D-Bus service name acquired: " << name << std::endl;
}
static void on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    std::cerr << "D-Bus service name lost: " << name << std::endl;
    GMainLoop *loop = (GMainLoop*)user_data;
    g_main_loop_quit(loop);
}


int main()
{
    std::cout << "Starting Music Info D-Bus Service..." << std::endl;
    std::string mpris_bus_name = "";
    while (mpris_bus_name.empty()) {
        mpris_bus_name = find_musicfox_bus_name();
        if (mpris_bus_name.empty()) { struct timespec ts = {0, 500000000L}; nanosleep(&ts, nullptr); }
    }
    std::cout << "Found musicfox at: " << mpris_bus_name << std::endl;

    GError *error = nullptr;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection) { std::cerr << "Failed to get session bus." << std::endl; return 1; }
    
    AppContext context = { connection, mpris_bus_name };
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);

    const char* object_manager_path = "/org/amazzy24128/MusicInfoService";
    const char* object_path = "/org/amazzy24128/MusicInfoService/Player";

    g_object_manager = g_dbus_object_manager_server_new(object_manager_path);
    GDBusObjectSkeleton *object_skeleton = g_dbus_object_skeleton_new(object_path);
    g_player_skeleton = music_info_service_player_skeleton_new();
    g_dbus_object_skeleton_add_interface(object_skeleton, G_DBUS_INTERFACE_SKELETON(g_player_skeleton));
    g_object_unref(g_player_skeleton);
    g_dbus_object_manager_server_export(g_object_manager, object_skeleton);
    g_object_unref(object_skeleton);
    g_dbus_object_manager_server_set_connection(g_object_manager, connection);
    
    g_bus_own_name(G_BUS_TYPE_SESSION, "org.amazzy24128.MusicInfoService", G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, on_name_acquired, on_name_lost, loop, nullptr);

    guint mpris_sub_id = g_dbus_connection_signal_subscribe(connection, mpris_bus_name.c_str(), "org.freedesktop.DBus.Properties", "PropertiesChanged", "/org/mpris/MediaPlayer2", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_any_signal, &context, nullptr);
    guint sync_timer_id = g_timeout_add_seconds(1, sync_position_from_dbus, &context);
    guint display_timer_id = g_timeout_add(100, predictive_update, &context);

    sync_position_from_dbus(&context);

    std::cout << "Service is running. Waiting for events..." << std::endl;
    g_main_loop_run(loop);

    g_source_remove(display_timer_id);
    g_source_remove(sync_timer_id);
    g_dbus_connection_signal_unsubscribe(connection, mpris_sub_id);
    g_main_loop_unref(loop);
    g_object_unref(g_object_manager);
    g_object_unref(connection);
    
    std::cout << "Service stopped." << std::endl;
    return 0;
}