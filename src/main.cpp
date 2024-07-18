#include "glitter/util/File.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <spdlog/spdlog.h>

#include <optional>
#include <print>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>

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
        m_window = glfwCreateWindow(
            m_windowWidth, m_windowHeight, "Glitter", nullptr, nullptr);
        if (!m_window) {
            return InitializeResult::GlfwWindowError;
        }
        glfwMakeContextCurrent(m_window);

        // Resize the Viewport if the Window size changes.
        glfwSetWindowUserPointer(m_window, this);
        glfwSetWindowSizeCallback(
            m_window, [](GLFWwindow* window, int width, int height) {
                auto app = static_cast<GlitterApplication*>(
                    glfwGetWindowUserPointer(window));
                app->m_windowWidth = width;
                app->m_windowHeight = height;
                glViewport(0, 0, width, height);
            });

        glfwSetKeyCallback(m_window,
            [](GLFWwindow* window, int key, int scancode, int action,
                int mods) {
                auto app = static_cast<GlitterApplication*>(
                    glfwGetWindowUserPointer(window));
                switch (key) {
                case GLFW_KEY_SPACE:
                    if (action == GLFW_RELEASE) {
                        // We only accept up to 999 cubes.
                        if (app->m_shaderData.size()
                            >= (sizeof(ShaderData) + 64) * 999) {
                            break;
                        }

                        // The Model has to follow the Scale-Rotate-Translate
                        // order.
                        glm::mat4 model = glm::mat4(1.0f);
                        model = glm::scale(model, glm::vec3(0.25f));
                        model = glm::translate(model, glm::sphericalRand(6.0f));

                        int alignment {};
                        glGetIntegerv(
                            GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);

                        // Calculate how much padding we need.
                        size_t padding = sizeof(ShaderData) % alignment == 0
                            ? sizeof(ShaderData)
                            : alignment - (sizeof(ShaderData) % alignment);

                        // Add the new Cube to the UBO.
                        ShaderData cubeData {model, app->m_currentView,
                            app->m_currentProjection};

                        // Reserve enough space for the new Cube and its
                        // padding.
                        app->m_shaderData.reserve(sizeof(ShaderData) + 64);

                        // Push the Cube into the UBO.
                        for (size_t i = 0; i < sizeof(ShaderData); i++) {
                            app->m_shaderData.emplace_back(
                                reinterpret_cast<uint8_t*>(&cubeData)[i]);
                        }

                        // Push the padding into the UBO.
                        app->m_shaderData.insert(app->m_shaderData.end(), padding, '\0');
                    }
                    break;
                case GLFW_KEY_ESCAPE:
                    glfwSetWindowShouldClose(window, true);
                    break;
                default:
                    break;
                }
            });

        if (!gladLoadGLLoader(
                reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
            return InitializeResult::GladLoadError;
        }

        glfwSwapInterval(1);

        // Seed the RNG.
        std::srand(std::time(nullptr));

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
            [](GLenum source, GLenum type, GLuint id, GLenum severity,
                GLsizei length, const GLchar* msg, const void* userParam) {
                switch (type) {
                case GL_DEBUG_TYPE_ERROR:
                case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
                case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
                    spdlog::error("{}", msg);
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
        std::optional<std::string> vertexSrc
            = Glitter::Util::ReadFile("shaders/VertexShader.glsl");
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
        std::optional<std::string> fragmentSrc
            = Glitter::Util::ReadFile("shaders/FragShader.glsl");
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
        };
        MeshAttribute cube[] {
            {.x = -0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +0.0f},
            {.x = +0.5f, .y = -0.5f, .z = -0.5f, .u = +1.0f, .v = +0.0f},
            {.x = +0.5f, .y = +0.5f, .z = -0.5f, .u = +1.0f, .v = +1.0f},
            {.x = +0.5f, .y = +0.5f, .z = -0.5f, .u = +1.0f, .v = +1.0f},
            {.x = -0.5f, .y = +0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f},
            {.x = -0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +0.0f},
            {.x = -0.5f, .y = -0.5f, .z = +0.5f, .u = +0.0f, .v = +0.0f},
            {.x = +0.5f, .y = -0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f},
            {.x = +0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +1.0f},
            {.x = +0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +1.0f},
            {.x = -0.5f, .y = +0.5f, .z = +0.5f, .u = +0.0f, .v = +1.0f},
            {.x = -0.5f, .y = -0.5f, .z = +0.5f, .u = +0.0f, .v = +0.0f},
            {.x = -0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f},
            {.x = -0.5f, .y = +0.5f, .z = -0.5f, .u = +1.0f, .v = +1.0f},
            {.x = -0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f},
            {.x = -0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f},
            {.x = -0.5f, .y = -0.5f, .z = +0.5f, .u = +0.0f, .v = +0.0f},
            {.x = -0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f},
            {.x = +0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f},
            {.x = +0.5f, .y = +0.5f, .z = -0.5f, .u = +1.0f, .v = +1.0f},
            {.x = +0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f},
            {.x = +0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f},
            {.x = +0.5f, .y = -0.5f, .z = +0.5f, .u = +0.0f, .v = +0.0f},
            {.x = +0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f},
            {.x = -0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f},
            {.x = +0.5f, .y = -0.5f, .z = -0.5f, .u = +1.0f, .v = +1.0f},
            {.x = +0.5f, .y = -0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f},
            {.x = +0.5f, .y = -0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f},
            {.x = -0.5f, .y = -0.5f, .z = +0.5f, .u = +0.0f, .v = +0.0f},
            {.x = -0.5f, .y = -0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f},
            {.x = -0.5f, .y = +0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f},
            {.x = +0.5f, .y = +0.5f, .z = -0.5f, .u = +1.0f, .v = +1.0f},
            {.x = +0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f},
            {.x = +0.5f, .y = +0.5f, .z = +0.5f, .u = +1.0f, .v = +0.0f},
            {.x = -0.5f, .y = +0.5f, .z = +0.5f, .u = +0.0f, .v = +0.0f},
            {.x = -0.5f, .y = +0.5f, .z = -0.5f, .u = +0.0f, .v = +1.0f}};

        // Create VAO.
        GLuint VAO;
        glCreateVertexArrays(1, &VAO);

        // Create VBO.
        GLuint VBO;
        glCreateBuffers(1, &VBO);
        glNamedBufferStorage(
            VBO, sizeof(MeshAttribute) * std::size(cube), &cube, 0);

        // Attach the VBO to the VAO.
        glVertexArrayVertexBuffer(VAO, 0, VBO, 0, sizeof(MeshAttribute));

        // Declare the Position Attribute.
        glEnableVertexArrayAttrib(VAO, 0);
        glVertexArrayAttribFormat(
            VAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(MeshAttribute, x));

        // Declare the UV Attribute.
        glEnableVertexArrayAttrib(VAO, 1);
        glVertexArrayAttribFormat(
            VAO, 1, 2, GL_FLOAT, GL_FALSE, offsetof(MeshAttribute, u));

        m_currentVAO = VAO;

        // Calculate View and Projection.
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 2.5f, -3.5f),
            glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            static_cast<float>(m_windowWidth)
                / static_cast<float>(m_windowHeight),
            0.1f, 100.0f);
        m_currentView = view;
        m_currentProjection = projection;

        // Create empty UBO buffer.
        GLuint ubo {};
        glCreateBuffers(1, &ubo);
        glNamedBufferData(
            ubo, (sizeof(ShaderData) + 64) * 999, nullptr, GL_DYNAMIC_DRAW);
        m_currentUBO = ubo;

        return PrepareResult::Ok;
    }

    void Render()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Upload the ShaderData buffer into the UBO.
        glNamedBufferSubData(m_currentUBO, 0,
            sizeof(m_shaderData[0]) * m_shaderData.size(), m_shaderData.data());

        // Pass other Uniforms into the Vertex Shader.
        GLint objectColorIdx
            = glGetUniformLocation(m_currentProgram, "uObjectColor");
        glm::vec3 firstCubeColor {1.0f, 0.0f, 0.5f};
        glProgramUniform3fv(m_currentProgram, objectColorIdx, 1,
            glm::value_ptr(firstCubeColor));

        // Bind the Program and VAO.
        glUseProgram(m_currentProgram);
        glBindVertexArray(m_currentVAO);

        // Render each Cube.
        for (int i = 0; i < m_shaderData.size() / sizeof(ShaderData); i++) {
            int padding {};
            glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &padding);

            // Bind the Cube's range in the UBO.
            glBindBufferRange(GL_UNIFORM_BUFFER, 0, m_currentUBO, padding * i,
                sizeof(ShaderData));

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

    struct ShaderData {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 projection;
    };
    std::vector<uint8_t> m_shaderData {};
    glm::mat4 m_currentView {};
    glm::mat4 m_currentProjection {};
};

int main()
{
    GlitterApplication glitterApp;
    glitterApp.Run();
    return 0;
}