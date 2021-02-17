#include "imgui.h"
#include <array>

#include "app.h"
#include "shader_window.h"
#include "scope_window.h"

#include "graphics/gl_fbo.h"
#include "graphics/gl_shader.h"

namespace SonicPi
{
namespace
{
Fbo fbo[2];
uint32_t currentFBO = 0;
uint32_t vao = 0;
uint32_t vbo = 0;
uint32_t iTimeParam = 0;
uint32_t iChannel0 = 0;
uint32_t iResolutionParam = 0;
uint32_t iSpectrum = 0;
uint64_t timeStart;
std::shared_ptr<GLCompileResult> spProgram;
uint32_t renderCount = 0;
} // namespace

// Based on:
// https://www.shadertoy.com/view/4lSyRw
std::string strVShader = R"(
#version 330

in vec3 positionsIn;
in vec2 texCoordsIn;

out vec2 texCoord;

void main() {
    gl_Position = vec4(positionsIn, 1.0);
}
)";

std::string strShader = R"(
#version 330

out vec4 fragColor;

uniform vec2 iResolution;
uniform sampler2D iChannel0;
uniform float iTime;
uniform vec4 iSpectrum;

mat2 rot(float a)
{
	float c = cos(a); float s = sin(a) + .2f;
    return mat2(c, s, -s, c);
}


void mainImage(in vec2 fragCoord)
{
	vec2 uv = fragCoord.xy - 0.5*iResolution.xy;
    uv *= 2.0/iResolution.y;

    float aud = (iSpectrum.y -.5f) * 1.5f;
    uv *= 2.0*rot(iTime + aud); 
    
    vec2 UV = fragCoord.xy - 0.5*iResolution.xy;
    UV *= rot(0.1 *sin(iTime + aud))*(1.0 + 0.05 *sin(0.5*iTime + aud));
    UV += 0.5*iResolution.xy;
    UV /= iResolution.xy;
   
    float offset = 0.0005f; 
    float s = abs(uv.x) + abs(uv.y); // "Square"
    float s1 = abs(uv.x + offset) + abs(uv.y + offset); // "Square"
    float s2 = abs(uv.x - offset) + abs(uv.y - offset); // "Square"
    float s3 = abs(uv.x - offset) + abs(uv.y + offset); // "Square"
    
    fragColor = 0.95*texture(iChannel0, UV);
    fragColor += vec4(iSpectrum.x) * ( step(1.0, s) - step(1.025, s)) * vec4(1.0f, 0.0f, 0.0f, 0.0f);
    fragColor += vec4(iSpectrum.y) * ( step(1.0, s1) - step(1.025, s1)) * vec4(0.5f, 1.0f, 0.0f, 0.0f);
    fragColor += vec4(iSpectrum.z) * ( step(1.0, s2) - step(1.025, s2)) * vec4(0.2f, .5f, 0.0f, 0.0f);
    fragColor += vec4(iSpectrum.w) * ( step(1.0, s3) - step(1.025, s3)) * vec4(0.0f, 0.5f, 0.2f, 0.0f);
}

void main()
{
    mainImage(gl_FragCoord.xy);
}

)";

auto Coords = std::array<float, 20>{ -1.0f, -1.0f, 0.0f,
    1.0f, -1.0f, 0.0f,
    1.0f, 1.0f, 0.0f,
    -1.0f, 1.0f, 0.0f,

    0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f };

