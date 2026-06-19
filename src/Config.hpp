#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace LiquidGlass {

inline HANDLE g_pluginHandle = nullptr;

inline constexpr const char* PLUGIN_NAME = "liquidglass";
inline constexpr const char* PLUGIN_DESCRIPTION = "Liquid Glass inspired per-window shader decoration";
inline constexpr const char* PLUGIN_AUTHOR = "lain";
inline constexpr const char* PLUGIN_VERSION = "0.3.0";

inline constexpr const char* CFG_ENABLED = "plugin:liquidglass:enabled";
inline constexpr const char* CFG_EXCLUDE_CLASSES = "plugin:liquidglass:exclude_classes";
inline constexpr const char* CFG_LAYER_NAMESPACES = "plugin:liquidglass:layer_namespaces";
inline constexpr const char* CFG_WINDOW_OPACITY = "plugin:liquidglass:window_opacity";
inline constexpr const char* CFG_LAYER_OPACITY = "plugin:liquidglass:layer_opacity";
inline constexpr const char* CFG_LAYER_CORNER_RADIUS = "plugin:liquidglass:layer_corner_radius";
inline constexpr const char* CFG_BLUR_STRENGTH = "plugin:liquidglass:blur_strength";
inline constexpr const char* CFG_BLUR_ITERATIONS = "plugin:liquidglass:blur_iterations";
inline constexpr const char* CFG_REFRACTION_STRENGTH = "plugin:liquidglass:refraction_strength";
inline constexpr const char* CFG_CHROMATIC_ABERRATION = "plugin:liquidglass:chromatic_aberration";
inline constexpr const char* CFG_FRESNEL_STRENGTH = "plugin:liquidglass:fresnel_strength";
inline constexpr const char* CFG_SPECULAR_STRENGTH = "plugin:liquidglass:specular_strength";
inline constexpr const char* CFG_GLASS_OPACITY = "plugin:liquidglass:glass_opacity";
inline constexpr const char* CFG_EDGE_THICKNESS = "plugin:liquidglass:edge_thickness";
inline constexpr const char* CFG_TINT_COLOR = "plugin:liquidglass:tint_color";
inline constexpr const char* CFG_LENS_DISTORTION = "plugin:liquidglass:lens_distortion";
inline constexpr const char* CFG_BRIGHTNESS = "plugin:liquidglass:brightness";
inline constexpr const char* CFG_CONTRAST = "plugin:liquidglass:contrast";
inline constexpr const char* CFG_SATURATION = "plugin:liquidglass:saturation";
inline constexpr const char* CFG_VIBRANCY = "plugin:liquidglass:vibrancy";
inline constexpr const char* CFG_ADAPTIVE_DIM = "plugin:liquidglass:adaptive_dim";
inline constexpr const char* CFG_ADAPTIVE_BOOST = "plugin:liquidglass:adaptive_boost";

// Per-surface distortion multipliers (scale refraction + lens + chromatic only).
inline constexpr const char* CFG_WINDOW_DISTORTION_SCALE = "plugin:liquidglass:window_distortion_scale";
inline constexpr const char* CFG_LAYER_DISTORTION_SCALE = "plugin:liquidglass:layer_distortion_scale";
inline constexpr const char* CFG_LAYER_DISTORTION_OVERRIDES = "plugin:liquidglass:layer_distortion_overrides";

// Cursor-following soft refraction + highlight (a "lens" that tracks the mouse).
inline constexpr const char* CFG_CURSOR_ENABLED = "plugin:liquidglass:cursor_enabled";
inline constexpr const char* CFG_CURSOR_RADIUS = "plugin:liquidglass:cursor_radius";         // size, logical px
inline constexpr const char* CFG_CURSOR_INTENSITY = "plugin:liquidglass:cursor_intensity";   // highlight strength
inline constexpr const char* CFG_CURSOR_REFRACTION = "plugin:liquidglass:cursor_refraction"; // lens distortion strength
inline constexpr const char* CFG_CURSOR_COLOR = "plugin:liquidglass:cursor_color";           // 0xRRGGBBAA highlight tint

inline constexpr const char* DEFAULT_EXCLUDE_CLASSES = "";
inline constexpr const char* DEFAULT_LAYER_NAMESPACES = "quickshell";
inline constexpr float DEFAULT_WINDOW_OPACITY = 0.90f;
inline constexpr float DEFAULT_LAYER_OPACITY = 1.0f;
inline constexpr float DEFAULT_LAYER_CORNER_RADIUS = 12.0f;
inline constexpr float DEFAULT_BLUR_STRENGTH = 0.32f;
inline constexpr Hyprlang::INT DEFAULT_BLUR_ITERATIONS = 2;
inline constexpr float DEFAULT_REFRACTION_STRENGTH = 1.15f;
inline constexpr float DEFAULT_CHROMATIC_ABERRATION = 0.90f;
inline constexpr float DEFAULT_FRESNEL_STRENGTH = 0.46f;
inline constexpr float DEFAULT_SPECULAR_STRENGTH = 0.38f;
inline constexpr float DEFAULT_GLASS_OPACITY = 0.78f;
inline constexpr float DEFAULT_EDGE_THICKNESS = 0.040f;
inline constexpr Hyprlang::INT DEFAULT_TINT_COLOR = 0xb8d8ff00;
inline constexpr float DEFAULT_LENS_DISTORTION = 1.15f;
inline constexpr float DEFAULT_BRIGHTNESS = 0.88f;
inline constexpr float DEFAULT_CONTRAST = 1.16f;
inline constexpr float DEFAULT_SATURATION = 1.14f;
inline constexpr float DEFAULT_VIBRANCY = 0.32f;
inline constexpr float DEFAULT_ADAPTIVE_DIM = 0.32f;
inline constexpr float DEFAULT_ADAPTIVE_BOOST = 0.10f;
inline constexpr float DEFAULT_WINDOW_DISTORTION_SCALE = 1.0f;
inline constexpr float DEFAULT_LAYER_DISTORTION_SCALE = 1.0f;
inline constexpr const char* DEFAULT_LAYER_DISTORTION_OVERRIDES = "";

