#include "dbus_client.h"
#include <iostream>
#include <cstring>

const char* MUSICFOX_BUS_NAME = "org.mpris.MediaPlayer2.musicfox";
const char* MPRIS_OBJECT_PATH = "/org/mpris/MediaPlayer2";
const char* MPRIS_PLAYER_INTERFACE = "org.mpris.MediaPlayer2.Player";

DBusClient::DBusClient() : proxy_(nullptr), signal_handler_id_(0) {}

DBusClient::~DBusClient() {
    disconnect();
}

bool DBusClient::connect() {
    GError* error = nullptr;
    
    proxy_ = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION,
        G_DBUS_PROXY_FLAGS_NONE,
        nullptr,
        MUSICFOX_BUS_NAME,
        MPRIS_OBJECT_PATH,
        MPRIS_PLAYER_INTERFACE,
        nullptr,
        &error
    );
    
    if (error) {
        std::cerr << "Failed to connect to MusicFox D-Bus: " << error->message << std::endl;
        g_error_free(error);
        return false;
    }
    
    // 连接属性变化信号
    signal_handler_id_ = g_signal_connect(
        proxy_,
        "g-properties-changed",
        G_CALLBACK(onPropertiesChanged),
        this
    );
    
    std::cout << "Connected to MusicFox D-Bus interface" << std::endl;
    return true;
}

void DBusClient::disconnect() {
    if (proxy_) {
        if (signal_handler_id_ > 0) {
            g_signal_handler_disconnect(proxy_, signal_handler_id_);
            signal_handler_id_ = 0;
        }
        g_object_unref(proxy_);
        proxy_ = nullptr;
    }
}

LyricData DBusClient::getCurrentLyricData() {
    LyricData data;
    
    if (!proxy_) {
        return data;
    }
    
    GError* error = nullptr;
    GVariant* metadata_variant = g_dbus_proxy_get_cached_property(proxy_, "Metadata");
    
    if (metadata_variant) {
        data = extractLyricFromMetadata(metadata_variant);
        g_variant_unref(metadata_variant);
    }
    
    // 获取播放状态
    GVariant* status_variant = g_dbus_proxy_get_cached_property(proxy_, "PlaybackStatus");
    if (status_variant) {
        const char* status = g_variant_get_string(status_variant, nullptr);
        data.is_playing = (strcmp(status, "Playing") == 0);
        g_variant_unref(status_variant);
    }
    
    return data;
}

void DBusClient::setPropertyChangedCallback(std::function<void(const LyricData&)> callback) {
    property_changed_callback_ = callback;
}

void DBusClient::onPropertiesChanged(GDBusProxy* proxy,
                                   GVariant* changed_properties,
                                   const gchar* const* invalidated_properties,
                                   gpointer user_data) {
    auto* client = static_cast<DBusClient*>(user_data);
    
    if (client->property_changed_callback_) {
        LyricData data = client->getCurrentLyricData();
        client->property_changed_callback_(data);
    }
}

LyricData DBusClient::extractLyricFromMetadata(GVariant* metadata) {
    LyricData data;
    
    if (!metadata || !g_variant_is_of_type(metadata, G_VARIANT_TYPE_VARDICT)) {
        return data;
    }
    
    GVariantIter iter;
    gchar* key;
    GVariant* value;
    
    g_variant_iter_init(&iter, metadata);
    while (g_variant_iter_next(&iter, "{sv}", &key, &value)) {
        if (strcmp(key, "xesam:title") == 0) {
            data.song_title = extractStringFromVariant(value);
        } else if (strcmp(key, "xesam:artist") == 0) {
            // artist 可能是数组
            if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING_ARRAY)) {
                gsize length;
                const gchar** artists = g_variant_get_strv(value, &length);
                if (length > 0) {
                    data.artist = artists[0];
                }
                g_free(artists);
            }
        } else if (strcmp(key, "xesam:asText") == 0) {
            data.current_lyric = extractStringFromVariant(value);
        }
        
        g_free(key);
        g_variant_unref(value);
    }
    
    return data;
}

std::string DBusClient::extractStringFromVariant(GVariant* variant) {
    if (g_variant_is_of_type(variant, G_VARIANT_TYPE_STRING)) {
        const char* str = g_variant_get_string(variant, nullptr);
        return str ? std::string(str) : std::string();
    }
    return std::string();
}