void dump_shader_errors(std::shared_ptr<GLCompileResult> spResult)
{
    if (spResult->Id != 0 && spResult->messages.empty())
    {
        return;
    }

    for (auto& msg : spResult->messages)
    {
        LOG(DBG, msg->line << " : " << msg->text);
    }
}
//
void shader_window_init()
{
    if (!fbo[0].fbo)
    {
        fbo[0] = fbo_create();
        fbo[1] = fbo_create();

        auto shader = ShaderPackage(fs::path());
        ShaderFragment main;
        main.name = "mainPS";
        main.source = strShader;
        shader.fragments.push_back(main);
        auto pixelShader = gl_compile_shader(ShaderType::Pixel, shader);
        dump_shader_errors(pixelShader);

        auto vertex_shader = ShaderPackage(fs::path());
        main.name = "mainVS";
        main.source = strVShader;
        vertex_shader.fragments.push_back(main);
        auto vertexShader = gl_compile_shader(ShaderType::Vertex, vertex_shader);
        dump_shader_errors(vertexShader);

        spProgram = gl_link_shaders(vertexShader, nullptr, pixelShader);
        dump_shader_errors(spProgram);

        glUseProgram(spProgram->Id);

        iChannel0 = glGetUniformLocation(spProgram->Id, "iChannel0");
        iTimeParam = glGetUniformLocation(spProgram->Id, "iTime");
        iResolutionParam = glGetUniformLocation(spProgram->Id, "iResolution");
        iSpectrum = glGetUniformLocation(spProgram->Id, "iSpectrum");

        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (void*)(sizeof(float) * 3 * 4));
        glBufferData(GL_ARRAY_BUFFER, Coords.size() * sizeof(float), &Coords[0], GL_STATIC_DRAW);

        glBindVertexArray(0);
        glUseProgram(0);

        timeStart = timer_start();
    }
}

void shader_window_close()
{
    if (fbo[0].fbo)
    {
        fbo_destroy(fbo[0]);
    }
    if (fbo[1].fbo)
    {
        fbo_destroy(fbo[1]);
    }
}

// Demonstrate creating a simple log window with basic filtering.
void shader_window_show()
{
    shader_window_init();

    if (fbo[currentFBO].fbo == 0)
    {
        return;
    }

    ImGui::Begin("Shader");

//#define IN_WINDOW
#ifdef IN_WINDOW 
    auto min = ImGui::GetWindowDrawList()->GetClipRectMin();
    auto max = ImGui::GetWindowDrawList()->GetClipRectMax();
#else
    auto min = ImGui::GetBackgroundDrawList()->GetClipRectMin();
    auto max = ImGui::GetBackgroundDrawList()->GetClipRectMax();
#endif

    auto size = Vec2i{ int(max.x - min.x), int(max.y - min.y) };
    auto pos = min;
    auto displaySize = ImGui::GetIO().DisplaySize;

    fbo_resize(fbo[currentFBO], size);

    fbo_bind(fbo[currentFBO]);
    fbo_bind_texture(fbo[1 - currentFBO], 0);

    if (renderCount < 2)
    {
        fbo_clear(Vec4f{ 0.0f, 0.0f, 0.0f, 1.0f });
        renderCount++;
    }

    glBindVertexArray(vao);
    glUseProgram(spProgram->Id);

    auto time = timer_stop(timeStart);
    float s1, s2, s3, s4;
    s1 = s2 = s3 = s4 = 0.0f;
    scope_window_get_spectrum(s1, s2, s3, s4);
    glUniform1i(iChannel0, 0);
    glUniform1f(iTimeParam, time);
    glUniform2f(iResolutionParam, float(size.x), float(size.y));
    glUniform4f(iSpectrum, s1, s2, s3, s4);

    glDrawArrays(GL_QUADS, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);

    fbo_unbind(fbo[currentFBO], Vec2i{ int(displaySize.x), int(displaySize.y) });
    fbo_unbind_texture(fbo[1 - currentFBO], 0);

#ifdef IN_WINDOW 
    ImGui::GetWindowDrawList()->AddImage(*(ImTextureID*)&fbo[currentFBO].texture, pos, ImVec2(float(pos.x + size.x), float(pos.y + size.y)), ImVec2(0, 1), ImVec2(1, 0));
#else
    ImGui::GetBackgroundDrawList()->AddImage(*(ImTextureID*)&fbo[currentFBO].texture, pos, ImVec2(float(pos.x + size.x), float(pos.y + size.y)), ImVec2(0, 1), ImVec2(1, 0));
#endif

    currentFBO = 1 - currentFBO;
    ImGui::End();
}

} // namespace SonicPi
