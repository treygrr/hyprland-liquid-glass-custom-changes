#pragma once

namespace LiquidGlass::Shaders {

inline constexpr const char* LIQUID_GLASS_FRAG = R"GLSL(
#version 300 es
precision highp float;

in vec2 v_texcoord;

uniform sampler2D tex;
uniform sampler2D maskTex;
uniform int useMask;
uniform vec2 fullSize;
uniform float radius;
uniform float roundingPower;

uniform float brightness;
uniform float contrast;
uniform float vibrancy;

uniform float refractionStrength;
uniform float chromaticAberration;
uniform float fresnelStrength;
uniform float specularStrength;
uniform float glassOpacity;
uniform float edgeThickness;
uniform vec2 uvPadding;
uniform vec3 tintColor;
uniform float tintAlpha;
uniform float lensDistortion;
uniform float saturation;
uniform float adaptiveDim;
uniform float adaptiveBoost;

// Cursor-following soft refraction + highlight. cursorPos is in the same
// surface-local pixel space as `pixel`; cursorRadius <= 0 disables it.
uniform vec2 cursorPos;
uniform float cursorRadius;
uniform float cursorIntensity;
uniform float cursorRefraction;
uniform vec3 cursorColor;
uniform float cursorColorAlpha;
uniform int cursorBlendMode; // 0 normal, 1 darken, 2 multiply, 3 lighten, 4 screen, 5 overlay, 6 add
uniform vec2 cursorStretchDir; // normalized travel direction in this surface's pixel space (0 if still)
uniform float cursorStretch;   // elongation of the dome along travel (0 = round)
uniform float cursorTrail;     // extra elongation behind the head -> comet tail (0 = symmetric)

layout(location = 0) out vec4 fragColor;

// Photoshop-style blend of the glass pixel (base) with cursorColor (blend).
// Codes must match cursorBlendModeCode() in Config.hpp.
vec3 blendCursor(vec3 base, vec3 blend, int mode) {
    if (mode == 1)
        return min(base, blend);                            // darken
    if (mode == 2)
        return base * blend;                                // multiply
    if (mode == 3)
        return max(base, blend);                            // lighten
    if (mode == 4)
        return 1.0 - (1.0 - base) * (1.0 - blend);          // screen
    if (mode == 5) {                                        // overlay
        vec3 lo = 2.0 * base * blend;
        vec3 hi = 1.0 - 2.0 * (1.0 - base) * (1.0 - blend);
        return mix(lo, hi, step(0.5, base));
    }
    if (mode == 6)
        return min(base + blend, vec3(1.0));                // add (linear dodge)
    return blend;                                           // 0 normal: paint toward color
}

float roundedBoxSdf(vec2 point, vec2 halfSize, float cornerRadius) {
    vec2 q = abs(point) - halfSize + vec2(cornerRadius);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - cornerRadius;
}

vec2 glassUvFromPixel(vec2 pixel, vec2 size) {
    vec2 normalized = clamp(pixel / max(size, vec2(1.0)), vec2(0.0), vec2(1.0));
    return mix(uvPadding, vec2(1.0) - uvPadding, normalized);
}

vec3 sampleGlassPixel(vec2 pixel, vec2 size) {
    return texture(tex, glassUvFromPixel(pixel, size)).rgb;
}

void main() {
    vec2 size = max(fullSize, vec2(1.0));
    float minDim = max(min(size.x, size.y), 1.0);
    float cornerRadius = clamp(radius, 0.0, minDim * 0.5);

    vec2 pixel = v_texcoord * size;
    vec2 center = size * 0.5;
    vec2 glassCoord = pixel - center;
    float sdf = roundedBoxSdf(glassCoord, size * 0.5, cornerRadius);
    float antialias = max(fwidth(sdf), 0.75);
    float shapeAlpha = 1.0 - smoothstep(-antialias, antialias, sdf);

    float layerMask = 1.0;
    if (useMask == 1)
        layerMask = smoothstep(0.18, 0.48, texture(maskTex, v_texcoord).a);

    if (shapeAlpha * layerMask <= 0.001)
        discard;

    float inverseSdf = max(-sdf / minDim, 0.0);
    float coordLength = length(glassCoord);
    vec2 radial = coordLength > 0.0001 ? glassCoord / coordLength : vec2(0.0);

    float distortionDepth = clamp(edgeThickness * 5.0, 0.02, 0.45);
    float distortionStrength = clamp(lensDistortion * refractionStrength * 0.1135, 0.0, 0.45);
    float distanceFromCenter = 1.0 - clamp(inverseSdf / distortionDepth, 0.0, 1.0);
    float distortion = 1.0 - sqrt(max(1.0 - distanceFromCenter * distanceFromCenter, 0.0));
    vec2 offset = distortion * radial * size * 0.5 * distortionStrength;

    // Cursor lens: a soft bump centred on the mouse that adds extra refraction
    // (sampled toward the cursor -> gentle magnify) and feeds a highlight below.
    float cursorBump = 0.0;
    vec2 cursorOffset = vec2(0.0);
    if (cursorRadius > 0.5) {
        vec2 toCursor = pixel - cursorPos;

        // Warp into the travel frame so the dome elongates along motion and trails behind.
        // Dividing the along-travel coordinate makes the dome physically reach further in
        // that axis; the rear half (behind the head) gets the extra `cursorTrail` -> comet.
        vec2 warped = toCursor;
        if (dot(cursorStretchDir, cursorStretchDir) > 0.5) {
            vec2 dir = cursorStretchDir;
            vec2 perpv = vec2(-dir.y, dir.x);
            float along = dot(toCursor, dir);
            float perp = dot(toCursor, perpv);
            float stretchHere = along < 0.0 ? (cursorStretch + cursorTrail) : cursorStretch;
            along /= (1.0 + stretchHere);
            warped = dir * along + perpv * perp;
        }

        float cDist = length(warped);
        float cn = clamp(cDist / cursorRadius, 0.0, 1.0);
        cursorBump = 1.0 - cn;
        cursorBump = cursorBump * cursorBump * (3.0 - 2.0 * cursorBump); // smoothstep dome
        float rawDist = length(toCursor);
        vec2 cDir = rawDist > 0.0001 ? toCursor / rawDist : vec2(0.0);
        float lensProfile = 4.0 * cn * (1.0 - cn);                       // 0 at centre & edge, peak mid
        cursorOffset = cDir * lensProfile * cursorRefraction * cursorRadius * 0.25;
    }

    vec2 samplePixel = pixel - offset - cursorOffset;

    float edge = smoothstep(0.0, 0.02, inverseSdf);
    float chromaticShift = clamp(chromaticAberration * 3.3333333, 0.0, 12.0);
    vec2 shift = radial * edge * chromaticShift;
    vec3 color = vec3(
        sampleGlassPixel(samplePixel - shift, size).r,
        sampleGlassPixel(samplePixel, size).g,
        sampleGlassPixel(samplePixel + shift, size).b
    );

    float glassTint = clamp(brightness * 1.0227273, 0.0, 2.0);
    color *= vec3(glassTint);
    color = mix(color, color * tintColor, clamp(tintAlpha, 0.0, 1.0));

    // Cursor-following effect: blend the glass toward cursorColor using the selected
    // blend mode, weighted by the dome falloff. Strength = dome * intensity * alpha.
    if (cursorRadius > 0.5) {
        vec3 blended = blendCursor(color, cursorColor, cursorBlendMode);
        color = mix(color, blended, clamp(cursorBump * cursorIntensity * cursorColorAlpha, 0.0, 1.0));
    }

    float materialAlpha = clamp(glassOpacity / 0.78, 0.0, 1.0);
    fragColor = vec4(clamp(color, 0.0, 1.0), materialAlpha * shapeAlpha * layerMask);
}
)GLSL";

inline constexpr const char* GAUSSIAN_BLUR_FRAG = R"GLSL(
#version 300 es
precision highp float;

in vec2 v_texcoord;

uniform sampler2D tex;
uniform vec2 direction;
uniform float blurRadius;

layout(location = 0) out vec4 fragColor;

void main() {
    float radius = max(blurRadius, 0.001);
    float sigma = max(radius / 3.0, 0.001);
    int samples = min(int(ceil(radius)), 8);

    vec4 color = texture(tex, v_texcoord);
    float total = 1.0;

    for (int i = 1; i <= 8; ++i) {
        if (i > samples)
            break;

        float x = float(i);
        float weight = exp(-(x * x) / (2.0 * sigma * sigma));
        vec2 delta = direction * x;

        color += texture(tex, v_texcoord + delta) * weight;
        color += texture(tex, v_texcoord - delta) * weight;
        total += 2.0 * weight;
    }

    fragColor = color / total;
}
)GLSL";

} // namespace LiquidGlass::Shaders
