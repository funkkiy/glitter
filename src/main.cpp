#include "glitter/Config.h"
#include "glitter/ImGuiConfig.h"
#include "glitter/util/File.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <cgltf.h>
#include <stb_image.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <array>
#include <expected>
#include <optional>
#include <print>
#include <vector>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>

template <typename Into, typename From> constexpr Into narrow_into(From x)
{
    static_assert(std::is_arithmetic_v<From> && std::is_arithmetic_v<Into>, "narrow_into requires arithmetic types");
    static_assert(std::is_integral_v<From> && std::is_integral_v<Into>, "narrow_into requires integral types");
    static_assert(!std::is_same_v<Into, From>, "narrow_into requires From and Into to be different types");
    static_assert(sizeof(From) >= sizeof(Into), "narrow_into requires From to be bigger than Into");

    if (std::cmp_less(x, std::numeric_limits<Into>::min())) {
        return std::numeric_limits<Into>::min();
    }

    if (std::cmp_greater(x, std::numeric_limits<Into>::max())) {
        return std::numeric_limits<Into>::max();
    }

    return static_cast<Into>(x);
}

class LinearAllocator {
public:
    LinearAllocator() = default;

    // Returns the offset after the object in the buffer.
    template <typename T> size_t Push(T& t)
    {
        InitializeAlignment();

        // Calculate total amount of bytes that will be pushed.
        size_t sizeAfterT = m_buffer.size() + sizeof(T);
        size_t paddingRequired = sizeAfterT % m_alignment == 0 ? 0 : m_alignment - (sizeAfterT % m_alignment);

        size_t offsetBeforePush = m_buffer.size();
        m_buffer.resize(sizeAfterT + paddingRequired);

        // Push the object.
        std::memcpy(m_buffer.data() + offsetBeforePush, &t, sizeof(T));

        // Push the padding.
        std::memset(m_buffer.data() + offsetBeforePush + sizeof(T), 0, paddingRequired);

        return offsetBeforePush;
    }

    std::byte* Data() { return m_buffer.data(); }
    size_t Size() { return m_buffer.size(); }
    GLint GetAlignment()
    {
        InitializeAlignment();
        return m_alignment;
    }
    void Clear() { m_buffer.clear(); }

private:
    void InitializeAlignment()
    {
        if (!m_initializedAlignment) {
            glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &m_alignment);
            m_initializedAlignment = true;
        }
    }

    std::vector<std::byte> m_buffer;

    bool m_initializedAlignment {false};
    GLint m_alignment {};
};

struct Primitive {
    GLuint m_vbo;
    GLuint m_ebo;
    GLuint m_baseTexture;
    GLsizei m_elementCount;
};

struct AABB {
    glm::vec3 m_localMin;
    glm::vec3 m_localMax;
};

struct Mesh {
    std::vector<Primitive> m_primitives;

    // Axis-Aligned Bounding Box for frustum culling.
    AABB m_aabb;
};

// Vertex Attributes!
struct MeshVertex {
    float x, y, z;
    float u, v;
    float nx, ny, nz;
};

[[nodiscard]] std::optional<GLuint> CreateShader(GLenum type, const char* src)
{
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) {
        return std::nullopt;
    }

    GLint res = GL_FALSE;

    GLuint shader = glCreateShader(type);
    glObjectLabel(GL_SHADER, shader, -1, type == GL_VERTEX_SHADER ? "Vertex Shader" : "Fragment Shader");
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &res);

    if (res != GL_TRUE) {
        {
            std::array<GLchar, 512> error {};
            glGetShaderInfoLog(shader, 512, nullptr, error.data());
            spdlog::error("[{}] {}", type == GL_VERTEX_SHADER ? "vertex" : "fragment", error.data());
        }
        glDeleteShader(shader);
        return std::nullopt;
    }

    return shader;
}

[[nodiscard]] std::optional<GLuint> CreateShaderFromPath(GLenum type, const char* path)
{
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) {
        return std::nullopt;
    }

    std::optional<std::string> src = Glitter::Util::ReadFile(path);
    if (!src) {
        return std::nullopt;
    }

    return CreateShader(type, src.value().c_str());
}

[[nodiscard]] std::optional<GLuint> LinkProgram(GLuint vertexShader, GLuint fragmentShader, const char* name)
{
    GLuint program = glCreateProgram();
    glObjectLabel(GL_PROGRAM, program, -1, name);
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    // The shaders can be safely deleted after being linked into a Program.
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Check if the Program was linked successfully.
    GLint res = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &res);
    if (res == GL_FALSE) {
        std::array<GLchar, 512> error {};
        glGetProgramInfoLog(program, 512, nullptr, error.data());
        spdlog::error("[program] {}", error.data());

        glDeleteProgram(program);
        return std::nullopt;
    }

    return program;
}

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
            Tick();
            Render();
        }

        Finish();
    }

