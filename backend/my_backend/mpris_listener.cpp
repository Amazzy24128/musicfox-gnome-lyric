#include <iostream>
#include <gio/gio.h>
#include <string>
#include <thread>    // 新增：用于后台线程
#include <mutex>     // 新增：用于保护 shared data
#include <atomic>    // 新增：停止标志
#include <chrono>    // 新增：用于 sleep
#include <ctime>
#include <sstream>
#include <iomanip>

// 新增：在 read_music 使用之前声明 helper 函数，避免未声明错误
static std::string format_rfc3339_from_unix_seconds(gint64 seconds);

typedef struct
{
    std::string artist;
    std::string title;
    std::string instance_lyrics; // 现在的歌词
    std::string started_time;       // 开始时间
    int duration;                // 歌曲总时长，单位秒
    int position;                // 当前播放位置，单位秒
    bool is_playing;             // 是否正在播放
} music_t;

// 查找 musicfox 的 MPRIS D-Bus 名称
std::string find_musicfox_bus_name()
{
    GError *error = nullptr;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection)
    {
        std::cerr << "Failed to get session bus: " << (error ? error->message : "Unknown error") << std::endl;
        if (error)
            g_error_free(error);
        return "";
    }
    GVariant *result = g_dbus_connection_call_sync(
        connection,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "ListNames",
        nullptr,
        G_VARIANT_TYPE("(as)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        &error);
    std::string bus_name;
    if (result)
    {
        GVariantIter *iter;
        g_variant_get(result, "(as)", &iter);
        gchar *name;
        while (g_variant_iter_next(iter, "s", &name))
        {
            std::string sname(name);
            if (sname.find("org.mpris.MediaPlayer2.musicfox") == 0)
            {
                bus_name = sname;
                g_free(name);
                break;
            }
            g_free(name);
        }
        g_variant_iter_free(iter);
        g_variant_unref(result);
    }
    else
    {
        std::cerr << "Failed to list D-Bus names: " << (error ? error->message : "Unknown error") << std::endl;
        if (error)
            g_error_free(error);
    }
    g_object_unref(connection);
    return bus_name;
}
music_t read_music(GDBusConnection *connection, const std::string &bus_name)
{ // 主动读取 Metadata（a{sv}）内容，并且将各个信息存入 music_t 结构体中
    music_t music = {};
    GError *error = nullptr;
    GDBusProxy *proxy = g_dbus_proxy_new_sync(
        connection,
        G_DBUS_PROXY_FLAGS_NONE,
        nullptr,
        bus_name.c_str(),
        "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player",
        nullptr,
        &error);
    if (!proxy) // 检查 proxy 是否创建成功
    {
        std::cerr << "Failed to create proxy: " << (error ? error->message : "Unknown error") << std::endl;
        if (error)
            g_error_free(error);
        return music;
    }

    GVariant *metadata_variant = g_dbus_proxy_get_cached_property(proxy, "Metadata");
    if (metadata_variant)
    {
        // Metadata 的类型通常是 a{sv}（字典），使用 "{sv}" 遍历键值对
        GVariantIter iter;
        gchar *key = nullptr;
        GVariant *value = nullptr;
        g_variant_iter_init(&iter, metadata_variant);
        //开始遍历歌曲信息
        while (g_variant_iter_next(&iter, "{sv}", &key, &value))
        {
            if (g_strcmp0(key, "xesam:artist") == 0)
            {
                // xesam:artist 通常是字符串数组 (as)，取第一个元素作为艺术家名
                if (g_variant_is_of_type(value, G_VARIANT_TYPE("as")))
                {
                    GVariantIter aiter;
                    gchar *artist_name = nullptr;
                    g_variant_iter_init(&aiter, value);
                    if (g_variant_iter_next(&aiter, "s", &artist_name))
                    {
                        music.artist = artist_name;
                        g_free(artist_name);
                    }
                }
                else if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
                {
                    music.artist = g_variant_get_string(value, nullptr);
                }
            }
            else if (g_strcmp0(key, "xesam:title") == 0) // 歌曲标题
            {
                if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
                {
                    music.title = g_variant_get_string(value, nullptr);
                }
            }
            else if (g_strcmp0(key, "mpris:length") == 0 || g_strcmp0(key, "length") == 0) // 歌曲时长
            {
                // mpris:length 通常为 microseconds（int64）
                if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64))
                {
                    gint64 us = g_variant_get_int64(value);
                    music.duration = static_cast<int>(us / 1000000); // 转换为秒
                }
                else if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT32))
                {
                    music.duration = g_variant_get_int32(value);
                }
            }
            else if (g_strcmp0(key, "xesam:asText") == 0) // 歌词
            {
                if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
                {
                    music.instance_lyrics = g_variant_get_string(value, nullptr);
                }
            }
            // 下面三个量似乎得从被动接收的 signal 里获取
            // else if (g_strcmp0(key, "mpris:position") == 0 || g_strcmp0(key, "position") == 0) // 当前播放位置
            // {
            //     // mpris:position 通常为 microseconds（int64）
            //     if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64))
            //     {
            //         gint64 us = g_variant_get_int64(value);
            //         music.position = static_cast<int>(us / 1000000); // 转换为秒
            //     }
            //     else if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT32))
            //     {
            //         music.position = g_variant_get_int32(value);
            //     }
            // }
            // else if (g_strcmp0(key, "PlaybackStatus") == 0) // 播放状态
            // {
            //     if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
            //     {
            //         const char *status = g_variant_get_string(value, nullptr);
            //         music.is_playing = (strcmp(status, "Playing") == 0);
            //     }
            // }
            // else if (g_strcmp0(key, "xesam:lastPlayed") == 0) // 开始时间（兼容多种类型）
            // {
            //     // 调试：打印类型和值，帮助确认 metadata 实际内容（可在调试完成后移除）
            //     {
            //         gchar *valstr = g_variant_print(value, TRUE);
            //         std::cerr << "DEBUG: key=" << key << " type=" << g_variant_get_type_string(value)
            //                   << " value=" << (valstr ? valstr : "(null)") << std::endl;
            //         if (valstr) g_free(valstr);
            //     }

            //     // 可能的情况：
            //     // - 字符串：直接作为 ISO8601/RFC3339 字符串使用
            //     // - int64/uint64：常见为 microseconds since epoch -> 转为 RFC3339
            //     if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
            //         music.started_time = g_variant_get_string(value, nullptr);
            //     }
            //     else if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) {
            //         gint64 us = g_variant_get_int64(value);
            //         music.started_time = format_rfc3339_from_unix_seconds(us / 1000000);
            //     }
            //     else if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT64)) {
            //         guint64 us = g_variant_get_uint64(value);
            //         music.started_time = format_rfc3339_from_unix_seconds(static_cast<gint64>(us / 1000000));
            //     }
            //     else {
            //         // 未知类型：将其打印为字符串备用
            //         gchar *valstr = g_variant_print(value, TRUE);
            //         if (valstr) {
            //             music.started_time = valstr;
            //             g_free(valstr);
            //         }
            //     }
            // }

            g_free(key);            // 释放 g_variant_iter_next 分配的 key
            g_variant_unref(value); // 释放 value
        }

        g_variant_unref(metadata_variant); // 释放 metadata
    }

    g_object_unref(proxy); // 释放 proxy
    return music;
}

