#include "glitter/util/File.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <spdlog/spdlog.h>

#include <optional>
#include <print>

class GlitterApplication {
public:
    void Run()
    {
        spdlog::info("Started Glitter.");

        if (Initialize() != InitializeResult::Ok) {
            Finish();
            return;
        }

        if (Prepare() != PrepareResult::Ok) {
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
        m_window = glfwCreateWindow(640, 480, "Glitter", nullptr, nullptr);
        if (!m_window) {
            return InitializeResult::GlfwWindowError;
        }
        glfwMakeContextCurrent(m_window);

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

        // Create the Vertex and Fragment shaders
        GLint shadersOk = true;

        // Vertex Shader
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
            return PrepareResult::ShaderCompileError;
        }

        // Fragment Shader
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
            return PrepareResult::ShaderCompileError;
        }

        // Link the shaders into a Program
        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);

        // The shaders are not necessary after being linked into a Program
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        // Check if the Program was linked successfully
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

        // Create VAO and VBO
        GLuint VAO, VBO;
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        // Configure the VAO and VBO
        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);

        // Declare the Position Attribute
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshAttribute),
            reinterpret_cast<void*>(offsetof(MeshAttribute, x)));
        glEnableVertexAttribArray(0);

        // Declare the UV Attribute
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(MeshAttribute),
            reinterpret_cast<void*>(offsetof(MeshAttribute, u)));
        glEnableVertexAttribArray(1);

        // Unbind VAO and VBO
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        m_currentVAO = VAO;

        return PrepareResult::Ok;
    }

    void Render()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Pass MVP into the Vertex Shader
        GLint modelIdx = glGetUniformLocation(m_currentProgram, "uModel");

        // The Model has to follow the Scale-Rotate-Translate order
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(0.5f, -0.5f, 0.0f));
        model = glm::rotate(
            model, (float)glfwGetTime(), glm::vec3(0.0f, 0.45f, 1.0f));

        glUseProgram(m_currentProgram);
        glBindVertexArray(m_currentVAO);

        // Uniforms must be set after a program is bound.
        glUniformMatrix4fv(modelIdx, 1, GL_FALSE, glm::value_ptr(model));

        // Draw!
        glDrawArrays(GL_TRIANGLES, 0, 36);

        glfwSwapBuffers(m_window);
        glfwPollEvents();
    }

    void Finish() { glfwTerminate(); }

    GLFWwindow* m_window {};
    GLuint m_currentProgram {};
    GLuint m_currentVAO {};
};

int main()
{
    GlitterApplication glitterApp;
    glitterApp.Run();
    return 0;
}