private:
    enum class [[nodiscard]] InitializeResult : std::uint8_t {
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
            auto* app = static_cast<GlitterApplication*>(glfwGetWindowUserPointer(window));

            if (width == 0 || height == 0) {
                return;
            }

            app->m_windowWidth = width;
            app->m_windowHeight = height;
            glViewport(0, 0, width, height);

            // Create new color and depth attachments for the FBO.
            GLuint oldColor = app->m_fboColor;
            GLuint oldDepth = app->m_fboDepth;

            // Create the color texture used with the FBO.
            GLuint fboColor = 0;
            glCreateTextures(GL_TEXTURE_2D, 1, &fboColor);
            glTextureStorage2D(fboColor, 1, GL_RGBA8, width, height);
            glObjectLabel(GL_TEXTURE, fboColor, -1, "Post-Processing FBO Color Texture");

            // Create the depth renderbuffer (note: can't be sampled) used with the FBO.
            GLuint fboDepth = 0;
            glCreateRenderbuffers(1, &fboDepth);
            glNamedRenderbufferStorage(fboDepth, GL_DEPTH_COMPONENT24, width, height);
            glObjectLabel(GL_RENDERBUFFER, fboDepth, -1, "Post-Processing FBO Depth Renderbuffer");

            // Attach the textures to the FBO.
            glNamedFramebufferTexture(app->m_fbo, GL_COLOR_ATTACHMENT0, fboColor, 0);
            glNamedFramebufferRenderbuffer(app->m_fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);

            app->m_fboColor = fboColor;
            app->m_fboDepth = fboDepth;

            // Delete the old FBO attachments.
            glDeleteTextures(1, &oldColor);
            glDeleteRenderbuffers(1, &oldDepth);
        });

        glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
            auto* app = static_cast<GlitterApplication*>(glfwGetWindowUserPointer(window));
            switch (key) {
            case GLFW_KEY_SPACE:
                if (action == GLFW_RELEASE) {
                    size_t nodesPerPress = Glitter::Config::MAX_NODES / 20;

                    // We only accept up to Glitter::Config::MAX_NODES nodes, because UBO reallocation hasn't been implemented yet.
                    if (app->m_nodes.size() >= Glitter::Config::MAX_NODES) {
                        break;
                    }
                    if (app->m_nodes.size() + nodesPerPress > Glitter::Config::MAX_NODES) {
                        nodesPerPress = Glitter::Config::MAX_NODES - app->m_nodes.size();
                    }

                    for (size_t i = 0; i < nodesPerPress; i++) {
                        app->m_nodes.push_back(Node {.m_position = glm::sphericalRand(45.0f),
                            .m_scale = glm::vec3(0.25f),
                            .m_meshID = std::rand() % app->m_meshes.size(),
                            .m_uboOffset = 0,
                            .m_texture = app->m_loadedTextures[std::rand() % app->m_loadedTextures.size()],
                            .m_opacity = 1.0f,
                            .m_shouldAnimate = true,
                            .m_culled = false});
                    }
                }
                break;
            case GLFW_KEY_K:
                if (action == GLFW_RELEASE) {
                    app->m_frustumCulling = !app->m_frustumCulling;
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

        // Initialize Dear ImGui context.
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Initialize Dear ImGui backend.
        ImGui_ImplGlfw_InitForOpenGL(m_window, true);
        ImGui_ImplOpenGL3_Init("#version 460");

        // Apply Dear ImGui theme.
        {
            ImVec4* colors = ImGui::GetStyle().Colors;
            colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
            colors[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.01f, 0.02f, 0.94f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
            colors[ImGuiCol_Border] = ImVec4(0.71f, 0.60f, 0.91f, 0.33f);
            colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.07f, 0.12f, 0.89f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.29f, 0.28f, 0.34f, 0.94f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.41f, 0.18f, 0.56f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
            colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
            colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.60f, 0.20f, 0.87f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.65f, 0.24f, 0.88f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.88f, 0.06f, 0.47f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.86f, 0.18f, 0.61f, 0.40f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.76f, 0.21f, 0.74f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.10f, 0.52f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.97f, 0.21f, 0.49f, 0.31f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.87f, 0.37f, 0.65f, 0.80f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.78f, 0.10f, 0.30f, 1.00f);
            colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.18f, 0.86f, 0.50f);
            colors[ImGuiCol_SeparatorHovered] = ImVec4(0.42f, 0.13f, 0.69f, 0.78f);
            colors[ImGuiCol_SeparatorActive] = ImVec4(0.55f, 0.04f, 0.80f, 1.00f);
            colors[ImGuiCol_ResizeGrip] = ImVec4(0.78f, 0.50f, 0.87f, 0.20f);
            colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.54f, 0.14f, 0.92f, 0.67f);
            colors[ImGuiCol_ResizeGripActive] = ImVec4(0.51f, 0.04f, 0.86f, 0.95f);
            colors[ImGuiCol_Tab] = ImVec4(0.23f, 0.13f, 0.40f, 0.86f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.45f, 0.23f, 0.86f, 0.80f);
            colors[ImGuiCol_TabActive] = ImVec4(0.30f, 0.17f, 0.76f, 1.00f);
            colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
            colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.26f, 0.42f, 1.00f);
            colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
            colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
            colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
            colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
            colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
            colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
            colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
            colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
            colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
            colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
            colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
            colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
            colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
            colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
        }

        return InitializeResult::Ok;
    }

    enum class [[nodiscard]] PrepareResult : std::uint8_t {
        Ok,
        ShaderCompileError,
        ProgramLinkError,
        FramebufferIncomplete,
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
                case GL_DEBUG_TYPE_PUSH_GROUP:
                case GL_DEBUG_TYPE_POP_GROUP:
                case GL_DEBUG_TYPE_OTHER:
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
        glEnable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glEnable(GL_LINE_SMOOTH);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

        // Create the Debug shaders and program.
        GLuint debugVS = CreateShaderFromPath(GL_VERTEX_SHADER, "shaders/debug/DebugVS.glsl").value_or(0);
        GLuint debugFS = CreateShaderFromPath(GL_FRAGMENT_SHADER, "shaders/debug/DebugFS.glsl").value_or(0);
        if (!debugVS || !debugFS) {
            return PrepareResult::ShaderCompileError;
        }

        GLuint debugProgram = LinkProgram(debugVS, debugFS, "Debug Program").value_or(0);
        if (!debugProgram) {
            return PrepareResult::ProgramLinkError;
        }

        m_debugProgram = debugProgram;

        {
            // Create Debug VAO.
            GLuint vao = 0;
            glCreateVertexArrays(1, &vao);
            glObjectLabel(GL_VERTEX_ARRAY, vao, -1, "Debug VAO");

            // Declare the Position Attribute.
            glEnableVertexArrayAttrib(vao, 0);
            glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(DebugVertex, x));
            glVertexArrayAttribBinding(vao, 0, 0);

            m_debugVAO = vao;
        }

        // Create the Main shaders and program.
        GLuint mainVS = CreateShaderFromPath(GL_VERTEX_SHADER, "shaders/MainVS.glsl").value_or(0);
        GLuint mainFS = CreateShaderFromPath(GL_FRAGMENT_SHADER, "shaders/MainFS.glsl").value_or(0);
        if (!mainVS || !mainFS) {
            return PrepareResult::ShaderCompileError;
        }

        GLuint mainProgram = LinkProgram(mainVS, mainFS, "Main Program").value_or(0);
        if (!mainProgram) {
            return PrepareResult::ProgramLinkError;
        }

        m_mainProgram = mainProgram;

        // Create the Post-Processing shaders and program.
        GLuint ppfxVS = CreateShaderFromPath(GL_VERTEX_SHADER, "shaders/ppfx/PpfxVS.glsl").value_or(0);
        GLuint ppfxFS = CreateShaderFromPath(GL_FRAGMENT_SHADER, "shaders/ppfx/PpfxFS.glsl").value_or(0);
        if (!ppfxVS || !ppfxFS) {
            return PrepareResult::ShaderCompileError;
        }

        GLuint ppfxProgram = LinkProgram(ppfxVS, ppfxFS, "Post-Processing Program").value_or(0);
        if (!ppfxProgram) {
            return PrepareResult::ProgramLinkError;
        }

        m_ppfxProgram = ppfxProgram;

        {
            // Create Post-Processing VAO
            GLuint vao = 0;
            glCreateVertexArrays(1, &vao);
            glObjectLabel(GL_VERTEX_ARRAY, vao, -1, "Post-Processing VAO");

            // Declare the Position attribute
            glEnableVertexArrayAttrib(vao, 0);
            glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(DebugVertex, x));
            glVertexArrayAttribBinding(vao, 0, 0);

            // Declare the UV Attribute.
            glEnableVertexArrayAttrib(vao, 1);
            glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, offsetof(MeshVertex, u));
            glVertexArrayAttribBinding(vao, 1, 0);

            m_ppfxVAO = vao;

            // Create Post-Processing VBO
            GLuint vbo = 0;
            glCreateBuffers(1, &vbo);

            std::array ppfxQuad = std::to_array<PpfxVertex>({{.x = -1.0f, .y = -1.0f, .z = 0.0f, .u = 0.0f, .v = 0.0f},
                {.x = 1.0f, .y = -1.0f, .z = 0.0f, .u = 1.0f, .v = 0.0f}, {.x = -1.0f, .y = 1.0f, .z = 0.0f, .u = 0.0f, .v = 1.0f},
                {.x = 1.0f, .y = 1.0f, .z = 0.0f, .u = 1.0f, .v = 1.0f}});

            glNamedBufferStorage(vbo, static_cast<GLsizeiptr>(sizeof(PpfxVertex) * std::size(ppfxQuad)), ppfxQuad.data(), 0);
            glObjectLabel(GL_BUFFER, vbo, -1, "Post-Processing VBO");

            // Attach the VBO to the VAO.
            glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(PpfxVertex));
        }

        // glTF mesh!
        std::array meshPaths(std::to_array<const char*>({"meshes/teapot.glb"}));
        for (auto& path : meshPaths) {
            cgltf_options options {};
            cgltf_data* data = nullptr;
            cgltf_result result = cgltf_parse_file(&options, path, &data);
            cgltf_load_buffers(&options, data, path);

            // Build the VBO for the loaded mesh.
            struct GltfPrimitive {
                std::vector<MeshVertex> m_vertexData;
                std::vector<uint32_t> m_vertexIndices;
            };

            struct GltfMesh {
                std::vector<GltfPrimitive> m_primitives;
                AABB m_aabb;
            };

            Mesh glitterMesh {};
            std::vector<GltfMesh> parsedMeshes;
            if (result == cgltf_result_success) {
                // Iterate through each meshes, then through its primitives and their attributes, creating one VBO per primitive and
                // filling it with data pointed by the attribute buffer views. A mesh can have several primitives.
                for (cgltf_size meshIdx = 0; meshIdx < data->meshes_count; meshIdx++) {
                    cgltf_mesh mesh = data->meshes[meshIdx];

                    GltfMesh gltfMesh {};
                    for (cgltf_size primIdx = 0; primIdx < mesh.primitives_count; primIdx++) {
                        cgltf_primitive prim = mesh.primitives[primIdx];

                        cgltf_size vertexCount {0};
                        // Fill out the accessor pointers.
                        cgltf_accessor* positionAccessor = nullptr;
                        cgltf_accessor* texCoordAccessor = nullptr;
                        cgltf_accessor* normalAccessor = nullptr;
                        for (cgltf_size attribIdx = 0; attribIdx < prim.attributes_count; attribIdx++) {
                            cgltf_attribute attrib = prim.attributes[attribIdx];

                            switch (attrib.type) {
                            case cgltf_attribute_type_position:
                                vertexCount = attrib.data->count;
                                if (attrib.data->component_type == cgltf_component_type_r_32f) {
                                    positionAccessor = attrib.data;
                                }
                                break;
                            case cgltf_attribute_type_texcoord:
                                if (attrib.data->component_type == cgltf_component_type_r_32f) {
                                    texCoordAccessor = attrib.data;
                                }
                                break;
                            case cgltf_attribute_type_normal:
                                if (attrib.data->component_type == cgltf_component_type_r_32f) {
                                    normalAccessor = attrib.data;
                                }
                                break;
                            default:
                                break;
                            }
                        }

                        GltfPrimitive gltfPrim {};
                        for (cgltf_size vertexIdx = 0; vertexIdx < vertexCount; vertexIdx++) {
                            MeshVertex vertex {};

                            if (positionAccessor) {
                                cgltf_accessor_read_float(positionAccessor, vertexIdx, &vertex.x, 3);

                                // Calculate the AABB.
                                auto& aabb = gltfMesh.m_aabb;
                                aabb.m_localMin = glm::vec3 {std::min(aabb.m_localMin.x, vertex.x),
                                    std::min(aabb.m_localMin.y, vertex.y), std::min(aabb.m_localMin.z, vertex.z)};
                                aabb.m_localMax = glm::vec3 {std::max(aabb.m_localMax.x, vertex.x),
                                    std::max(aabb.m_localMax.y, vertex.y), std::max(aabb.m_localMax.z, vertex.z)};
                            }
                            if (texCoordAccessor) {
                                cgltf_accessor_read_float(texCoordAccessor, vertexIdx, &vertex.u, 2);
                            }
                            if (normalAccessor) {
                                cgltf_accessor_read_float(normalAccessor, vertexIdx, &vertex.nx, 3);
                            }

                            gltfPrim.m_vertexData.emplace_back(vertex);
                        }

                        for (cgltf_size indexIdx = 0; indexIdx < prim.indices->count; indexIdx++) {
                            gltfPrim.m_vertexIndices.emplace_back(
                                static_cast<GLuint>(cgltf_accessor_read_index(prim.indices, indexIdx)));
                        }

                        gltfMesh.m_primitives.emplace_back(gltfPrim);
                    } // Iterating through the primitives.

                    parsedMeshes.emplace_back(gltfMesh);
                } // Iterating through the meshes.

                cgltf_free(data);

                for (auto& primitives : parsedMeshes[0].m_primitives) {
                    Primitive primitive {.m_vbo = 0,
                        .m_ebo = 0,
                        .m_baseTexture = 0,
                        .m_elementCount = narrow_into<GLsizei>(primitives.m_vertexIndices.size())};

                    // Create VBO.
                    GLuint vbo = 0;
                    glCreateBuffers(1, &vbo);
                    glNamedBufferStorage(vbo,
                        static_cast<GLsizeiptr>(sizeof(MeshVertex) * parsedMeshes[0].m_primitives[0].m_vertexData.size()),
                        parsedMeshes[0].m_primitives[0].m_vertexData.data(), 0);
                    glObjectLabel(GL_BUFFER, vbo, -1, "VBO");
                    primitive.m_vbo = vbo;

                    // Create EBO.
                    GLuint ebo = 0;
                    glCreateBuffers(1, &ebo);
                    glNamedBufferStorage(ebo,
                        static_cast<GLsizeiptr>(sizeof(uint32_t) * parsedMeshes[0].m_primitives[0].m_vertexIndices.size()),
                        parsedMeshes[0].m_primitives[0].m_vertexIndices.data(), 0);
                    glObjectLabel(GL_BUFFER, ebo, -1, "EBO");
                    primitive.m_ebo = ebo;

                    // Add primitive to the Mesh.
                    glitterMesh.m_primitives.emplace_back(primitive);
                }

                glitterMesh.m_aabb = parsedMeshes[0].m_aabb;

                m_meshes.emplace_back(glitterMesh);
            }
        }

        // Create VAO.
        GLuint vao = 0;
        glCreateVertexArrays(1, &vao);
        glObjectLabel(GL_VERTEX_ARRAY, vao, -1, "Main VAO");

        // Declare the Position Attribute.
        glEnableVertexArrayAttrib(vao, 0);
        glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(MeshVertex, x));
        glVertexArrayAttribBinding(vao, 0, 0);

        // Declare the UV Attribute.
        glEnableVertexArrayAttrib(vao, 1);
        glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, offsetof(MeshVertex, u));
        glVertexArrayAttribBinding(vao, 1, 0);

        // Declare the Normal attribute
        glEnableVertexArrayAttrib(vao, 2);
        glVertexArrayAttribFormat(vao, 2, 3, GL_FLOAT, GL_FALSE, offsetof(MeshVertex, nx));
        glVertexArrayAttribBinding(vao, 2, 0);

        m_mainVAO = vao;

        // Create empty UBO buffer.
        GLuint ubo {};
        glCreateBuffers(1, &ubo);
        glObjectLabel(GL_BUFFER, ubo, -1, "UBO");

        // Just enough for the Common stuff and the Nodes.
        glNamedBufferData(ubo,
            static_cast<GLsizeiptr>(
                sizeof(CommonData) + ((sizeof(PerDrawData) + m_uboAllocator.GetAlignment())) * Glitter::Config::MAX_NODES),
            nullptr, GL_DYNAMIC_DRAW);
        m_mainUBO = ubo;

        // Load some Node textures.
        std::array texturePaths(std::to_array<const char*>({"textures/Tile.png", "textures/Cobble.png"}));

        for (auto& path : texturePaths) {
            GLuint texture {};
            glCreateTextures(GL_TEXTURE_2D, 1, &texture);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glObjectLabel(GL_TEXTURE, texture, -1, std::format("Texture <{}>", path).c_str());

            int width = 0, height = 0, nChannels = 0;
            unsigned char* textureData = stbi_load(path, &width, &height, &nChannels, 4);
            if (textureData) {
                glTextureStorage2D(texture, 1, GL_RGBA8, width, height);
                glTextureSubImage2D(texture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, textureData);
                glGenerateTextureMipmap(texture);
            }
            stbi_image_free(textureData);
            m_loadedTextures.push_back(texture);
        }

        // Create FBO to be used for post-processing effects.
        GLuint fbo = 0;
        glCreateFramebuffers(1, &fbo);

        // Create the color texture used with the FBO.
        GLuint fboColor = 0;
        glCreateTextures(GL_TEXTURE_2D, 1, &fboColor);
        glTextureStorage2D(fboColor, 1, GL_RGBA8, m_windowWidth, m_windowHeight);
        glObjectLabel(GL_TEXTURE, fboColor, -1, "Post-Processing FBO Color Texture");

        // Create the depth renderbuffer (note: can't be sampled) used with the FBO.
        GLuint fboDepth = 0;
        glCreateRenderbuffers(1, &fboDepth);
        glNamedRenderbufferStorage(fboDepth, GL_DEPTH_COMPONENT24, m_windowWidth, m_windowHeight);
        glObjectLabel(GL_RENDERBUFFER, fboDepth, -1, "Post-Processing FBO Depth Renderbuffer");

        // Attach the textures to the FBO.
        glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, fboColor, 0);
        glNamedFramebufferRenderbuffer(fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);

        if (glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            return PrepareResult::FramebufferIncomplete;
        }

        m_fbo = fbo;
        m_fboColor = fboColor;
        m_fboDepth = fboDepth;

        return PrepareResult::Ok;
    }

    void Tick()
    {
        glfwPollEvents();

        // Clear Debug data.
        m_debugData.Clear();

        // Start Dear ImGui frame.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        for (Node& node : m_nodes) {
            if (node.m_shouldAnimate) {
                node.m_opacity = std::clamp(std::abs(1.25f * std::cosf(static_cast<float>(glfwGetTime()))), 0.0f, 1.0f);
            }
        }
    }

    void Render()
    {
        // Note: glClear() respects depth-write, therefore depth-write must be enabled to clear the depth buffer.
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Clear the UBO CPU-backing buffer.
        m_uboAllocator.Clear();

        // Calculate View and Projection.
        glm::vec3 eyePos = glm::vec3(std::sin(glfwGetTime()), 2.5f, -3.5f);
        glm::mat4 view = glm::lookAt(eyePos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f), static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight), 1.0f, 20.0f);
        m_currentView = view;
        m_currentProjection = projection;

        // Extract the frustum planes using the VP matrix.
        // By using the combined View and Projection matrices, we should obtain the clipping planes in World Space.
        // Proj: (View Space)  -> (Clip Space);
        // View: (World Space) -> (View Space);
        //   VP: (World Space) -> (Clip Space).
        struct Plane {
            float a, b, c, d;
        };
        std::array<Plane, 6> frustumPlanes {};
        glm::mat4 vp = projection * view;
        {
            Plane left {.a = vp[0][3] + vp[0][0], .b = vp[1][3] + vp[1][0], .c = vp[2][3] + vp[2][0], .d = vp[3][3] + vp[3][0]};
            Plane right {.a = vp[0][3] - vp[0][0], .b = vp[1][3] - vp[1][0], .c = vp[2][3] - vp[2][0], .d = vp[3][3] - vp[3][0]};

            Plane bottom {.a = vp[0][3] + vp[0][1], .b = vp[1][3] + vp[1][1], .c = vp[2][3] + vp[2][1], .d = vp[3][3] + vp[3][1]};
            Plane top {.a = vp[0][3] - vp[0][1], .b = vp[1][3] - vp[1][1], .c = vp[2][3] - vp[2][1], .d = vp[3][3] - vp[3][1]};

            Plane near {.a = vp[0][3] + vp[0][2], .b = vp[1][3] + vp[1][2], .c = vp[2][3] + vp[2][2], .d = vp[3][3] + vp[3][2]};
            Plane far {.a = vp[0][3] - vp[0][2], .b = vp[1][3] - vp[1][2], .c = vp[2][3] - vp[2][2], .d = vp[3][3] - vp[3][2]};

            frustumPlanes[0] = left;
            frustumPlanes[1] = right;
            frustumPlanes[2] = bottom;
            frustumPlanes[3] = top;
            frustumPlanes[4] = near;
            frustumPlanes[5] = far;
        }

        // Write the CommonData into the UBO-backing CPU buffer.
        CommonData commonData = {.m_view = view,
            .m_projection = projection,
            .m_eyePos = glm::vec4(eyePos, 1.0),
            .m_lightPos = glm::vec4(1.0, 0.5, -0.5, 1.0),
            .m_lightColor = glm::vec4(1.0, 1.0, 1.0, 1.0)};
        m_uboAllocator.Push(commonData);

        // Write each Node's PerDrawData into the buffer.
        int numCulledNodes = 0;
        for (auto& node : m_nodes) {
            // Don't bother writing data for a totally transparent Node.
            if (node.m_opacity == 0.0f) {
                continue;
            }

            if (m_frustumCulling) {
                // Check if a `vec3` point is in the inside halfspace of a plane, for culling purposes.
                auto isInsideHalfspace = [](glm::vec3& position, Plane& plane) {
                    float d = (plane.a * position.x) + (plane.b * position.y) + (plane.c * position.z) + plane.d;

                    // Inside halfspace.
                    return d > 0;
                };

                // Obtain the AABB's scaled and translated transform.
                auto aabbTransform = glm::mat4(1.0f);
                aabbTransform = glm::scale(aabbTransform, node.m_scale);
                aabbTransform = glm::translate(aabbTransform, node.m_position);

                AABB aabb = m_meshes[node.m_meshID].m_aabb;
                std::array aabbCorners = std::to_array({
                    /* 0 */ glm::vec3 {aabb.m_localMin},
                    /* 1 */ glm::vec3 {aabb.m_localMax.x, aabb.m_localMin.y, aabb.m_localMin.z},
                    /* 2 */ glm::vec3 {aabb.m_localMin.x, aabb.m_localMax.y, aabb.m_localMin.z},
                    /* 3 */ glm::vec3 {aabb.m_localMin.x, aabb.m_localMin.y, aabb.m_localMax.z},
                    /* 4 */ glm::vec3 {aabb.m_localMax.x, aabb.m_localMin.y, aabb.m_localMax.z},
                    /* 5 */ glm::vec3 {aabb.m_localMax.x, aabb.m_localMax.y, aabb.m_localMin.z},
                    /* 6 */ glm::vec3 {aabb.m_localMin.x, aabb.m_localMax.y, aabb.m_localMax.z},
                    /* 7 */ glm::vec3 {aabb.m_localMax},
                });

                // Transform the corners in `aabbCorners` into world space.
                for (auto& corner : aabbCorners) {
                    corner = glm::vec3(aabbTransform * glm::vec4(corner, 1.0f));
                }

                // Draw each AABB's lines using PushDebugLine.
                if (m_drawAABBs) {
                    m_debugData.PushDebugLine(aabbCorners[0], aabbCorners[1]);
                    m_debugData.PushDebugLine(aabbCorners[0], aabbCorners[2]);
                    m_debugData.PushDebugLine(aabbCorners[0], aabbCorners[3]);
                    m_debugData.PushDebugLine(aabbCorners[1], aabbCorners[4]);
                    m_debugData.PushDebugLine(aabbCorners[1], aabbCorners[5]);
                    m_debugData.PushDebugLine(aabbCorners[2], aabbCorners[5]);
                    m_debugData.PushDebugLine(aabbCorners[2], aabbCorners[6]);
                    m_debugData.PushDebugLine(aabbCorners[3], aabbCorners[4]);
                    m_debugData.PushDebugLine(aabbCorners[3], aabbCorners[6]);
                    m_debugData.PushDebugLine(aabbCorners[4], aabbCorners[7]);
                    m_debugData.PushDebugLine(aabbCorners[5], aabbCorners[7]);
                    m_debugData.PushDebugLine(aabbCorners[7], aabbCorners[6]);
                }

                // Check if any corners of the AABB are inside one of the viewing frustums. If so, don't cull that Node.
                bool cullNode = true;
                for (auto& corner : aabbCorners) {
                    bool insideLeft = isInsideHalfspace(corner, frustumPlanes[0]);
                    bool insideRight = isInsideHalfspace(corner, frustumPlanes[1]);
                    bool insideBottom = isInsideHalfspace(corner, frustumPlanes[2]);
                    bool insideTop = isInsideHalfspace(corner, frustumPlanes[3]);
                    bool insideNear = isInsideHalfspace(corner, frustumPlanes[4]);
                    bool insideFar = isInsideHalfspace(corner, frustumPlanes[5]);

                    if (insideLeft && insideRight && insideBottom && insideTop && insideNear && insideFar) {
                        cullNode = false;
                        break;
                    }
                }
                if (cullNode) {
                    numCulledNodes += 1;
                }
                node.m_culled = cullNode;
            }

            // The Model has to follow the Scale-Rotate-Translate
            // order.
            auto model = glm::mat4(1.0f);
            model = glm::scale(model, node.m_scale);
            model = glm::translate(model, node.m_position);

            PerDrawData shaderData {.m_model = model, .m_opacity = node.m_opacity};
            node.m_uboOffset = m_uboAllocator.Push(shaderData);
        }

        // Add Debug UI.
        ImGui::Begin("Glitter Debug");
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Frustum Culling", &m_frustumCulling);
            ImGui::Text("Culled Nodes: %d/%zu (%.2f%%)", numCulledNodes, m_nodes.size(),
                !m_nodes.empty() ? static_cast<float>(numCulledNodes) / static_cast<float>(m_nodes.size()) * 100.0f : 0.0f);
            if (ImGui::Button("Clear Nodes", ImVec2(-1.0f, 0.0f))) {
                m_nodes.clear();
            }
        }
        if (ImGui::CollapsingHeader("Debug View", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Debug Lines", &m_debugLines);
            ImGui::SameLine();
            ImGui::Checkbox("Draw AABBs", &m_drawAABBs);
        }
        ImGui::SeparatorText("Scene Properties");
        ImGui::SliderFloat("Scene Gamma", &m_sceneGamma, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::End();

        ImGui::Begin("Glitter Framebuffers");
        if (ImGui::CollapsingHeader("Main FB", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Image(m_fboColor, ImGui::GetWindowSize(), ImVec2(0, 1), ImVec2(1, 0));
        }
        ImGui::End();

        // Upload the CPU-backing buffer into the UBO.
        glNamedBufferSubData(m_mainUBO, 0, static_cast<GLsizeiptr>(sizeof(uint8_t) * m_uboAllocator.Size()), m_uboAllocator.Data());

        // Bind the Program and VAO.
        glUseProgram(m_mainProgram);
        glBindVertexArray(m_mainVAO);

        // Split Node elements between opaque and transparent.
        std::vector<Node> opaqueNodes {};
        std::vector<Node> transparentNodes {};
        for (Node& node : m_nodes) {
            if (m_frustumCulling) {
                if (node.m_culled) {
                    continue;
                }
            }

            if (node.m_opacity == 1.0f) {
                opaqueNodes.emplace_back(node);
            } else if (node.m_opacity != 0.0f) {
                transparentNodes.emplace_back(node);
            } else {
                // A totally transparent Node (opacity = 0.0f).
                continue;
            }
        }

        // Sort each opaque Node from front-to-back.
        std::sort(opaqueNodes.begin(), opaqueNodes.end(), [&eyePos](const Node& a, const Node& b) {
            return glm::distance(eyePos, a.m_position) < glm::distance(eyePos, b.m_position);
        });

        // Sort each transparent Node from back-to-front.
        std::sort(transparentNodes.begin(), transparentNodes.end(), [&eyePos](const Node& a, const Node& b) {
            return glm::distance(eyePos, a.m_position) > glm::distance(eyePos, b.m_position);
        });

        auto renderNodes = [this](const std::vector<Node>& nodes) {
            for (const Node& node : nodes) {
                size_t meshIdx = node.m_meshID;

                for (const auto& primitive : m_meshes[meshIdx].m_primitives) {
                    // Attach the VBO to the VAO.
                    glVertexArrayVertexBuffer(m_mainVAO, 0, primitive.m_vbo, 0, sizeof(MeshVertex));

                    // Attach the EBO to the VAO.
                    glVertexArrayElementBuffer(m_mainVAO, primitive.m_ebo);

                    // Bind the Common UBO data into the first slot of the UBO.
                    glBindBufferRange(GL_UNIFORM_BUFFER, 0, m_mainUBO, 0, sizeof(CommonData));

                    // Bind the Per-Draw UBO data into the second slot of the UBO.
                    glBindBufferRange(
                        GL_UNIFORM_BUFFER, 1, m_mainUBO, static_cast<GLintptr>(node.m_uboOffset), sizeof(PerDrawData));

                    // Bind the texture.
                    glBindTextureUnit(0, node.m_texture);

                    // Draw the Primitive!
                    glDrawElements(GL_TRIANGLES, primitive.m_elementCount, GL_UNSIGNED_INT, nullptr);
                }
            }
        };

        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Main FB Draw");
        {
            glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
            // The FBO needs its own independent clear.
            glDepthMask(GL_TRUE);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Render each opaque Node.
            if (!opaqueNodes.empty()) {
                glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 1, -1, "Opaque Nodes");
                {
                    glDepthMask(GL_TRUE);
                    renderNodes(opaqueNodes);
                }
                glPopDebugGroup();
            }

            // Render each transparent Node.
            if (!transparentNodes.empty()) {
                glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 2, -1, "Transparent Nodes");
                {
                    glDepthMask(GL_FALSE);
                    renderNodes(transparentNodes);
                }
                glPopDebugGroup();
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        glPopDebugGroup();

        // Render Post-Processing effects.
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Post-Processing");
        {
            glUseProgram(m_ppfxProgram);
            glBindVertexArray(m_ppfxVAO);

            // uniform layout(location = 0) sampler2D u_ColorTexture;
            // uniform layout(location = 1) float u_Gamma;
            glBindTextureUnit(0, m_fboColor);
            glUniform1f(1, m_sceneGamma);

            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
        glPopDebugGroup();

        // Render Debug.
        if (m_debugLines && !m_debugData.m_debugLines.empty()) {
            glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 2, -1, "Debug");
            {
                glDepthFunc(GL_ALWAYS);

                // Bind the Program and VAO.
                glUseProgram(m_debugProgram);
                glBindVertexArray(m_debugVAO);

                // Create VBO.
                GLuint vbo = 0;
                glCreateBuffers(1, &vbo);
                glNamedBufferStorage(vbo, static_cast<GLsizeiptr>(sizeof(DebugVertex) * m_debugData.m_debugLines.size()),
                    m_debugData.m_debugLines.data(), 0);
                glObjectLabel(GL_BUFFER, vbo, -1, "Debug VBO");

                // Attach the VBO to the VAO.
                glVertexArrayVertexBuffer(m_debugVAO, 0, vbo, 0, sizeof(DebugVertex));

                // Bind the Common UBO data into the first slot of the UBO.
                glBindBufferRange(GL_UNIFORM_BUFFER, 0, m_mainUBO, 0, sizeof(CommonData));

                // Draw the Primitive!
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_debugData.m_debugLines.size()));

                glDepthFunc(GL_LEQUAL);
            }
            glPopDebugGroup();
        }

        // Render Dear ImGui.
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 3, -1, "Dear ImGui");
        {
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        glPopDebugGroup();

        glfwSwapBuffers(m_window);
    }

    void Finish()
    {
        spdlog::info("Stopping...");

        // Shutdown Dear ImGui.
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        // Shutdown OpenGL.
        glDeleteProgram(m_mainProgram);
        glDeleteBuffers(1, &m_mainVAO);
        glDeleteBuffers(1, &m_mainUBO);

        glDeleteProgram(m_debugProgram);
        glDeleteBuffers(1, &m_debugVAO);

        glDeleteProgram(m_ppfxProgram);
        glDeleteBuffers(1, &m_ppfxVAO);

        glDeleteFramebuffers(1, &m_fbo);
        glDeleteTextures(1, &m_fboColor);
        glDeleteRenderbuffers(1, &m_fboDepth);

        glDeleteTextures(m_loadedTextures.size(), m_loadedTextures.data());

        // Shutdown GLFW.
        glfwTerminate();
    }

    GLFWwindow* m_window {};

    GLuint m_mainProgram {};
    GLuint m_mainVAO {};
    GLuint m_mainUBO {};

    GLuint m_debugProgram {};
    GLuint m_debugVAO {};

    struct PpfxVertex {
        float x, y, z;
        float u, v;
    };

    GLuint m_ppfxProgram {};
    GLuint m_ppfxVAO {};

    GLuint m_fbo {};
    GLuint m_fboColor {};
    GLuint m_fboDepth {};

    struct DebugVertex {
        float x, y, z;
    };

    struct {
        std::vector<DebugVertex> m_debugLines;

        void PushDebugLine(glm::vec3 a, glm::vec3 b)
        {
            m_debugLines.emplace_back(DebugVertex {.x = a.x, .y = a.y, .z = a.z});
            m_debugLines.emplace_back(DebugVertex {.x = b.x, .y = b.y, .z = b.z});
        }

        void Clear() { m_debugLines.clear(); }
    } m_debugData;

    int m_windowWidth {1366};
    int m_windowHeight {768};

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
        float m_opacity;
    };
    struct ShaderData {
        CommonData m_commonData;
        PerDrawData m_perDrawData;
    };
    LinearAllocator m_uboAllocator;

    std::vector<GLuint> m_loadedTextures;

    struct Node {
        glm::vec3 m_position;
        glm::vec3 m_scale;

        size_t m_meshID;
        size_t m_uboOffset;

        GLuint m_texture;
        float m_opacity;

        bool m_shouldAnimate;
        bool m_culled;
    };
    std::vector<Node> m_nodes;

    std::vector<Mesh> m_meshes;

    bool m_frustumCulling {true};
    bool m_debugLines {true};
    bool m_drawAABBs {false};

    float m_sceneGamma {1.0f};
};

int main()
{
    GlitterApplication glitterApp;
    glitterApp.Run();
    return 0;
}