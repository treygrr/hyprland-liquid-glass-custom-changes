#include "GlassRenderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

#include <GLES3/gl32.h>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprutils/math/Misc.hpp>

#include "Config.hpp"
#include "Globals.hpp"

using namespace Render::GL;

namespace LiquidGlass::GlassRenderer {

// --- Liquid cursor motion state (global layout coords, logical px) -----------
// The dome eases toward the real pointer (lag), and we track a smoothed velocity
// to drive the travel-direction stretch + trail. Updated once per frame (dt-gated
// so the many per-surface calls in a frame don't over-integrate).
namespace {
struct SCursorMotion {
    bool                                  initialized = false;
    Vector2D                              pos;  // smoothed (lagged) position
    Vector2D                              vel;  // smoothed velocity, px/s
    std::chrono::steady_clock::time_point lastTime;
};
SCursorMotion g_cursorMotion;

// Eases the smoothed cursor toward `target`. When the motion hasn't settled yet it
// re-damages around the cursor to request another frame, so the dome keeps animating
// for the brief settle after the pointer stops (motion events alone wouldn't repaint).
void updateCursorMotion(const Vector2D& target) {
    const auto now = std::chrono::steady_clock::now();
    if (!g_cursorMotion.initialized) {
        g_cursorMotion.pos         = target;
        g_cursorMotion.vel         = Vector2D(0.0, 0.0);
        g_cursorMotion.lastTime    = now;
        g_cursorMotion.initialized = true;
        return;
    }

    const double dt = std::chrono::duration<double>(now - g_cursorMotion.lastTime).count();
    if (dt < 0.0008) // duplicate same-frame call: don't integrate twice
        return;
    g_cursorMotion.lastTime = now;

    const double delay = std::clamp(configFloat(CFG_CURSOR_FOLLOW_DELAY, DEFAULT_CURSOR_FOLLOW_DELAY), 0.0F, 1.0F);
    const double posA  = delay > 1.0e-4 ? 1.0 - std::exp(-dt / delay) : 1.0; // 0 delay => snap
    const Vector2D newPos = g_cursorMotion.pos + (target - g_cursorMotion.pos) * posA;

    const Vector2D instVel = (newPos - g_cursorMotion.pos) / dt; // px/s
    const double   velA    = 1.0 - std::exp(-dt / 0.06);         // smooth velocity (~60ms) to avoid jitter
    g_cursorMotion.vel     = g_cursorMotion.vel + (instVel - g_cursorMotion.vel) * velA;
    g_cursorMotion.pos     = newPos;

    const double dist  = std::hypot(target.x - g_cursorMotion.pos.x, target.y - g_cursorMotion.pos.y);
    const double speed = std::hypot(g_cursorMotion.vel.x, g_cursorMotion.vel.y);
    if ((dist > 0.5 || speed > 4.0) && g_pHyprRenderer) {
        const double r = static_cast<double>(std::clamp(configFloat(CFG_CURSOR_RADIUS, DEFAULT_CURSOR_RADIUS), 0.0F, 4000.0F)) + SAMPLE_PADDING_PX;
        const CBox   box = {g_cursorMotion.pos.x - r, g_cursorMotion.pos.y - r, r * 2.0, r * 2.0};
        g_pHyprRenderer->damageBox(box);
    }
}
} // namespace

static bool ensureFramebuffer(SP<Render::IFramebuffer>& framebuffer, int width, int height, DRMFormat format, const std::string& name) {
    if (!g_pHyprRenderer)
        return false;

    if (!framebuffer)
        framebuffer = g_pHyprRenderer->createFB(name);

    if (!framebuffer)
        return false;

    if (framebuffer->m_size.x != width || framebuffer->m_size.y != height || framebuffer->m_drmFormat != format)
        return framebuffer->alloc(width, height, format);

    return framebuffer->isAllocated();
}

static void uploadToneUniforms() {
    const auto& uniforms = g_state->shaderManager.glassUniforms;
    const auto& shader = g_state->shaderManager.glassShader;

    shader->setUniformFloat(SHADER_BRIGHTNESS, std::clamp(configFloat(CFG_BRIGHTNESS, DEFAULT_BRIGHTNESS), 0.0F, 2.0F));
    shader->setUniformFloat(SHADER_CONTRAST, std::clamp(configFloat(CFG_CONTRAST, DEFAULT_CONTRAST), 0.0F, 2.5F));
    shader->setUniformFloat(SHADER_VIBRANCY, std::clamp(configFloat(CFG_VIBRANCY, DEFAULT_VIBRANCY), 0.0F, 1.0F));
    glUniform1f(uniforms.saturation, std::clamp(configFloat(CFG_SATURATION, DEFAULT_SATURATION), 0.0F, 2.0F));
    glUniform1f(uniforms.adaptiveDim, std::clamp(configFloat(CFG_ADAPTIVE_DIM, DEFAULT_ADAPTIVE_DIM), 0.0F, 1.0F));
    glUniform1f(uniforms.adaptiveBoost, std::clamp(configFloat(CFG_ADAPTIVE_BOOST, DEFAULT_ADAPTIVE_BOOST), 0.0F, 1.0F));
}

bool sampleBackground(SP<Render::IFramebuffer>& sampleFramebuffer, SP<Render::IFramebuffer> sourceFramebuffer, CBox box, Vector2D& outPaddingRatio,
                      int downscale) {
    if (!sourceFramebuffer)
        return false;

    const int pad = SAMPLE_PADDING_PX;
    const int fullWidth = static_cast<int>(box.width) + 2 * pad;
    const int fullHeight = static_cast<int>(box.height) + 2 * pad;
    const int sampleWidth = std::max(1, fullWidth / downscale);
    const int sampleHeight = std::max(1, fullHeight / downscale);

    if (!ensureFramebuffer(sampleFramebuffer, sampleWidth, sampleHeight, sourceFramebuffer->m_drmFormat, "liquidglass-sample"))
        return false;

    auto* sampleGL = GLFB(sampleFramebuffer);
    auto* sourceGL = GLFB(sourceFramebuffer);
    if (!sampleGL || !sourceGL)
        return false;

    int srcX0 = static_cast<int>(box.x) - pad;
    int srcX1 = static_cast<int>(box.x + box.width) + pad;
    int srcY0 = static_cast<int>(box.y) - pad;
    int srcY1 = static_cast<int>(box.y + box.height) + pad;

    const int framebufferWidth = static_cast<int>(sourceFramebuffer->m_size.x);
    const int framebufferHeight = static_cast<int>(sourceFramebuffer->m_size.y);

    int dstX0 = 0;
    int dstY0 = 0;
    int dstX1 = sampleWidth;
    int dstY1 = sampleHeight;

    const float xScale = static_cast<float>(sampleWidth) / static_cast<float>(fullWidth);
    const float yScale = static_cast<float>(sampleHeight) / static_cast<float>(fullHeight);

    if (srcX0 < 0) {
        dstX0 += static_cast<int>(static_cast<float>(-srcX0) * xScale);
        srcX0 = 0;
    }
    if (srcY0 < 0) {
        dstY0 += static_cast<int>(static_cast<float>(-srcY0) * yScale);
        srcY0 = 0;
    }
    if (srcX1 > framebufferWidth) {
        dstX1 -= static_cast<int>(static_cast<float>(srcX1 - framebufferWidth) * xScale);
        srcX1 = framebufferWidth;
    }
    if (srcY1 > framebufferHeight) {
        dstY1 -= static_cast<int>(static_cast<float>(srcY1 - framebufferHeight) * yScale);
        srcY1 = framebufferHeight;
    }

    outPaddingRatio = Vector2D(static_cast<double>(pad) / static_cast<double>(fullWidth), static_cast<double>(pad) / static_cast<double>(fullHeight));

    g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);

