#include <iostream>
#include <gio/gio.h>
#include <string>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <regex>
#include <algorithm>
#include <chrono>

// --- 数据结构 ---
struct LyricLine
{
    gint64 timestamp_us;
    std::string text;
};

typedef struct
{
    std::string trackid;
    std::string artist;
    std::string title;
    float duration;
    gint64 position_us;
    bool is_playing;
} music_t;

typedef struct {
    GDBusConnection *connection;
    std::string bus_name;
} AppContext;

// --- 全局变量 ---
static music_t g_current_music = {};
static std::vector<LyricLine> g_parsed_lyrics;
static int g_current_lyric_index = -1;
static std::chrono::steady_clock::time_point g_last_sync_time;
static gint64 g_last_sync_position_us = 0;

// --- 函数声明 ---
static std::vector<LyricLine> parse_lrc(const std::string &lrc_text);
static void display_full_info(gint64 display_position_us);
static gboolean sync_position_from_dbus(gpointer user_data);
static gboolean predictive_update_and_display(gpointer user_data);
std::string find_musicfox_bus_name();


// (find_musicfox_bus_name 和 parse_lrc 函数与之前版本相同, 为简洁省略)
std::string find_musicfox_bus_name()
{
    GError *error = nullptr;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection) { std::cerr << "Failed to get session bus: " << (error ? error->message : "Unknown error") << std::endl; if (error) g_error_free(error); return ""; }
    GVariant *result = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", nullptr, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    std::string bus_name;
    if (result) {
        GVariantIter *iter;
        g_variant_get(result, "(as)", &iter);
        gchar *name;
        while (g_variant_iter_next(iter, "s", &name)) {
            if (std::string(name).find("org.mpris.MediaPlayer2.musicfox") == 0) { bus_name = name; g_free(name); break; }
            g_free(name);
        }
        g_variant_iter_free(iter);
        g_variant_unref(result);
    } else { std::cerr << "Failed to list D-Bus names: " << (error ? error->message : "Unknown error") << std::endl; if (error) g_error_free(error); }
    g_object_unref(connection);
    return bus_name;
}

std::vector<LyricLine> parse_lrc(const std::string &lrc_text) {
    std::vector<LyricLine> lyrics;
    std::regex lrc_regex(R"(\[(\d{2}):(\d{2})\.(\d{2,3})\](.*))");
    std::smatch match;
    std::stringstream ss(lrc_text);
    std::string line;
    while (std::getline(ss, line)) {
        if (std::regex_match(line, match, lrc_regex)) {
            gint64 minutes = std::stoll(match[1].str());
            gint64 seconds = std::stoll(match[2].str());
            gint64 milliseconds = (match[3].str().length() == 2) ? std::stoll(match[3].str()) * 10 : std::stoll(match[3].str());
            gint64 total_microseconds = (minutes * 60 + seconds) * 1000000 + milliseconds * 1000;
            std::string text = match[4].str();
            text.erase(0, text.find_first_not_of(" \t\r\n"));
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            if (!text.empty()) { lyrics.push_back({total_microseconds, text}); }
        }
    }
    std::sort(lyrics.begin(), lyrics.end(), [](const LyricLine& a, const LyricLine& b){ return a.timestamp_us < b.timestamp_us; });
    return lyrics;
}


// --- 核心函数 ---

void display_full_info(gint64 display_position_us) {
    std::cout << "\r\033[K"; 
    std::cout << (g_current_music.is_playing ? "❚❚" : "▶") << " "
              << g_current_music.artist << " - " << g_current_music.title
              << " [" << std::fixed << std::setprecision(0) << (display_position_us / 1000000.0f) << "s / " 
              << g_current_music.duration << "s] ";
    if (g_current_lyric_index != -1 && g_current_lyric_index < g_parsed_lyrics.size()) {
        std::cout << "   🎵 " << g_parsed_lyrics[g_current_lyric_index].text;
    }
    std::cout.flush();
}

