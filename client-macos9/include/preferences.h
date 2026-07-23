#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <string>

namespace mochila {

struct MochilaPreferences {
  std::string serverHost;
  int serverPort;
  std::string lastUrl;

  MochilaPreferences()
    : serverHost(""), serverPort(8080), lastUrl("https://en.wikipedia.org/wiki/Mac_OS_9") {}
};

class PreferencesManager {
public:
  // Load preferences from file (returns false if file doesn't exist)
  static bool load(MochilaPreferences& prefs);

  // Save preferences to file (returns false on error)
  static bool save(const MochilaPreferences& prefs);

  // Show dialog to get server configuration from user
  // Returns true if user clicked OK, false if cancelled
  static bool showConfigDialog(MochilaPreferences& prefs);

private:
  static std::string getPreferencesPath();
};

} // namespace mochila

#endif // PREFERENCES_H
