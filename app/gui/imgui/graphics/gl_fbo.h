#pragma once

#include <cstdint>

namespace SonicPi
{

struct Vec2i
{
    int x;
    int y;
};

struct Vec4f
{
    float x, y, z, w;
};

struct Fbo
{
    uint32_t fbo = 0;
    uint32_t texture = 0;
    uint32_t depth = 0;
    Vec2i size;
    Vec2i displaySize;
};

void fbo_bind(const Fbo& fbo);
void fbo_unbind(const Fbo& fbo, const Vec2i& displaySize);
Fbo fbo_create();
void fbo_resize(Fbo& fbo, const Vec2i& size);
void fbo_clear(const Vec4f& color);
void fbo_destroy(const Fbo& fbo);
void fbo_bind_texture(const Fbo& fbo, int channel);
void fbo_unbind_texture(const Fbo& fbo, int channel);

} // namespace MUtils
