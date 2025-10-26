#include <iostream>
#include <gio/gio.h>
#include <string>
#include <unistd.h>

std::string find_musicfox_bus_name();
extern "C" void on_name_owner_changed(GDBusConnection *connection, const gchar *sender, const gchar *path, const gchar *iface_name, const gchar *signal, GVariant *params, gpointer data);


