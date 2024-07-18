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
                        app->m_cubes.emplace_back(
                            Cube {.pos = glm::sphericalRand(6.0f)});
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

        // Calculate View and Projection.
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 2.5f, -3.5f),
            glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            static_cast<float>(m_windowWidth)
                / static_cast<float>(m_windowHeight),
            0.1f, 100.0f);

        // Pass View and Projection into the Vertex Shader using SSBOs.
        struct ShaderData {
            glm::mat4 view;
            glm::mat4 projection;
        };
        ShaderData meshData = {view, projection};

        GLuint ssbo = 0;
        glCreateBuffers(1, &ssbo);
        glNamedBufferStorage(ssbo, sizeof(ShaderData), &meshData, 0);
        m_currentSSBO = ssbo;

        m_currentVAO = VAO;

        return PrepareResult::Ok;
    }

    void Render()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Obtain the location for the View Uniform.
        GLint modelIdx = glGetUniformLocation(m_currentProgram, "uModel");

        // Pass other Uniforms into the Vertex Shader.
        GLint objectColorIdx
            = glGetUniformLocation(m_currentProgram, "uObjectColor");
        glm::vec3 firstCubeColor {1.0f, 0.0f, 0.5f};
        glProgramUniform3fv(m_currentProgram, objectColorIdx, 1,
            glm::value_ptr(firstCubeColor));

        // Bind the Program and its VAO.
        glUseProgram(m_currentProgram);
        glBindVertexArray(m_currentVAO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_currentSSBO);

        // Render each Cube.
        for (auto& cube : m_cubes) {
            // The Model has to follow the Scale-Rotate-Translate order.
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::scale(model, glm::vec3(0.25f));
            model = glm::translate(model, cube.pos);

            // Pass the Model into the Vertex Shader.
            glProgramUniformMatrix4fv(
                m_currentProgram, modelIdx, 1, GL_FALSE, glm::value_ptr(model));

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
    GLuint m_currentSSBO {};

    uint32_t m_windowWidth {640};
    uint32_t m_windowHeight {480};

    struct Cube {
        glm::vec3 pos;
    };
    std::vector<Cube> m_cubes;
};

int main()
{
    GlitterApplication glitterApp;
    glitterApp.Run();
    return 0;
}