#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string>
#include <vector>

#include <stdlib.h>
#include <string.h>

#include <GL/gl3w.h>

#include "api/logger.h"
#include "api/sonicpi_api.h"
#include "api/string_utils.h"

#include "gl_shader.h"

#undef min
#undef max

using namespace std;

namespace SonicPi
{

void compile_parse_shader_errors(CompileResult* pResult, const std::string& messages)
{
    // Try to parse the DX error string into file, line, column and message
    // Exception should catch silly mistakes.
    std::vector<std::string> errors;
    string_split(messages, "\n", errors);
    for (auto error : errors)
    {
        auto pMsg = std::make_shared<CompileMessage>();
        pMsg->filePath = pResult->fileSource.string();
        pMsg->rawText = error;

        try
        {
            auto bracketPos = error.find_first_of('(');
            if (bracketPos != std::string::npos)
            {
                auto lastBracket = error.find(")", bracketPos);
                if (lastBracket)
                {
                    // file (line, colstart-colend) error
                    pMsg->filePath = string_trim(error.substr(0, bracketPos));
                    pMsg->text = string_trim(error.substr(lastBracket, error.size() - lastBracket), " \t():\r\n\f\v");
                    std::string numbers = string_trim(error.substr(bracketPos, lastBracket - bracketPos), "( )");
                    auto numVec = string_split(numbers, ",");
                    // (3, 5 - 3)
                    if (!numVec.empty())
                    {
                        pMsg->line = std::max(0, std::stoi(numVec[0]) - 1);
                    }
                    if (numVec.size() > 1)
                    {
                        auto columnVec = string_split(numVec[1], "-");
                        if (!columnVec.empty())
                        {
                            pMsg->range.first = std::max(0, std::stoi(columnVec[0]) - 1);
                            if (columnVec.size() > 1)
                            {
                                pMsg->range.second = std::stoi(columnVec[1]);
                            }
                            else
                            {
                                pMsg->range.second = pMsg->range.first + 1;
                            }
                        }
                    }
                }
            }
            else
            {
                pMsg->text = error;
                pMsg->line = 0;
                pMsg->range = std::make_pair(0, 0);
                pMsg->msgType = CompileMessageType::Error;
            }
        }
        catch (...)
        {
            pMsg->text = "Failed to parse compiler error:\n" + error;
            pMsg->line = 0;
            pMsg->msgType = CompileMessageType::Error;
        }
        pResult->messages.push_back(pMsg);
    }
}
std::shared_ptr<GLCompileResult> gl_compile_shader(ShaderType type, const ShaderPackage& shaderPackage)
{
    auto spResult = std::make_shared<GLCompileResult>();
    spResult->fileSource = shaderPackage.path;
    spResult->Id = 0;

    try
    {
        switch (type)
        {
        case ShaderType::Vertex:
        {
            spResult->Id = glCreateShader(GL_VERTEX_SHADER);
        }
        break;
        case ShaderType::Geometry:
        {
            spResult->Id = glCreateShader(GL_GEOMETRY_SHADER);
        }
        break;
        case ShaderType::Pixel:
        {
            spResult->Id = glCreateShader(GL_FRAGMENT_SHADER);
        }
        break;
        case ShaderType::Unknown:
        {
            spResult->messages.push_back(std::make_shared<CompileMessage>(CompileMessageType::Error, "Shader type could not be determined.  Missing #shadertype tag?"));
            return spResult;
        }
        break;
        default:
        {
            spResult->messages.push_back(std::make_shared<CompileMessage>(CompileMessageType::Error, "Shader type not supported yet"));
            return spResult;
        }
        break;
        }

        std::string shader;
        if (shaderPackage.fragments.empty())
        {
            std::ifstream shaderStream(shaderPackage.path, std::ios::in);
            if (shaderStream.is_open())
            {
                std::stringstream sstr;
                sstr << shaderStream.rdbuf();
                shader = sstr.str();
                shaderStream.close();
            }
            else
            {
                spResult->messages.push_back(std::make_shared<CompileMessage>(CompileMessageType::Error, shaderPackage.path, "Couldn't open file"));
                return spResult;
            }
        }
        else
        {
            for (auto& fragment : shaderPackage.fragments)
            {
                shader += fragment.source;
            }
        }

        GLint Result = GL_FALSE;
        int InfoLogLength;

        // Compile Fragment Shader
        char const* sourcePointer = shader.c_str();
        glShaderSource(spResult->Id, 1, &sourcePointer, NULL);
        glCompileShader(spResult->Id);

        std::vector<uint32_t> counts;
        for (auto& fragment : shaderPackage.fragments)
        {
            auto count = std::count_if(fragment.source.begin(), fragment.source.end(), [](char c) {return (c == '\n'); });
            counts.push_back(uint32_t(count));
        }

        // Check Fragment Shader
        glGetShaderiv(spResult->Id, GL_COMPILE_STATUS, &Result);
        glGetShaderiv(spResult->Id, GL_INFO_LOG_LENGTH, &InfoLogLength);
        if (InfoLogLength > 0)
        {
            std::vector<char> errorMessage(InfoLogLength + 1);
            glGetShaderInfoLog(spResult->Id, InfoLogLength, NULL, &errorMessage[0]);
            compile_parse_shader_errors(spResult.get(), &errorMessage[0]);

            //file_write("d:/dev/test.txt", shader.c_str());
            // Fix up line numbers
            for (auto& pMessage : spResult->messages)
            {
                pMessage->filePath = spResult->fileSource;
                uint32_t lastCount = 0;
                uint32_t fragment = 0;
                for (auto& count : counts)
                {
                    if (pMessage->line < int32_t(count + lastCount))
                    {
                        pMessage->line = pMessage->line - lastCount;
                        pMessage->fragmentIndex = fragment;
                        break;
                    }
                    lastCount += count;
                    fragment++;
                }
            }
        }

        if (Result == GL_FALSE)
        {
            glDeleteShader(spResult->Id);
            spResult->Id = 0;
        }
        spResult->state = (Result == GL_TRUE) ? CompileState::Valid : CompileState::Invalid;
    }
    catch (std::exception& ex)
    {
        (void)&ex;
        LOG(ERR, "Exception compiling shader: " << ex.what());
    }
    return spResult;
}

std::shared_ptr<GLCompileResult> gl_link_shaders(std::shared_ptr<GLCompileResult> spVertex, std::shared_ptr<GLCompileResult> spGeometry, std::shared_ptr<GLCompileResult> spPixel)
{
    if (spVertex->state == CompileState::Invalid)
    {
        return spVertex;
    }

    if (spPixel->state == CompileState::Invalid)
    {
        return spPixel;
    }

    // Geometry allowed to be null
    if (spGeometry != nullptr && spGeometry->state == CompileState::Invalid)
    {
        return spGeometry;
    }

    auto spResult = std::make_shared<GLCompileResult>();
    spResult->fileSource = spVertex->fileSource;
    spResult->Id = 0;

    // Link the program
    GLuint ProgramID = glCreateProgram();
    glAttachShader(ProgramID, spVertex->Id);
    glAttachShader(ProgramID, spPixel->Id);
    if (spGeometry)
    {
        glAttachShader(ProgramID, spGeometry->Id);
    }
    glLinkProgram(ProgramID);

    GLint Result = GL_FALSE;
    int InfoLogLength;

    // Check the program
    glGetProgramiv(ProgramID, GL_LINK_STATUS, &Result);
    glGetProgramiv(ProgramID, GL_INFO_LOG_LENGTH, &InfoLogLength);
    if (InfoLogLength > 0)
    {
        std::vector<char> ProgramErrorMessage(InfoLogLength + 1);
        glGetProgramInfoLog(ProgramID, InfoLogLength, NULL, &ProgramErrorMessage[0]);
        compile_parse_shader_errors(spResult.get(), &ProgramErrorMessage[0]);
        for (auto& msg : spResult->messages)
        {
            msg->filePath = spResult->fileSource;
            // Try to figure out the error source in the link step!
            auto err = string_tolower(msg->rawText);
            if (err.find("vertex") != std::string::npos)
            {
                msg->filePath = spVertex->fileSource;
            }
            else
            {
                msg->filePath = spPixel->fileSource;
            }
        }
    }

    if (Result == GL_TRUE)
    {
        spResult->Id = ProgramID;
    }
    else
    {
        glDetachShader(ProgramID, spVertex->Id);
        glDetachShader(ProgramID, spPixel->Id);
        if (spGeometry != nullptr)
        {
            glDetachShader(ProgramID, spGeometry->Id);
        }
        glDeleteProgram(ProgramID);
        glGetError();
    }

    spResult->state = (Result == GL_TRUE) ? CompileState::Valid : CompileState::Invalid;
    return spResult;
}

std::shared_ptr<GLCompileResult> gl_load_program(const fs::path& vertex_file_path, const fs::path& fragment_file_path)
{
    auto pVertex = gl_compile_shader(ShaderType::Vertex, vertex_file_path);
    auto pFragment = gl_compile_shader(ShaderType::Pixel, fragment_file_path);

    if (pVertex->state == CompileState::Invalid)
    {
        return pVertex;
    }

    if (pFragment->state == CompileState::Invalid)
    {
        return pFragment;
    }

    auto pRet = gl_link_shaders(pVertex, nullptr, pFragment);
    if (pRet->state == CompileState::Valid)
    {
        glDetachShader(pRet->Id, pVertex->Id);
        glDetachShader(pRet->Id, pFragment->Id);
    }

    glDeleteShader(pVertex->Id);
    glDeleteShader(pFragment->Id);

    return pRet;
}

void gl_delete_shader(std::shared_ptr<GLCompileResult> spResult)
{
    if (spResult->state == CompileState::Valid)
    {
        glDeleteShader(spResult->Id);
    }
}

void gl_delete_program(std::shared_ptr<GLCompileResult> spResult)
{
    if (spResult->state == CompileState::Valid)
    {
        glDeleteProgram(spResult->Id);
    }
}

} // MUtils
