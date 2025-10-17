#include <iostream>
#include <gio/gio.h>
#include <string>
#include <unistd.h>

// 查找musicfox的D-Bus总线名称（简化版）
std::string find_musicfox_bus_name() {
    GError *error = nullptr;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
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
    g_object_unref(connection);
    return bus_name;
}

// 回调函数：处理NameOwnerChanged信号以检测musicfox关闭
extern "C" void on_name_owner_changed(GDBusConnection *connection, const gchar *sender, const gchar *path, const gchar *iface_name, const gchar *signal, GVariant *params, gpointer data) {
    if (g_strcmp0(signal, "NameOwnerChanged") != 0 || !params) return;

    const gchar *name, *old_owner, *new_owner;
    g_variant_get(params, "(&s&s&s)", &name, &old_owner, &new_owner);

    // 修改：检查name是否以"org.mpris.MediaPlayer2.musicfox"开头，而不是精确匹配
    if (std::string(name).find("org.mpris.MediaPlayer2.musicfox") == 0 && g_strcmp0(new_owner, "") == 0) {
        std::cout << "Detected musicfox exit via D-Bus NameOwnerChanged signal for name: " << name << std::endl;
        GMainLoop *loop = static_cast<GMainLoop*>(data);
        g_main_loop_quit(loop);  // 退出主循环
    }
}

int main() {
    std::cout << "Starting D-Bus Exit Listener for musicfox..." << std::endl;

    std::string mpris_bus_name = "";
    while (mpris_bus_name.empty()) {
        mpris_bus_name = find_musicfox_bus_name();
        if (mpris_bus_name.empty()) {
            std::cout << "Waiting for musicfox to appear on D-Bus..." << std::endl;
            struct timespec ts = {0, 500000000L}; nanosleep(&ts, nullptr);
        }
    }
    std::cout << "Found musicfox at: " << mpris_bus_name << std::endl;

    GError *error = nullptr;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection) {
        std::cerr << "Failed to get session bus." << std::endl;
        return 1;
    }

    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);

    // 订阅NameOwnerChanged信号以监听musicfox退出
    guint sub_id = g_dbus_connection_signal_subscribe(
        connection,
        "org.freedesktop.DBus",  // 发送者
        "org.freedesktop.DBus",  // 接口
        "NameOwnerChanged",     // 信号
        "/org/freedesktop/DBus", // 路径
        nullptr,                 // 无过滤（订阅所有NameOwnerChanged）
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_name_owner_changed,
        loop,
        nullptr
    );

    std::cout << "Listening for musicfox exit. Close musicfox to test..." << std::endl;
    g_main_loop_run(loop);  // 运行循环，直到检测到退出

    // 清理
    g_dbus_connection_signal_unsubscribe(connection, sub_id);
    g_main_loop_unref(loop);
    g_object_unref(connection);

    std::cout << "Listener stopped." << std::endl;
    return 0;
}