inline constexpr Hyprlang::INT DEFAULT_CURSOR_ENABLED = 1;
inline constexpr float DEFAULT_CURSOR_RADIUS = 220.0f;
inline constexpr float DEFAULT_CURSOR_INTENSITY = 0.50f;
inline constexpr float DEFAULT_CURSOR_REFRACTION = 0.60f;
inline constexpr Hyprlang::INT DEFAULT_CURSOR_COLOR = 0xffffff66; // soft white, ~40% alpha

inline std::unordered_map<std::string, SP<Config::Values::CIntValue>> g_intConfigValues;
inline std::unordered_map<std::string, SP<Config::Values::CFloatValue>> g_floatConfigValues;
inline std::unordered_map<std::string, SP<Config::Values::CStringValue>> g_stringConfigValues;

inline bool addIntConfig(const char* name, Hyprlang::INT fallback) {
    auto value = makeShared<Config::Values::CIntValue>(name, name, static_cast<Config::INTEGER>(fallback));
    if (!HyprlandAPI::addConfigValueV2(g_pluginHandle, value))
        return false;

    g_intConfigValues[name] = value;
    return true;
}

inline bool addFloatConfig(const char* name, float fallback) {
    auto value = makeShared<Config::Values::CFloatValue>(name, name, static_cast<Config::FLOAT>(fallback));
    if (!HyprlandAPI::addConfigValueV2(g_pluginHandle, value))
        return false;

    g_floatConfigValues[name] = value;
    return true;
}

inline bool addStringConfig(const char* name, std::string_view fallback) {
    auto value = makeShared<Config::Values::CStringValue>(name, name, Config::STRING(fallback));
    if (!HyprlandAPI::addConfigValueV2(g_pluginHandle, value))
        return false;

    g_stringConfigValues[name] = value;
    return true;
}

inline void clearConfigValues() {
    g_intConfigValues.clear();
    g_floatConfigValues.clear();
    g_stringConfigValues.clear();
}

inline Hyprlang::INT configInt(const char* name, Hyprlang::INT fallback) {
    const auto found = g_intConfigValues.find(name);
    if (found == g_intConfigValues.end() || !found->second)
        return fallback;

    return static_cast<Hyprlang::INT>(found->second->value());
}

inline float configFloat(const char* name, float fallback) {
    const auto found = g_floatConfigValues.find(name);
    if (found == g_floatConfigValues.end() || !found->second)
        return fallback;

    return static_cast<float>(found->second->value());
}

inline std::string configString(const char* name, std::string_view fallback) {
    const auto found = g_stringConfigValues.find(name);
    if (found == g_stringConfigValues.end() || !found->second)
        return std::string(fallback);

    return found->second->value();
}

inline bool enabled() {
    return configInt(CFG_ENABLED, 1) != 0;
}

inline bool cursorEnabled() {
    return configInt(CFG_CURSOR_ENABLED, DEFAULT_CURSOR_ENABLED) != 0;
}

inline float windowOpacity() {
    return std::clamp(configFloat(CFG_WINDOW_OPACITY, DEFAULT_WINDOW_OPACITY), 0.05f, 1.0f);
}

inline float layerOpacity() {
    return std::clamp(configFloat(CFG_LAYER_OPACITY, DEFAULT_LAYER_OPACITY), 0.05f, 1.0f);
}

inline float layerCornerRadius() {
    return std::clamp(configFloat(CFG_LAYER_CORNER_RADIUS, DEFAULT_LAYER_CORNER_RADIUS), 0.0f, 128.0f);
}

inline float windowDistortionScale() {
    return std::clamp(configFloat(CFG_WINDOW_DISTORTION_SCALE, DEFAULT_WINDOW_DISTORTION_SCALE), 0.0f, 4.0f);
}

// Distortion multiplier for a layer namespace. Looks up an explicit per-namespace
// override in CFG_LAYER_DISTORTION_OVERRIDES ("waybar:0.3,notifications:0.4"), else
// falls back to the global layer scale.
inline float layerDistortionScale(const std::string& ns) {
    const std::string overrides = configString(CFG_LAYER_DISTORTION_OVERRIDES, DEFAULT_LAYER_DISTORTION_OVERRIDES);

    const auto trim = [](std::string s) {
        const auto a = s.find_first_not_of(" \t");
        if (a == std::string::npos)
            return std::string();
        const auto b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    };
    const auto iequals = [](const std::string& x, const std::string& y) {
        if (x.size() != y.size())
            return false;
        for (size_t i = 0; i < x.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(x[i])) != std::tolower(static_cast<unsigned char>(y[i])))
                return false;
        return true;
    };

    size_t pos = 0;
    while (pos < overrides.size()) {
        const auto comma = overrides.find(',', pos);
        const std::string token = overrides.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        const auto colon = token.find(':');
        if (colon != std::string::npos && iequals(trim(token.substr(0, colon)), ns)) {
            try {
                return std::clamp(std::stof(trim(token.substr(colon + 1))), 0.0f, 4.0f);
            } catch (...) {
            }
        }
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }

    return std::clamp(configFloat(CFG_LAYER_DISTORTION_SCALE, DEFAULT_LAYER_DISTORTION_SCALE), 0.0f, 4.0f);
}

} // namespace LiquidGlass
