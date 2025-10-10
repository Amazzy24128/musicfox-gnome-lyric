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

// 将全局缓存与同步控制的定义移动到这里，确保 on_any_signal 可以访问它们
static music_t g_current_music = {};       // 全局当前音乐信息缓存
static std::mutex g_music_mutex;           // 保护 g_current_music 的互斥锁
static std::atomic<bool> g_stop_reader{false}; // 停止后台读取线程的标志

// 统一信号处理函数，打印并处理 PropertiesChanged 信号内容
extern "C" void on_any_signal(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data)
{
    // 解析 PropertiesChanged 的 parameters: (s a{sv} as)
    if (!parameters)
        return;

    const char *iface = nullptr;
    GVariant *changed_props = nullptr;
    GVariant *invalidated = nullptr;

    // 获取参数：接口名（s）、changed props（a{sv}，作为 GVariant*）、invalidated array（as）
    g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed_props, &invalidated);

    // 遍历 changed_props 字典 a{sv}
    GVariantIter iter;
    gchar *prop_name = nullptr;
    GVariant *prop_value = nullptr;

    g_variant_iter_init(&iter, changed_props);
    while (g_variant_iter_next(&iter, "{sv}", &prop_name, &prop_value))
    {
        // 只处理 Player 接口的属性变化（可选）
        // if (iface && strcmp(iface, "org.mpris.MediaPlayer2.Player") != 0) { ... }

        // 保护全局状态写入
        {
            std::lock_guard<std::mutex> lk(g_music_mutex);

            // 新增：处理播放状态变化（Playing / Paused / Stopped）
            if (g_strcmp0(prop_name, "PlaybackStatus") == 0)
            {
                if (g_variant_is_of_type(prop_value, G_VARIANT_TYPE_STRING))
                {
                    const char *status = g_variant_get_string(prop_value, nullptr);
                    // Playing -> true，Paused/Stopped -> false
                    g_current_music.is_playing = (strcmp(status, "Playing") == 0);
                }
            }
            
            if (g_strcmp0(prop_name, "xesam:lastPlayed") == 0) // 播放开始时间 
            {
                // 支持多种类型：字符串（ISO8601/RFC3339）、int64/uint64（microseconds since epoch）、double（秒）
                if (g_variant_is_of_type(prop_value, G_VARIANT_TYPE_STRING)) {
                    g_current_music.started_time = g_variant_get_string(prop_value, nullptr);
                } else if (g_variant_is_of_type(prop_value, G_VARIANT_TYPE_INT64)) {
                    gint64 us = g_variant_get_int64(prop_value);
                    g_current_music.started_time = format_rfc3339_from_unix_seconds(us / 1000000);
                } else if (g_variant_is_of_type(prop_value, G_VARIANT_TYPE_UINT64)) {
                    guint64 us = g_variant_get_uint64(prop_value);
                    g_current_music.started_time = format_rfc3339_from_unix_seconds(static_cast<gint64>(us / 1000000));
                } else if (g_variant_is_of_type(prop_value, G_VARIANT_TYPE_DOUBLE)) {
                    double secs = g_variant_get_double(prop_value);
                    g_current_music.started_time = format_rfc3339_from_unix_seconds(static_cast<gint64>(secs));
                } else {
                    // 回退：把任意值打印为字符串保存，便于调试
                    gchar *valstr = g_variant_print(prop_value, TRUE);
                    if (valstr) {
                        g_current_music.started_time = valstr;
                        g_free(valstr);
                    }
                }
            }
            else if (g_strcmp0(prop_name, "Metadata") == 0)
            {
                // prop_value 的类型通常是 a{sv}（字典）
                if (g_variant_is_of_type(prop_value, G_VARIANT_TYPE("a{sv}")))
                {
                    GVariantIter miter;
                    gchar *mkey = nullptr;
                    GVariant *mval = nullptr;
                    g_variant_iter_init(&miter, prop_value);
                    while (g_variant_iter_next(&miter, "{sv}", &mkey, &mval))
                    {
                        if (g_strcmp0(mkey, "xesam:title") == 0)
                        {
                            if (g_variant_is_of_type(mval, G_VARIANT_TYPE_STRING))
                                g_current_music.title = g_variant_get_string(mval, nullptr);
                        }
                        else if (g_strcmp0(mkey, "xesam:artist") == 0)
                        {
                            // 通常为字符串数组 (as)，取第一个
                            if (g_variant_is_of_type(mval, G_VARIANT_TYPE("as")))
                            {
                                GVariantIter aiter;
                                gchar *artist_name = nullptr;
                                g_variant_iter_init(&aiter, mval);
                                if (g_variant_iter_next(&aiter, "s", &artist_name))
                                {
                                    g_current_music.artist = artist_name;
                                    g_free(artist_name);
                                }
                            }
                            else if (g_variant_is_of_type(mval, G_VARIANT_TYPE_STRING))
                            {
                                g_current_music.artist = g_variant_get_string(mval, nullptr);
                            }
                        }
                        else if (g_strcmp0(mkey, "xesam:asText") == 0)
                        {
                            if (g_variant_is_of_type(mval, G_VARIANT_TYPE_STRING))
                                g_current_music.instance_lyrics = g_variant_get_string(mval, nullptr);
                        }
                        else if (g_strcmp0(mkey, "mpris:length") == 0 || g_strcmp0(mkey, "length") == 0)
                        {
                            if (g_variant_is_of_type(mval, G_VARIANT_TYPE_INT64))
                            {
                                gint64 us = g_variant_get_int64(mval);
                                g_current_music.duration = static_cast<int>(us / 1000000);
                            }
                            else if (g_variant_is_of_type(mval, G_VARIANT_TYPE_INT32))
                            {
                                g_current_music.duration = g_variant_get_int32(mval);
                            }
                        }

                        g_free(mkey);
                        g_variant_unref(mval);
                    } // end metadata inner loop
                } // end if metadata type check
            }
            else if (g_strcmp0(prop_name, "Volume") == 0)
            {
                // 示例：可以处理音量变化（可选）
                // if (g_variant_is_of_type(prop_value, G_VARIANT_TYPE_DOUBLE))
                // {
                //     double vol = g_variant_get_double(prop_value);
                // }
            }
            // 其它属性可按需处理
        } // unlock mutex
        display_music(g_current_music); // 每次属性变化后打印当前音乐信息（可选）
        g_free(prop_name);
        g_variant_unref(prop_value);
    }

    // 释放从 g_variant_get 得到的引用
    if (changed_props)
        g_variant_unref(changed_props);
    if (invalidated)
        g_variant_unref(invalidated);
}
// // 全局缓存与同步控制，用于后台线程写入并主线程/其它消费者读取
// static music_t g_current_music = {};       // 全局当前音乐信息缓存
// static std::mutex g_music_mutex;           // 保护 g_current_music 的互斥锁
// static std::atomic<bool> g_stop_reader{false}; // 停止后台读取线程的标志

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
            // std::cout << "Background read count: " << cnt << std::endl;
            // display_music(g_current_music);
        }
        
        // 睡眠一段时间，下次再读取（间隔可调整）
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}


int main()
{
    std::cout << "MPRIS Listener started. Waiting musicfox start..." << std::endl;
    // 一直等待musicfox挂载，直到找到musicfox
    std::string bus_name = "";
    while (bus_name.empty())
    {
        bus_name = find_musicfox_bus_name();
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

    std::cout << "musicfox strated , Listening for ALL musicfox MPRIS signals..." << std::endl;

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