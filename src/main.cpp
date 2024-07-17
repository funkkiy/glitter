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

#include <cstdint>

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
        glfwSetWindowSizeCallback(
            m_window, [](GLFWwindow* window, int width, int height) {
                auto app = static_cast<GlitterApplication*>(
                    glfwGetWindowUserPointer(window));
                app->m_windowWidth = width;
                app->m_windowHeight = height;
                glViewport(0, 0, width, height);
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

        return PrepareResult::Ok;
    }

    void Render()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // The Model has to follow the Scale-Rotate-Translate order.
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::scale(model, glm::vec3(std::sin(glfwGetTime())));
        model = glm::rotate(model, static_cast<float>(glfwGetTime()),
            glm::vec3(0.0f, 0.5f, 0.0f));
        model = glm::translate(
            model, glm::vec3(0.0f, std::sin(glfwGetTime()), 0.0f));

        // Calculate View and Projection.
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 2.5f, -3.5f),
            glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight), 0.1f,
            100.0f);

        // Pass MVP into the Vertex Shader.
        GLint modelIdx = glGetUniformLocation(m_currentProgram, "uModel");
        glProgramUniformMatrix4fv(
            m_currentProgram, modelIdx, 1, GL_FALSE, glm::value_ptr(model));
        GLint viewIdx = glGetUniformLocation(m_currentProgram, "uView");
        glProgramUniformMatrix4fv(
            m_currentProgram, viewIdx, 1, GL_FALSE, glm::value_ptr(view));
        GLint projectionIdx
            = glGetUniformLocation(m_currentProgram, "uProjection");
        glProgramUniformMatrix4fv(m_currentProgram, projectionIdx, 1, GL_FALSE,
            glm::value_ptr(projection));

        // Pass other Uniforms into the Vertex Shader.
        GLint objectColorIdx = glGetUniformLocation(m_currentProgram, "uObjectColor");
        glm::vec3 firstCubeColor {1.0f, 0.0f, 0.5f};
        glProgramUniform3fv(m_currentProgram, objectColorIdx, 1,
            glm::value_ptr(firstCubeColor));

        // Bind the Program and its VAO.
        glUseProgram(m_currentProgram);
        glBindVertexArray(m_currentVAO);

        // Draw the first cube!
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // Configure the information to draw the second cube.
        glm::mat4 secondModel = glm::mat4(1.0f);
        secondModel = glm::scale(model, glm::vec3(std::cos(glfwGetTime())));
        secondModel = glm::rotate(secondModel,
            static_cast<float>(glfwGetTime()), glm::vec3(0.0f, -0.5f, 0.0f));
        secondModel = glm::translate(secondModel, glm::vec3(-3.0f, 0.0f, 0.0f));
        glProgramUniformMatrix4fv(
            m_currentProgram, modelIdx, 1, GL_FALSE, glm::value_ptr(secondModel));

        glm::vec3 secondCubeColor {1.0f, 0.0f, 0.0f};
        glProgramUniform3fv(m_currentProgram, objectColorIdx, 1,
            glm::value_ptr(secondCubeColor));

        // Draw the second cube!
        glDrawArrays(GL_TRIANGLES, 0, 36);

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

    uint32_t m_windowWidth {640};
    uint32_t m_windowHeight {480};
};

int main()
{
    GlitterApplication glitterApp;
    glitterApp.Run();
    return 0;
}