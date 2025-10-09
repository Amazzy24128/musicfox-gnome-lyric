
#include <iostream>
#include <gio/gio.h>
#include <string>
#include <vector>
std::string find_musicfox_bus_name() {
    GError* error = nullptr;
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection) {
        std::cerr << "Failed to get session bus: " << error->message << std::endl;
        g_error_free(error);
        return "";
    }

    GVariant* result = g_dbus_connection_call_sync(
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
        &error
    );

    std::string bus_name;
    if (result) {
        GVariantIter* iter;
        g_variant_get(result, "(as)", &iter);
        gchar* name;
        while (g_variant_iter_next(iter, "s", &name)) {
            std::string sname(name);
            if (sname.find("org.mpris.MediaPlayer2.musicfox") == 0) {
                bus_name = sname;
                g_free(name);
                break;
            }
            g_free(name);
        }
        g_variant_iter_free(iter);
        g_variant_unref(result);
    } else {
        std::cerr << "Failed to list D-Bus names: " << error->message << std::endl;
        g_error_free(error);
    }
    g_object_unref(connection);
    return bus_name;
}





int main() {

    std::string bus_name = find_musicfox_bus_name();
    if (bus_name.empty()) {
        std::cerr << "No musicfox MPRIS service found." << std::endl;
        return 1;
    }

    GError *error = nullptr;
    GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION,
        G_DBUS_PROXY_FLAGS_NONE,
        nullptr,
        bus_name.c_str(), // 自动查找的 bus name
        "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player",
        nullptr,
        &error
    );

    if (!proxy) {
        std::cerr << "Failed to connect to go-musicfox D-Bus: " << error->message << std::endl;
        g_error_free(error);
        return 1;
    }

    // 获取 Metadata 属性
    GVariant *metadata = g_dbus_proxy_get_cached_property(proxy, "Metadata");
    if (!metadata) {
        std::cerr << "Failed to get Metadata property." << std::endl;
        g_object_unref(proxy);
        return 1;
    }

    // 查找 xesam:asText 字段
    GVariantIter iter;
    gchar *key;
    GVariant *value;
    const char *lyric = nullptr;

    g_variant_iter_init(&iter, metadata);
    while (g_variant_iter_next(&iter, "{sv}", &key, &value)) {
        if (strcmp(key, "xesam:asText") == 0) {
            // 歌词字段
            lyric = g_variant_get_string(value, nullptr);
            std::cout << "当前歌词内容：\n" << (lyric ? lyric : "(无歌词)") << std::endl;
        }
        g_free(key);
        g_variant_unref(value);
    }

    g_variant_unref(metadata);
    g_object_unref(proxy);

    // 可选：暂停，防止闪退
    // std::cin.get();

    return 0;
}