#include "glitter/util/File.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <spdlog/spdlog.h>

#include <stb_image.h>

#include <array>
#include <optional>
#include <print>
#include <vector>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>

class LinearAllocator {
public:
    LinearAllocator() {};

    // Returns the offset after the object in the buffer.
    template <typename T> size_t Push(T& t)
    {
        if (!m_initializedAlignment) {
            glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &m_alignment);
            m_initializedAlignment = true;
        }

        // Calculate total amount of bytes that will be pushed.
        size_t futureSize = m_buffer.size() + sizeof(T);
        size_t paddingRequired = futureSize % m_alignment == 0 ? 0 : m_alignment - (futureSize % m_alignment);
        size_t bytesRequired = futureSize + paddingRequired;
        m_buffer.reserve(m_buffer.size() + bytesRequired);

        // Push the object.
        m_buffer.insert(m_buffer.end(), reinterpret_cast<uint8_t*>(&t), reinterpret_cast<uint8_t*>(&t) + sizeof(T));

        // Push the padding.
        m_buffer.resize(m_buffer.size() + paddingRequired);

        return m_buffer.size();
    }

    uint8_t* Data() { return m_buffer.data(); }
    size_t Size() { return m_buffer.size(); }
    GLint GetAlignment()
    {
        assert(m_initializedAlignment);
        return m_alignment;
    }
    void Clear() { m_buffer.clear(); }

private:
    std::vector<uint8_t> m_buffer {};

    bool m_initializedAlignment {false};
    GLint m_alignment {};
};

class GlitterApplication {
public:
    void Run()
    {
        spdlog::info("Started Glitter.");

        if (Initialize() != InitializeResult::Ok) {
            spdlog::error("Initialize() failed!");
            Finish();
            return;
        }

        if (Prepare() != PrepareResult::Ok) {
            spdlog::error("Prepare() failed!");
            Finish();
            return;
        }

        while (!glfwWindowShouldClose(m_window)) {
            Render();
        }

        Finish();
    }

private:
    enum class [[nodiscard]] InitializeResult {
        Ok,
        GlfwInitError,
        GlfwWindowError,
        GladLoadError,
    };