// --- 关键修正：最终的、最健壮的信号处理逻辑 ---
extern "C" void on_any_signal(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
    if (g_strcmp0(signal_name, "PropertiesChanged") != 0) return;
    if (!parameters) return;

    const char *iface = nullptr;
    GVariant *changed_props = nullptr;
    g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed_props, nullptr);

    // 1. 准备一个临时的、干净的结构体来接收所有新数据
    music_t temp_music = {};
    std::vector<LyricLine> temp_lyrics;

    // 2. 从信号中解析所有可用的数据到临时结构体中
    GVariant *status_variant = g_variant_lookup_value(changed_props, "PlaybackStatus", G_VARIANT_TYPE_STRING);
    if (status_variant) {
        temp_music.is_playing = (g_strcmp0(g_variant_get_string(status_variant, nullptr), "Playing") == 0);
        g_variant_unref(status_variant);
    } else {
        temp_music.is_playing = g_current_music.is_playing; // 如果信号没给，就用旧的
    }

    GVariant *metadata_variant = g_variant_lookup_value(changed_props, "Metadata", G_VARIANT_TYPE("a{sv}"));
    if (metadata_variant) {
        GVariantIter miter;
        gchar *mkey = nullptr;
        GVariant *mval = nullptr;
        g_variant_iter_init(&miter, metadata_variant);
        while (g_variant_iter_next(&miter, "{sv}", &mkey, &mval)) {
            if (g_strcmp0(mkey, "mpris:trackid") == 0) temp_music.trackid = g_variant_get_string(mval, nullptr);
            else if (g_strcmp0(mkey, "xesam:title") == 0) temp_music.title = g_variant_get_string(mval, nullptr);
            else if (g_strcmp0(mkey, "xesam:artist") == 0 && g_variant_is_of_type(mval, G_VARIANT_TYPE("as")) && g_variant_n_children(mval) > 0) temp_music.artist = g_variant_get_string(g_variant_get_child_value(mval, 0), nullptr);
            else if (g_strcmp0(mkey, "mpris:length") == 0) temp_music.duration = static_cast<float>(g_variant_get_int64(mval)) / 1000000;
            else if (g_strcmp0(mkey, "xesam:asText") == 0) {
                const char* lrc = g_variant_get_string(mval, nullptr);
                if (lrc) temp_lyrics = parse_lrc(lrc);
            }
            g_free(mkey);
            g_variant_unref(mval);
        }
        g_variant_unref(metadata_variant);
    } else {
        // 如果信号没给元数据，就完全继承旧的元数据和歌词
        temp_music.trackid = g_current_music.trackid;
        temp_music.artist = g_current_music.artist;
        temp_music.title = g_current_music.title;
        temp_music.duration = g_current_music.duration;
        temp_lyrics = g_parsed_lyrics;
    }

    // 3. 所有数据解析完毕后，进行逻辑判断和原子性替换
    bool is_new_track = (temp_music.trackid != "" && temp_music.trackid != g_current_music.trackid);
    
    if (is_new_track) {
        std::cout << "\n--- New Track Loaded ---" << std::endl;
    }

    // 4. 无条件用临时数据整体覆盖全局数据
    g_current_music = temp_music;
    g_parsed_lyrics = temp_lyrics;

    // 5. 如果是新歌，重置歌词索引并立即同步时间
    if (is_new_track) {
        g_current_lyric_index = -1;
        sync_position_from_dbus(user_data);
    }
    
    if (changed_props) g_variant_unref(changed_props);
}

static gboolean sync_position_from_dbus(gpointer user_data) {
    AppContext* context = static_cast<AppContext*>(user_data);
    GError *error = nullptr;
    GVariant *result = g_dbus_connection_call_sync(context->connection, context->bus_name.c_str(), "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "Position"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    if (result) {
        GVariant *inner_variant;
        g_variant_get(result, "(v)", &inner_variant);
        g_current_music.position_us = g_variant_get_int64(inner_variant);
        g_last_sync_position_us = g_current_music.position_us;
        g_last_sync_time = std::chrono::steady_clock::now();
        g_variant_unref(inner_variant);
        g_variant_unref(result);
    } else if (error) { g_error_free(error); }
    return G_SOURCE_CONTINUE; 
}

static gboolean predictive_update_and_display(gpointer user_data) {
    gint64 predicted_position_us = g_last_sync_position_us;
    if (g_current_music.is_playing) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - g_last_sync_time).count();
        predicted_position_us += elapsed_us;
    }
    if (!g_parsed_lyrics.empty()) {
        int new_lyric_index = -1;
        for (size_t i = 0; i < g_parsed_lyrics.size(); ++i) {
            if (predicted_position_us >= g_parsed_lyrics[i].timestamp_us) {
                new_lyric_index = i;
            } else { break; }
        }
        if (new_lyric_index != g_current_lyric_index) {
            g_current_lyric_index = new_lyric_index;
        }
    }
    display_full_info(predicted_position_us);
    return G_SOURCE_CONTINUE; 
}

int main()
{
    std::cout << "MPRIS Listener started. Waiting for musicfox to start..." << std::endl;
    std::string bus_name = "";
    while (bus_name.empty()) {
        bus_name = find_musicfox_bus_name();
        if (bus_name.empty()) { struct timespec ts = {0, 500000000L}; nanosleep(&ts, nullptr); }
    }
    GError *error = nullptr;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection) { std::cerr << "Failed to get session bus: " << (error ? error->message : "Unknown error") << std::endl; if (error) g_error_free(error); return 1; }
    
    AppContext context = { connection, bus_name };

    guint sub_id = g_dbus_connection_signal_subscribe(connection, bus_name.c_str(), "org.freedesktop.DBus.Properties", "PropertiesChanged", "/org/mpris/MediaPlayer2", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_any_signal, &context, nullptr);
    guint sync_timer_id = g_timeout_add_seconds(1, sync_position_from_dbus, &context);
    guint display_timer_id = g_timeout_add(100, predictive_update_and_display, &context);

    std::cout << "musicfox started. Listening for MPRIS property changes..." << std::endl;
    sync_position_from_dbus(&context);
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);

    g_source_remove(display_timer_id);
    g_source_remove(sync_timer_id);
    g_dbus_connection_signal_unsubscribe(connection, sub_id);
    g_main_loop_unref(loop);
    g_object_unref(connection);
    
    std::cout << std::endl; 
    return 0;
}