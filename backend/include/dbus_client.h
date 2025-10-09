#ifndef DBUS_CLIENT_H
#define DBUS_CLIENT_H

#include <string>
#include <functional>
#include <gio/gio.h>

struct LyricData {
    bool is_playing;
    std::string current_lyric;
    std::string next_lyric;
    std::string song_title;
    std::string artist;
    int64_t progress_ms;
    
    LyricData() : is_playing(false), progress_ms(0) {}
};

class DBusClient {
public:
    DBusClient();
    ~DBusClient();
    
    bool connect();
    void disconnect();
    
    LyricData getCurrentLyricData();
    void setPropertyChangedCallback(std::function<void(const LyricData&)> callback);
    
    bool isConnected() const { return proxy_ != nullptr; }

private:
    GDBusProxy* proxy_;
    gulong signal_handler_id_;
    std::function<void(const LyricData&)> property_changed_callback_;
    
    static void onPropertiesChanged(GDBusProxy* proxy, 
                                   GVariant* changed_properties,
                                   const gchar* const* invalidated_properties,
                                   gpointer user_data);
    
    LyricData extractLyricFromMetadata(GVariant* metadata);
    std::string extractStringFromVariant(GVariant* variant);
};

#endif // DBUS_CLIENT_H