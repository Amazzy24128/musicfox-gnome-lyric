#include <iostream> // 引入输入输出流，用于 std::cout / std::cerr
#include <gio/gio.h> // 引入 GIO（GLib 的 I/O 与 D-Bus 支持）
#include <string> // 引入 std::string 类型
#include <vector> // 引入 std::vector（本文件未直接使用，但保留以防扩展）

// 查找运行中的 musicfox（MPRIS）服务的 D-Bus 名称
std::string find_musicfox_bus_name() {
    GError* error = nullptr; // 用于存放可能的错误信息
    GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error); // 同步获取会话总线连接
    if (!connection) {
        std::cerr << "Failed to get session bus: " << error->message << std::endl; // 打印获取总线失败的错误信息
        g_error_free(error); // 释放错误对象
        return ""; // 返回空字符串表示未找到或出错
    }

    // 调用 org.freedesktop.DBus.ListNames 来列出所有注册的 D-Bus 名称
    GVariant* result = g_dbus_connection_call_sync(
        connection, // 使用的连接
        "org.freedesktop.DBus", // 目标服务名
        "/org/freedesktop/DBus", // 对象路径
        "org.freedesktop.DBus", // 接口名
        "ListNames", // 方法名
        nullptr, // 参数（无）
        G_VARIANT_TYPE("(as)"), // 期望的返回类型：一个字符串数组的元组
        G_DBUS_CALL_FLAGS_NONE, // 调用标志
        -1, // 超时（-1 表示默认）
        nullptr, // 取消对象（不使用）
        &error // 错误返回
    );

    std::string bus_name; // 存放找到的 bus 名称
    if (result) {
        GVariantIter* iter; // 用于迭代返回的字符串数组
        g_variant_get(result, "(as)", &iter); // 解析返回的 GVariant，得到字符串迭代器
        gchar* name; // 存放每次迭代得到的名称
        while (g_variant_iter_next(iter, "s", &name)) { // 遍历每个名称
            std::string sname(name); // 转为 std::string 方便处理
            if (sname.find("org.mpris.MediaPlayer2.musicfox") == 0) { // 检查名称是否以目标前缀开头，如果是，if内为真，否则为假
                bus_name = sname; // 找到匹配的名称则保存
                g_free(name); // 释放 g_malloc 分配的字符串
                break; // 找到第一个匹配后退出循环
            }
            g_free(name); // 释放当前名称的内存
        }
        g_variant_iter_free(iter); // 释放迭代器
        g_variant_unref(result); // 释放返回的 GVariant
    } else {
        std::cerr << "Failed to list D-Bus names: " << error->message << std::endl; // 列出名称失败时打印错误
        g_error_free(error); // 释放错误对象
    }
    g_object_unref(connection); // 释放连接对象
    return bus_name; // 返回找到的 bus 名称（可能为空）
}





int main() {

    std::string bus_name = find_musicfox_bus_name(); // 查找 musicfox 的 D-Bus 名称
    if (bus_name.empty()) {
        std::cerr << "No musicfox MPRIS service found." << std::endl; // 如果没找到则提示并退出
        return 1; // 非零退出表示失败
    }

    GError *error = nullptr; // 再次准备错误对象用于创建 proxy
    // 创建一个同步的 GDBusProxy 来访问 Player 接口
    GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION, // 使用会话总线
        G_DBUS_PROXY_FLAGS_NONE, // 代理标志（无）
        nullptr, // GDBusInterfaceInfo（可选，设置为 nullptr）
        bus_name.c_str(), // 目标服务名称（动态查找得到）
        "/org/mpris/MediaPlayer2", // 对象路径（MPRIS 规范的路径）
        "org.mpris.MediaPlayer2.Player", // 接口名（MPRIS Player 接口）
        nullptr, // GCancellable（不使用）
        &error // 错误返回
    );

    if (!proxy) {
        std::cerr << "Failed to connect to go-musicfox D-Bus: " << error->message << std::endl; // 连接失败时打印错误
        g_error_free(error); // 释放错误对象
        return 1; // 退出
    }

    // 获取代理缓存的 Metadata 属性（这是一个 GVariant，通常是 a{sv} 类型）
    GVariant *metadata = g_dbus_proxy_get_cached_property(proxy, "Metadata");
    if (!metadata) {
        std::cerr << "Failed to get Metadata property." << std::endl; // 获取失败时提示
        g_object_unref(proxy); // 释放代理对象
        return 1; // 退出
    }

    // 遍历 metadata（a{sv}）来查找键为 "xesam:asText" 的字段（可能包含歌词）
    GVariantIter iter; // 字典迭代器
    gchar *key; // 存放字典键（字符串）
    GVariant *value; // 存放对应的值（GVariant）
    const char *lyric = nullptr; // 指向歌词字符串（如果找到的话）

    g_variant_iter_init(&iter, metadata); // 初始化迭代器以遍历 metadata
    while (g_variant_iter_next(&iter, "{sv}", &key, &value)) { // 迭代每个键值对
        if (strcmp(key, "xesam:asText") == 0) { // 检查当前键是否为 xesam:asText
            // 歌词字段，尝试从 value 中取字符串
            lyric = g_variant_get_string(value, nullptr); // 直接获取字符串指针（注意：这是只读指针）
            std::cout << "当前歌词内容：\n" << (lyric ? lyric : "(无歌词)") << std::endl; // 输出歌词或提示无歌词
        }
        g_free(key); // 释放键字符串内存
        g_variant_unref(value); // 释放 value 的 GVariant
    }

    g_variant_unref(metadata); // 释放 metadata GVariant
    g_object_unref(proxy); // 释放代理对象

    // 可选：阻止程序立即退出以便调试或查看输出
    // std::cin.get();

    return 0; // 正常退出
}