    glBindFramebuffer(GL_FRAMEBUFFER, sampleGL->getFBID());
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceGL->getFBID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sampleGL->getFBID());
    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    return true;
}

void blurBackground(SP<Render::IFramebuffer> sampleFramebuffer, float radius, int iterations, GLuint callerFramebufferID, int viewportWidth,
                    int viewportHeight) {
    auto& shaderManager = g_state->shaderManager;
    if (!sampleFramebuffer || radius <= 0.0F || iterations <= 0 || !shaderManager.initialized())
        return;

    const int width = static_cast<int>(sampleFramebuffer->m_size.x);
    const int height = static_cast<int>(sampleFramebuffer->m_size.y);

    auto& blurTempFramebuffer = g_state->blurTempFramebuffer;
    if (!ensureFramebuffer(blurTempFramebuffer, width, height, sampleFramebuffer->m_drmFormat, "liquidglass-blur-temp"))
        return;

    auto* sampleGL = GLFB(sampleFramebuffer);
    auto* blurTempGL = GLFB(blurTempFramebuffer);
    if (!sampleGL || !blurTempGL)
        return;

    static constexpr std::array<GLfloat, 9> FULLSCREEN_PROJECTION = {
        2.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0F, -1.0F, -1.0F, 1.0F,
    };

    const auto& uniforms = shaderManager.blurUniforms;
    auto shader = g_pHyprOpenGL->useShader(shaderManager.blurShader);
    shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, FULLSCREEN_PROJECTION);
    shader->setUniformInt(SHADER_TEX, 0);
    glUniform1f(uniforms.radius, radius);
    glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
    g_pHyprOpenGL->setViewport(0, 0, width, height);
    glActiveTexture(GL_TEXTURE0);

    for (int iteration = 0; iteration < iterations; ++iteration) {
        glBindFramebuffer(GL_FRAMEBUFFER, blurTempGL->getFBID());
        sampleFramebuffer->getTexture()->bind();
        glUniform2f(uniforms.direction, 1.0F / static_cast<float>(width), 0.0F);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glBindFramebuffer(GL_FRAMEBUFFER, sampleGL->getFBID());
        blurTempFramebuffer->getTexture()->bind();
        glUniform2f(uniforms.direction, 0.0F, 1.0F / static_cast<float>(height));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, callerFramebufferID);
    glBindVertexArray(0);
    g_pHyprOpenGL->setViewport(0, 0, viewportWidth, viewportHeight);
}

