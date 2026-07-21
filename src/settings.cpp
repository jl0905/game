#include "settings.h"
#include "raylib.h"
#include <fstream>
#include <sstream>
#include <string>

namespace {

Settings g_settings;

// Same lookup order as the other cfg loaders: beside the exe, then the
// working directory and its parent.
std::string SettingsPath() {
    const std::string candidates[] = {
        IsWindowReady() ? std::string(GetApplicationDirectory()) + "assets/settings.cfg"
                        : "assets/settings.cfg",
        "assets/settings.cfg", "../assets/settings.cfg" };
    for (const std::string& p : candidates)
        if (FileExists(p.c_str())) return p;
    return "";
}

bool TruthValue(const std::string& v) { return v == "1" || v == "on" || v == "true"; }

}  // namespace

Settings& GetSettings() { return g_settings; }

void LoadSettings() {
    const std::string path = SettingsPath();
    if (path.empty()) return;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos)
            line.erase(hash);
        std::istringstream ss(line);
        std::string key, value;
        if (!(ss >> key >> value)) continue;
        Settings& s = g_settings;
        if      (key == "width")      s.windowWidth  = std::stoi(value);
        else if (key == "height")     s.windowHeight = std::stoi(value);
        else if (key == "fullscreen") s.fullscreen   = TruthValue(value);
        else if (key == "loddist")    s.lodDistance  = std::stof(value);
        else if (key == "particles")  s.particles    = TruthValue(value);
        else if (key == "volume")     s.masterVolume = std::stof(value);
        else if (key == "inverty")    s.invertY      = TruthValue(value);
    }
}
