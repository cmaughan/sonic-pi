#pragma once

#include <GL/gl3w.h>

#include "api/file_utils.h"

namespace SonicPi
{

enum class ShaderType
{
    Unknown,
    Pixel,
    Vertex,
    Geometry,
    Compute,
};

struct ShaderFragment
{
    std::string name;
    std::string source;
};

struct ShaderPackage
{
    ShaderPackage(const fs::path& p)
        : path(p)
    {}

    std::vector<ShaderFragment> fragments;
    uint32_t mainFragment = 0;
    fs::path path;
    //MUtils::MetaTags_t tags;
};

// Messages during compile steps
enum class CompileMessageType
{
    Warning,
    Error,
    Info
};

struct CompileMessage
{
    CompileMessage() {};
    CompileMessage(CompileMessageType type, const fs::path& path)
        :filePath(path),
        text("Unknown issue compiling: " + path.string())
    {
    }

    CompileMessage(CompileMessageType type, const fs::path& path, const std::string& message)
        :filePath(path),
        text(message)
    {
    }

    CompileMessage(CompileMessageType type, const fs::path& path, const std::string& message, int line)
        :filePath(path),
        text(message),
        line(line)
    {
    }

    // Text before it is parsed
    std::string rawText;

    // Parsed message, file, line, buffer range and type
    std::string text;
    fs::path filePath;
    int32_t line = 0;
    uint32_t fragmentIndex = 0;
    std::pair<int32_t, int32_t> range = std::make_pair(-1, -1);
    CompileMessageType msgType = CompileMessageType::Error;
};

enum class CompileState
{
    Invalid,
    Valid
};

struct CompileResult
{
    fs::path fileSource;
    std::vector<std::shared_ptr<CompileMessage>> messages;
    CompileState state = CompileState::Invalid;
    //MetaTags_t tags;
};

struct GLCompileResult : CompileResult
{
    GLuint Id;
};

std::shared_ptr<GLCompileResult> gl_compile_shader(ShaderType type, const ShaderPackage& package);
std::shared_ptr<GLCompileResult> gl_load_program(const fs::path& vertex_file_path, const fs::path& fragment_file_path);
std::shared_ptr<GLCompileResult> gl_link_shaders(std::shared_ptr<GLCompileResult> spVertex, std::shared_ptr<GLCompileResult> spGeometry, std::shared_ptr<GLCompileResult> spPixel);

void gl_delete_program(std::shared_ptr<GLCompileResult> spResult);
void gl_delete_shader(std::shared_ptr<GLCompileResult> spResult);

void gl_parse_messages(std::shared_ptr<GLCompileResult>& spResult, const std::string& messages);

}