void applyGlassEffect(SP<Render::IFramebuffer> sampleFramebuffer, SP<Render::IFramebuffer> targetFramebuffer, CBox& rawBox, CBox& transformedBox, float alpha,
                      float cornerRadius, float roundingPower, const Vector2D& paddingRatio, float distortionScale, SP<Render::ITexture> maskTexture) {
    if (!sampleFramebuffer || !targetFramebuffer)
        return;

    auto* targetGL = GLFB(targetFramebuffer);
    if (!targetGL)
        return;

    auto& shaderManager = g_state->shaderManager;
    const auto& uniforms = shaderManager.glassUniforms;

    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    if (!monitor)
        return;

    const auto transform = Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform));
    Mat3x3 glMatrix = g_pHyprRenderer->projectBoxToTarget(rawBox, transform);
    glMatrix.transpose();

    glBindFramebuffer(GL_FRAMEBUFFER, targetGL->getFBID());
    glActiveTexture(GL_TEXTURE0);
    sampleFramebuffer->getTexture()->bind();

    auto shader = g_pHyprOpenGL->useShader(shaderManager.glassShader);
    shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, glMatrix.getMatrix());
    shader->setUniformInt(SHADER_TEX, 0);
    glUniform1i(uniforms.maskTexture, 1);
    glUniform1i(uniforms.useMask, maskTexture ? 1 : 0);
    shader->setUniformFloat2(SHADER_FULL_SIZE, static_cast<float>(transformedBox.width), static_cast<float>(transformedBox.height));
    shader->setUniformFloat(SHADER_RADIUS, cornerRadius);
    shader->setUniformFloat(SHADER_ROUNDING_POWER, roundingPower);

    glUniform1f(uniforms.refractionStrength, std::clamp(configFloat(CFG_REFRACTION_STRENGTH, DEFAULT_REFRACTION_STRENGTH) * distortionScale, 0.0f, 1.5f));
    glUniform1f(uniforms.chromaticAberration, std::clamp(configFloat(CFG_CHROMATIC_ABERRATION, DEFAULT_CHROMATIC_ABERRATION) * distortionScale, 0.0f, 1.5f));
    glUniform1f(uniforms.fresnelStrength, std::clamp(configFloat(CFG_FRESNEL_STRENGTH, DEFAULT_FRESNEL_STRENGTH), 0.0f, 2.0f));
    glUniform1f(uniforms.specularStrength, std::clamp(configFloat(CFG_SPECULAR_STRENGTH, DEFAULT_SPECULAR_STRENGTH), 0.0f, 2.0f));
    glUniform1f(uniforms.glassOpacity, std::clamp(configFloat(CFG_GLASS_OPACITY, DEFAULT_GLASS_OPACITY), 0.0f, 1.0f) * alpha);
    glUniform1f(uniforms.edgeThickness, std::clamp(configFloat(CFG_EDGE_THICKNESS, DEFAULT_EDGE_THICKNESS), 0.005f, 0.20f));
    glUniform1f(uniforms.lensDistortion, std::clamp(configFloat(CFG_LENS_DISTORTION, DEFAULT_LENS_DISTORTION) * distortionScale, 0.0f, 1.5f));
    glUniform2f(uniforms.uvPadding, static_cast<float>(paddingRatio.x), static_cast<float>(paddingRatio.y));

    const auto tint = static_cast<std::uint64_t>(configInt(CFG_TINT_COLOR, DEFAULT_TINT_COLOR));
    glUniform3f(uniforms.tintColor, static_cast<float>((tint >> 24U) & 0xFFU) / 255.0F, static_cast<float>((tint >> 16U) & 0xFFU) / 255.0F,
                static_cast<float>((tint >> 8U) & 0xFFU) / 255.0F);
    glUniform1f(uniforms.tintAlpha, static_cast<float>(tint & 0xFFU) / 255.0F);

    // --- Cursor-following refraction + highlight -------------------------------
    // Convert the global cursor position into the same surface-local pixel space
    // the shader uses (origin = transformedBox top-left, device pixels).
    // NOTE: assumes a normal (non-rotated/flipped) monitor transform.
    float    cursorRadiusPx = 0.0F;
    Vector2D cursorLocal(-1.0e6, -1.0e6);
    Vector2D cursorDir(0.0, 0.0);
    float    cursorStretch = 0.0F;
    float    cursorTrail   = 0.0F;
    if (cursorEnabled() && g_pInputManager) {
        const auto mouse = g_pInputManager->getMouseCoordsInternal();
        updateCursorMotion(mouse); // ease the smoothed pos/velocity toward the live pointer

        // Use the lagged position so the dome trails the pointer.
        const Vector2D deviceLocal = (g_cursorMotion.pos - monitor->m_position) * monitor->m_scale;
        cursorLocal    = deviceLocal - Vector2D(transformedBox.x, transformedBox.y);
        cursorRadiusPx = std::clamp(configFloat(CFG_CURSOR_RADIUS, DEFAULT_CURSOR_RADIUS), 0.0F, 4000.0F) * static_cast<float>(monitor->m_scale);

        // Map pointer speed -> 0..1 against a reference speed, then scale stretch + trail.
        // The monitor transform is a uniform scale (no rotation), so the normalized
        // velocity doubles as the travel direction in surface pixel space.
        const double speed    = std::hypot(g_cursorMotion.vel.x, g_cursorMotion.vel.y);
        const double refSpeed = std::max(configFloat(CFG_CURSOR_STRETCH_SPEED, DEFAULT_CURSOR_STRETCH_SPEED), 1.0F);
        const double t        = std::clamp(speed / refSpeed, 0.0, 1.0);
        cursorStretch         = static_cast<float>(std::clamp(configFloat(CFG_CURSOR_STRETCH, DEFAULT_CURSOR_STRETCH), 0.0F, 4.0F) * t);
        cursorTrail           = static_cast<float>(std::clamp(configFloat(CFG_CURSOR_TRAIL, DEFAULT_CURSOR_TRAIL), 0.0F, 4.0F) * t);
        if (speed > 1.0)
            cursorDir = g_cursorMotion.vel / speed;
    }
    glUniform2f(uniforms.cursorPos, static_cast<float>(cursorLocal.x), static_cast<float>(cursorLocal.y));
    glUniform1f(uniforms.cursorRadius, cursorRadiusPx);
    glUniform2f(uniforms.cursorStretchDir, static_cast<float>(cursorDir.x), static_cast<float>(cursorDir.y));
    glUniform1f(uniforms.cursorStretch, cursorStretch);
    glUniform1f(uniforms.cursorTrail, cursorTrail);
    glUniform1f(uniforms.cursorIntensity, std::clamp(configFloat(CFG_CURSOR_INTENSITY, DEFAULT_CURSOR_INTENSITY), 0.0F, 4.0F));
    glUniform1f(uniforms.cursorRefraction, std::clamp(configFloat(CFG_CURSOR_REFRACTION, DEFAULT_CURSOR_REFRACTION), 0.0F, 4.0F));

    const auto cursorTint = static_cast<std::uint64_t>(configInt(CFG_CURSOR_COLOR, DEFAULT_CURSOR_COLOR));
    glUniform3f(uniforms.cursorColor, static_cast<float>((cursorTint >> 24U) & 0xFFU) / 255.0F, static_cast<float>((cursorTint >> 16U) & 0xFFU) / 255.0F,
                static_cast<float>((cursorTint >> 8U) & 0xFFU) / 255.0F);
    glUniform1f(uniforms.cursorColorAlpha, static_cast<float>(cursorTint & 0xFFU) / 255.0F);
    glUniform1i(uniforms.cursorBlendMode, cursorBlendModeCode());

    uploadToneUniforms();

    if (maskTexture) {
        glActiveTexture(GL_TEXTURE1);
        maskTexture->bind();
        glActiveTexture(GL_TEXTURE0);
    }

    glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
    g_pHyprOpenGL->scissor(rawBox);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    g_pHyprOpenGL->scissor(nullptr);
}

} // namespace LiquidGlass::GlassRenderer