    InitializeResult Initialize()
    {
        if (!glfwInit()) {
            return InitializeResult::GlfwInitError;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef _DEBUG
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif
        m_window = glfwCreateWindow(m_windowWidth, m_windowHeight, "Glitter", nullptr, nullptr);
        if (!m_window) {
            return InitializeResult::GlfwWindowError;
        }
        glfwMakeContextCurrent(m_window);

        // Resize the Viewport if the Window size changes.
        glfwSetWindowUserPointer(m_window, this);
        glfwSetWindowSizeCallback(m_window, [](GLFWwindow* window, int width, int height) {
            auto app = static_cast<GlitterApplication*>(glfwGetWindowUserPointer(window));
            app->m_windowWidth = width;
            app->m_windowHeight = height;
            glViewport(0, 0, width, height);
        });

        glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
            auto app = static_cast<GlitterApplication*>(glfwGetWindowUserPointer(window));
            switch (key) {
            case GLFW_KEY_SPACE:
                if (action == GLFW_RELEASE) {
                    // We only accept up to 999 cubes.
                    if (app->m_cubes.size() >= 999) {
                        break;
                    }

                    // The Model has to follow the Scale-Rotate-Translate
                    // order.
                    glm::mat4 model = glm::mat4(1.0f);
                    model = glm::scale(model, glm::vec3(0.25f));
                    model = glm::translate(model, glm::sphericalRand(6.0f));

                    app->m_cubes.push_back(
                        Cube {.m_position = model, .m_texture = app->m_loadedTextures[std::rand() % app->m_loadedTextures.size()]});
                }
                break;
            case GLFW_KEY_ESCAPE:
                glfwSetWindowShouldClose(window, true);
                break;
            default:
                break;
            }
        });

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
            return InitializeResult::GladLoadError;
        }

        glfwSwapInterval(1);

        // Seed the RNG.
        std::srand(static_cast<unsigned int>(std::time(nullptr)));

        return InitializeResult::Ok;
    }

    enum class [[nodiscard]] PrepareResult {
        Ok,
        ShaderLoadError,
        ShaderCompileError,
        ProgramLinkError,
    };

    PrepareResult Prepare()
    {
#ifdef _DEBUG
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(
            [](GLenum source, GLenum type, GLuint /*id*/, GLenum /*severity*/, GLsizei /*length*/, const GLchar* msg,
                const void* /*userParam*/) {
                switch (type) {
                case GL_DEBUG_TYPE_ERROR:
                case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
                case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
                    spdlog::error("{} {}", source, msg);
                    break;
                default:
                    spdlog::warn("{}", msg);
                    break;
                }
            },
            nullptr);
#endif

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glEnable(GL_DEPTH_TEST);

        // Create the Vertex and Fragment shaders.
        GLint shadersOk = true;

        auto printErrorMsg = [](GLuint shader, const char* shaderType) {
            GLchar error[512];
            GLsizei errorLen = 0;
            glGetShaderInfoLog(shader, 512, &errorLen, error);
            spdlog::error("[{}] {}", shaderType, error);
        };

        // Vertex Shader.
        std::optional<std::string> vertexSrc = Glitter::Util::ReadFile("shaders/VertexShader.glsl");
        if (!vertexSrc) {
            return PrepareResult::ShaderLoadError;
        }
        const char* vertexSrcRaw = vertexSrc.value().c_str();
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexSrcRaw, nullptr);
        glCompileShader(vertexShader);
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &shadersOk);
        if (!shadersOk) {
            printErrorMsg(vertexShader, "vertex");
            return PrepareResult::ShaderCompileError;
        }

        // Fragment Shader.
        std::optional<std::string> fragmentSrc = Glitter::Util::ReadFile("shaders/FragShader.glsl");
        if (!fragmentSrc) {
            return PrepareResult::ShaderLoadError;
        }
        const char* fragmentSrcRaw = fragmentSrc.value().c_str();
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentSrcRaw, nullptr);
        glCompileShader(fragmentShader);
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &shadersOk);
        if (!shadersOk) {
            printErrorMsg(fragmentShader, "fragment");
            return PrepareResult::ShaderCompileError;
        }

        // Link the shaders into a Program.
        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);

        // The shaders can be safely deleted after being linked into a Program.
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        // Check if the Program was linked successfully.
        glGetProgramiv(shaderProgram, GL_LINK_STATUS, &shadersOk);
        if (!shadersOk) {
            return PrepareResult::ProgramLinkError;
        }
        m_currentProgram = shaderProgram;

        // Cube!
        struct MeshAttribute {
            float x, y, z;
            float u, v;
            float nx, ny, nz;
        };
        MeshAttribute cube[] {{.x = -0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +0.0f, .nx = +0.0f, .ny = +0.0f, .nz = -1.0f},
            {.x = +0.5f, .y = -0.5f, .z = -0.5f, .u = +1.0f, .v = +0.0f, .nx = +0.0f, .ny = +0.0f, .nz = -1.0f},
            {.x = +0.5f, .y = +0.5f, .z = -0.5f, .u = +1.0f, .v = +1.0f, .nx = +0.0f, .ny = +0.0f, .nz = -1.0f},
            {.x = +0.5f, .y = +0.5f, .z = -0.5f, .u = +1.0f, .v = +1.0f, .nx = +0.0f, .ny = +0.0f, .nz = -1.0f},
            {.x = -0.5f, .y = +0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f, .nx = +0.0f, .ny = +0.0f, .nz = -1.0f},
            {.x = -0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +0.0f, .nx = +0.0f, .ny = +0.0f, .nz = -1.0f},
            {.x = -0.5f, .y = -0.5f, .z = +0.5f, .u = +0.0f, .v = +0.0f, .nx = +0.0f, .ny = +0.0f, .nz = +1.0f},
            {.x = +0.5f, .y = -0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f, .nx = +0.0f, .ny = +0.0f, .nz = +1.0f},
            {.x = +0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +1.0f, .nx = +0.0f, .ny = +0.0f, .nz = +1.0f},
            {.x = +0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +1.0f, .nx = +0.0f, .ny = +0.0f, .nz = +1.0f},
            {.x = -0.5f, .y = +0.5f, .z = +0.5f, .u = +0.0f, .v = +1.0f, .nx = +0.0f, .ny = +0.0f, .nz = +1.0f},
            {.x = -0.5f, .y = -0.5f, .z = +0.5f, .u = +0.0f, .v = +0.0f, .nx = +0.0f, .ny = +0.0f, .nz = +1.0f},
            {.x = -0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f, .nx = -1.0f, .ny = +0.0f, .nz = +0.0f},
            {.x = -0.5f, .y = +0.5f, .z = -0.5f, .u = +1.0f, .v = +1.0f, .nx = -1.0f, .ny = +0.0f, .nz = +0.0f},
            {.x = -0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f, .nx = -1.0f, .ny = +0.0f, .nz = +0.0f},
            {.x = -0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f, .nx = -1.0f, .ny = +0.0f, .nz = +0.0f},
            {.x = -0.5f, .y = -0.5f, .z = +0.5f, .u = +0.0f, .v = +0.0f, .nx = -1.0f, .ny = +0.0f, .nz = +0.0f},
            {.x = -0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f, .nx = -1.0f, .ny = +0.0f, .nz = +0.0f},
            {.x = +0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f, .nx = +1.0f, .ny = +0.0f, .nz = +0.0f},
            {.x = +0.5f, .y = +0.5f, .z = -0.5f, .u = +1.0f, .v = +1.0f, .nx = +1.0f, .ny = +0.0f, .nz = +0.0f},
            {.x = +0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f, .nx = +1.0f, .ny = +0.0f, .nz = +0.0f},
            {.x = +0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f, .nx = +1.0f, .ny = +0.0f, .nz = +0.0f},
            {.x = +0.5f, .y = -0.5f, .z = +0.5f, .u = +0.0f, .v = +0.0f, .nx = +1.0f, .ny = +0.0f, .nz = +0.0f},
            {.x = +0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f, .nx = +1.0f, .ny = +0.0f, .nz = +0.0f},
            {.x = -0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f, .nx = +0.0f, .ny = -1.0f, .nz = +0.0f},
            {.x = +0.5f, .y = -0.5f, .z = -0.5f, .u = +1.0f, .v = +1.0f, .nx = +0.0f, .ny = -1.0f, .nz = +0.0f},
            {.x = +0.5f, .y = -0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f, .nx = +0.0f, .ny = -1.0f, .nz = +0.0f},
            {.x = +0.5f, .y = -0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f, .nx = +0.0f, .ny = -1.0f, .nz = +0.0f},
            {.x = -0.5f, .y = -0.5f, .z = +0.5f, .u = +0.0f, .v = +0.0f, .nx = +0.0f, .ny = -1.0f, .nz = +0.0f},
            {.x = -0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f, .nx = +0.0f, .ny = -1.0f, .nz = +0.0f},
            {.x = -0.5f, .y = +0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f, .nx = +0.0f, .ny = +1.0f, .nz = +0.0f},
            {.x = +0.5f, .y = +0.5f, .z = -0.5f, .u = +1.0f, .v = +1.0f, .nx = +0.0f, .ny = +1.0f, .nz = +0.0f},
            {.x = +0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f, .nx = +0.0f, .ny = +1.0f, .nz = +0.0f},
            {.x = +0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f, .nx = +0.0f, .ny = +1.0f, .nz = +0.0f},
            {.x = -0.5f, .y = +0.5f, .z = +0.5f, .u = +0.0f, .v = +0.0f, .nx = +0.0f, .ny = +1.0f, .nz = +0.0f},
            {.x = -0.5f, .y = +0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f, .nx = +0.0f, .ny = +1.0f, .nz = +0.0f}};

        // Create VAO.
        GLuint VAO;
        glCreateVertexArrays(1, &VAO);

        // Create VBO.
        GLuint VBO;
        glCreateBuffers(1, &VBO);
        glNamedBufferStorage(VBO, sizeof(MeshAttribute) * std::size(cube), &cube, 0);

        // Attach the VBO to the VAO.
        glVertexArrayVertexBuffer(VAO, 0, VBO, 0, sizeof(MeshAttribute));

        // Declare the Position Attribute.
        glEnableVertexArrayAttrib(VAO, 0);
        glVertexArrayAttribFormat(VAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(MeshAttribute, x));
        glVertexArrayAttribBinding(VAO, 0, 0);

        // Declare the UV Attribute.
        glEnableVertexArrayAttrib(VAO, 1);
        glVertexArrayAttribFormat(VAO, 1, 2, GL_FLOAT, GL_FALSE, offsetof(MeshAttribute, u));
        glVertexArrayAttribBinding(VAO, 1, 0);

        // Declare the Normal attribute
        glEnableVertexArrayAttrib(VAO, 2);
        glVertexArrayAttribFormat(VAO, 2, 3, GL_FLOAT, GL_FALSE, offsetof(MeshAttribute, nx));
        glVertexArrayAttribBinding(VAO, 2, 0);

        m_currentVAO = VAO;

        // Create empty UBO buffer.
        GLuint ubo {};
        glCreateBuffers(1, &ubo);

        // Just enough for the Common stuff and 999 Cubes.
        glNamedBufferData(ubo, sizeof(CommonData) + (sizeof(PerDrawData) * 999), nullptr, GL_DYNAMIC_DRAW);
        m_currentUBO = ubo;

        // Load some Cube textures.
        std::array texturePaths(std::to_array<const char*>({"textures/Tile.png", "textures/Cobble.png"}));

        for (auto& path : texturePaths) {
            GLuint texture {};
            glCreateTextures(GL_TEXTURE_2D, 1, &texture);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            int width, height, nChannels;
            unsigned char* textureData = stbi_load(path, &width, &height, &nChannels, 4);
            if (textureData) {
                glTextureStorage2D(texture, 1, GL_RGBA8, width, height);
                glTextureSubImage2D(texture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, textureData);
                glGenerateTextureMipmap(texture);
            }
            stbi_image_free(textureData);
            m_loadedTextures.push_back(texture);
        }

        return PrepareResult::Ok;
    }

    void Render()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Clear the UBO CPU-backing buffer.
        m_uboAllocator.Clear();

        // Calculate View and Projection.
        glm::vec3 eyePos = glm::vec3(0.0f, 2.5f, -3.5f);
        glm::mat4 view = glm::lookAt(eyePos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f), static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight), 0.1f, 100.0f);
        m_currentView = view;
        m_currentProjection = projection;

        // Write the CommonData into the UBO-backing CPU buffer.
        CommonData commonData = {.m_view = view,
            .m_projection = projection,
            .m_eyePos = glm::vec4(eyePos, 1.0),
            .m_lightPos = glm::vec4(1.0, 0.5, -0.5, 1.0),
            .m_lightColor = glm::vec4(1.0, 0.0, 0.0, 1.0)};
        m_uboAllocator.Push(commonData);

        // Write each Cube's PerDrawData into the buffer.
        for (auto& cube : m_cubes) {
            PerDrawData shaderData {cube.m_position};
            m_uboAllocator.Push(shaderData);
        }

        // Upload the CPU-backing buffer into the UBO.
        glNamedBufferSubData(m_currentUBO, 0, sizeof(uint8_t) * m_uboAllocator.Size(), m_uboAllocator.Data());

        // Bind the Program and VAO.
        glUseProgram(m_currentProgram);
        glBindVertexArray(m_currentVAO);

        // Render each Cube.
        for (int i = 0; i < m_cubes.size(); i++) {
            // Bind the Common UBO data into the first slot of the UBO.
            glBindBufferRange(GL_UNIFORM_BUFFER, 0, m_currentUBO, 0, sizeof(CommonData));

            // Bind the Per-Draw UBO data into the second slot of the UBO.
            glBindBufferRange(GL_UNIFORM_BUFFER, 1, m_currentUBO, m_uboAllocator.GetAlignment() * (i + 1), sizeof(PerDrawData));

            // Bind the texture.
            glBindTextureUnit(0, m_cubes[i].m_texture);

            // Draw the Cube!
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        glfwSwapBuffers(m_window);
        glfwPollEvents();
    }

    void Finish()
    {
        spdlog::info("Stopping...");
        glfwTerminate();
    }

    GLFWwindow* m_window {};
    GLuint m_currentProgram {};
    GLuint m_currentVAO {};
    GLuint m_currentUBO {};

    uint32_t m_windowWidth {640};
    uint32_t m_windowHeight {480};

    glm::mat4 m_currentView {};
    glm::mat4 m_currentProjection {};

    struct CommonData {
        glm::mat4 m_view;
        glm::mat4 m_projection;
        glm::vec4 m_eyePos;
        glm::vec4 m_lightPos;
        glm::vec4 m_lightColor;
    };
    struct PerDrawData {
        glm::mat4 m_model;
    };
    struct ShaderData {
        CommonData m_commonData;
        PerDrawData m_perDrawData;
    };
    LinearAllocator m_uboAllocator {};

    std::vector<GLuint> m_loadedTextures {};

    struct Cube {
        glm::mat4 m_position;
        GLuint m_texture;
    };
    std::vector<Cube> m_cubes {};
};

int main()
{
    GlitterApplication glitterApp;
    glitterApp.Run();
    return 0;
}