void display_music(const music_t &music)
{
    std::cout << "Artist: " << music.artist << std::endl;
    std::cout << "Title: " << music.title << std::endl;
    std::cout << "Duration: " << music.duration << " seconds" << std::endl;
    std::cout << "Position: " << music.position << " seconds" << std::endl;
    std::cout << "Is Playing: " << (music.is_playing ? "Yes" : "No") << std::endl;
    std::cout << "Lyrics: " << music.instance_lyrics << std::endl;
    std::cout << "Started Time: " << music.started_time << std::endl;
    
}

// 统一信号处理函数，打印所有信号内容
extern "C" void on_any_signal(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
    // std::cout << "==== Signal received ====" << std::endl;
    // std::cout << "Sender: " << (sender_name ? sender_name : "(null)") << std::endl;
    // std::cout << "ObjectPath: " << (object_path ? object_path : "(null)") << std::endl;
    // std::cout << "Interface: " << (interface_name ? interface_name : "(null)") << std::endl;
    // std::cout << "SignalName: " << (signal_name ? signal_name : "(null)") << std::endl;
    // // 打印参数（GVariant的字符串表示）
    // gchar *param_str = g_variant_print(parameters, TRUE);
    // std::cout << "Parameters: " << (param_str ? param_str : "(null)") << std::endl;
    // g_free(param_str);
    // std::cout << "========================" << std::endl
    //           << std::endl;
}

