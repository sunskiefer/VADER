#include "paths.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif
#include "gen/vader_res.h"

namespace fs = std::filesystem;

static std::string dataRoot() {
#ifdef _WIN32
    const char *a = getenv("APPDATA");
    if (a && *a) return a;
    const char *u = getenv("USERPROFILE");
    return u && *u ? std::string(u) + "\\AppData\\Roaming" : std::string();
#else
    const char *x = getenv("XDG_DATA_HOME");
    if (x && *x) return x;
    const char *h = getenv("HOME");
    if (!h || !*h) return std::string();
#ifdef __APPLE__
    return std::string(h) + "/Library/Application Support";
#else
    return std::string(h) + "/.local/share";
#endif
#endif
}

std::string vaderShapesDir() {
    std::string root = dataRoot();
    if (root.empty()) return std::string();
    std::error_code ec;
    fs::path shapes = fs::u8path(root) / "VADER" / "Shapes";
    fs::create_directories(shapes, ec);
    if (ec) return std::string();
    fs::path mark = shapes.parent_path() / ".examples_installed";
    if (!fs::exists(mark, ec)) {   /* the examples, once: a file the user deleted later stays deleted */
        for (int i = 0; i < vg_shape_count(); i++) {
            fs::path f = shapes / vg_shape_name(i);
            if (fs::exists(f, ec)) continue;
            int n = 0;
            const unsigned char *d = vg_shape_data(i, &n);
            std::ofstream o(f, std::ios::binary);
            if (o) o.write((const char *)d, n);
        }
        std::ofstream m(mark);
        if (m) m << "1\n";
    }
    std::string s = shapes.u8string();
#ifdef _WIN32
    char sh[1024];
    DWORD n = GetShortPathNameA(s.c_str(), sh, sizeof sh);
    if (n > 0 && n < sizeof sh) s = sh;
#endif
    for (char &c : s)
        if (c == '\\') c = '/';
    return s;
}