// 全局缓存与同步控制，用于后台线程写入并主线程/其它消费者读取
static music_t g_current_music = {};       // 全局当前音乐信息缓存
static std::mutex g_music_mutex;           // 保护 g_current_music 的互斥锁
static std::atomic<bool> g_stop_reader{false}; // 停止后台读取线程的标志

// 后台读取函数：在独立线程中周期性同步读取 metadata 并更新全局缓存
static void background_reader(GDBusConnection *connection, const std::string &bus_name, int interval_ms = 1000)
{
    static int cnt = 0;
    while (!g_stop_reader.load(std::memory_order_relaxed))
    {
        // 同步读取当前音乐信息
        music_t music = read_music(connection, bus_name);

        // 将读取到的数据写入全局缓存（加锁保护）
        {
            std::lock_guard<std::mutex> lk(g_music_mutex);
            g_current_music = music;
            cnt ++ ;
            std::cout << "Background read count: " << cnt << std::endl;
            display_music(g_current_music);
        }
        
        // 睡眠一段时间，下次再读取（间隔可调整）
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

// helper: 将 unix 秒转换为 RFC3339（含本地时区偏移）字符串
static std::string format_rfc3339_from_unix_seconds(gint64 seconds) {
    time_t t = static_cast<time_t>(seconds);
    struct tm loc_tm;
    localtime_r(&t, &loc_tm);
    char datetime[64];
    strftime(datetime, sizeof(datetime), "%Y-%m-%dT%H:%M:%S", &loc_tm);

    struct tm gmt_tm;
    gmtime_r(&t, &gmt_tm);

    time_t lt = mktime(&loc_tm);
    time_t gt = mktime(&gmt_tm);
    long offset = static_cast<long>(difftime(lt, gt)); // seconds east of UTC

    if (offset == 0) {
        return std::string(datetime) + "Z";
    } else {
        char sign = offset >= 0 ? '+' : '-';
        long absoff = std::labs(offset);
        int hh = static_cast<int>(absoff / 3600);
        int mm = static_cast<int>((absoff % 3600) / 60);
        char tz[8];
        snprintf(tz, sizeof(tz), "%c%02d:%02d", sign, hh, mm);
        return std::string(datetime) + tz;
    }
}

int main()
{
    std::string bus_name = find_musicfox_bus_name();
    if (bus_name.empty())
    {
        std::cerr << "No musicfox MPRIS service found." << std::endl;
        return 1;
    }
    GError *error = nullptr;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection)
    {
        std::cerr << "Failed to get session bus: " << (error ? error->message : "Unknown error") << std::endl;
        if (error)
            g_error_free(error);
        return 1;
    }

    // 订阅所有musicfox相关信号（所有接口、所有信号名）
    guint subscription_id = g_dbus_connection_signal_subscribe(
        connection,
        bus_name.c_str(), // 只监听 musicfox
        nullptr,          // 所有接口
        nullptr,          // 所有信号名
        nullptr,          // 所有对象路径
        nullptr,          // arg0
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_any_signal,   // 回调函数
        nullptr,
        nullptr);

    std::cout << "Listening for ALL musicfox MPRIS signals..." << std::endl;

    // 启动后台读取线程：定期同步读取歌曲数据并更新全局缓存
    std::thread reader_thread(background_reader, connection, bus_name, 1000 /*ms*/);

    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);

    // 程序退出时清理：停止后台线程并等待其结束
    g_stop_reader.store(true, std::memory_order_relaxed);
    if (reader_thread.joinable())
        reader_thread.join();

    // 程序退出时清理
    g_dbus_connection_signal_unsubscribe(connection, subscription_id);
    g_main_loop_unref(loop);
    g_object_unref(connection);
    return 0;
}