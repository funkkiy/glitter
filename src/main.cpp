#include "glitter/Config.h"
#include "glitter/ImGuiConfig.h"
#include "glitter/gfx/LinearAllocator.h"
#include "glitter/gfx/VAO.h"
#include "glitter/systems/Camera.h"
#include "glitter/util/Common.h"
#include "glitter/util/Debug.h"
#include "glitter/util/File.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <cgltf.h>
#include <stb_image.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "glitter/util/ImGui.h"

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

#ifdef GLITTER_WITH_LIVEPP
#include <LPP_API_x64_CPP.h>
#endif

namespace Glitter {

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

class Application {
public:
    void Run()
    {
        spdlog::info("Started Glitter.");

#ifdef GLITTER_WITH_LIVEPP
        // Start LivePP agent.
        lpp::LppSynchronizedAgent lppAgent = lpp::LppCreateSynchronizedAgent(nullptr, L"..\\..\\glitter\\vendor\\livepp");
        if (!lpp::LppIsValidSynchronizedAgent(&lppAgent)) {
            spdlog::error("LivePP initialization failed!");
            Finish();
            return;
        }
        lppAgent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_ALL_IMPORT_MODULES, nullptr, nullptr);
#endif

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
#ifdef GLITTER_WITH_LIVEPP
            if (lppAgent.WantsReload(lpp::LPP_RELOAD_OPTION_SYNCHRONIZE_WITH_RELOAD)) {
                lppAgent.Reload(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);
            }

            if (lppAgent.WantsRestart()) {
                lppAgent.Restart(lpp::LPP_RESTART_BEHAVIOUR_INSTANT_TERMINATION, 0, nullptr);
            }
#endif

            double currentTick = glfwGetTime();
            double dt = currentTick - m_lastTick;

            Tick(dt);
            Render();

            m_lastTick = currentTick;
        }

#ifdef GLITTER_WITH_LIVEPP
        // Stop LivePP agent.
        lpp::LppDestroySynchronizedAgent(&lppAgent);
#endif

        Finish();
    }

private:
    void CreateFramebuffer(GLsizei width, GLsizei height)
    {
        // Create new FBO.
        if (!m_fbo) {
            GLuint fbo = 0;
            glCreateFramebuffers(1, &fbo);
            m_fbo = fbo;
        }

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
        glNamedFramebufferTexture(*m_fbo, GL_COLOR_ATTACHMENT0, fboColor, 0);
        glNamedFramebufferRenderbuffer(*m_fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);

        m_fboColor = fboColor;
        m_fboDepth = fboDepth;
    }

    void UpdateFramebuffer(GLsizei width, GLsizei height)
    {
        GLuint oldColor = m_fboColor;
        GLuint oldDepth = m_fboDepth;

        CreateFramebuffer(width, height);

        glDeleteTextures(1, &oldColor);
        glDeleteRenderbuffers(1, &oldDepth);
    }

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
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));

            if (width == 0 || height == 0) {
                return;
            }

            app->m_windowWidth = width;
            app->m_windowHeight = height;

            app->UpdateFramebuffer(width, height);
            glViewport(0, 0, width, height);
        });

        glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
            switch (key) {
            case GLFW_KEY_W:
            case GLFW_KEY_A:
            case GLFW_KEY_S:
            case GLFW_KEY_D:
            case GLFW_KEY_Q:
            case GLFW_KEY_E:
                app->m_currentCamera.ProcessKeys(key, action);
                break;
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
                        app->m_nodes.push_back(Node {.m_position = glm::sphericalRand(15.0f),
                            .m_scale = glm::vec3(0.5f),
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

        glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        glfwSetCursorPosCallback(m_window, [](GLFWwindow* window, double x, double y) {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
            app->m_currentCamera.ProcessMouse(x, y);
        });

        glfwSetMouseButtonCallback(m_window, [](GLFWwindow* window, int button, int action, int mods) {
            auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
            app->m_currentCamera.ProcessMouseButton(button, action, mods);
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
        Glitter::Util::ImGui::InstallTheme(ImGui::GetStyle().Colors);

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

        m_debugVAO = Glitter::Gfx::CreateVAO("Debug VAO",
            {
                {.m_size = 3, .m_type = GL_FLOAT, .m_offset = offsetof(DebugVertex, x)},
        });

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

        {
            m_mainVAO = Glitter::Gfx::CreateVAO("Main VAO",
                {
                    {.m_size = 3, .m_type = GL_FLOAT, .m_offset = offsetof(MeshVertex, x) },
                    {.m_size = 2, .m_type = GL_FLOAT, .m_offset = offsetof(MeshVertex, u) },
                    {.m_size = 3, .m_type = GL_FLOAT, .m_offset = offsetof(MeshVertex, nx)}
            });

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
        }

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

        // Load some Node textures.
        std::array texturePaths(std::to_array<const char*>({"textures/Froge.png", "textures/Tile.png"}));

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

        // Create ground plane.
        {
            std::array planeVerts(std::to_array<MeshVertex>({
                {.x = 0.0f, .y = 0.0f, .z = 0.0f, .u = 0.0f, .v = 0.0f, .nx = 0.0f, .ny = 1.0f, .nz = 0.0f},
                {.x = 0.0f, .y = 0.0f, .z = 1.0f, .u = 0.0f, .v = 1.0f, .nx = 0.0f, .ny = 1.0f, .nz = 0.0f},
                {.x = 1.0f, .y = 0.0f, .z = 1.0f, .u = 1.0f, .v = 1.0f, .nx = 0.0f, .ny = 1.0f, .nz = 0.0f},
                {.x = 1.0f, .y = 0.0f, .z = 0.0f, .u = 1.0f, .v = 0.0f, .nx = 0.0f, .ny = 1.0f, .nz = 0.0f},
            }));
            std::array planeIndices(std::to_array<uint32_t>({0, 1, 2, 2, 3, 0}));

            // Create VBO.
            GLuint vbo = 0;
            glCreateBuffers(1, &vbo);
            glNamedBufferStorage(vbo, static_cast<GLsizeiptr>(sizeof(MeshVertex) * planeVerts.size()), planeVerts.data(), 0);
            glObjectLabel(GL_BUFFER, vbo, -1, "VBO");

            // Create EBO.
            GLuint ebo = 0;
            glCreateBuffers(1, &ebo);
            glNamedBufferStorage(ebo, static_cast<GLsizeiptr>(sizeof(uint32_t) * planeIndices.size()), planeIndices.data(), 0);
            glObjectLabel(GL_BUFFER, ebo, -1, "EBO");

            Primitive prim {
                .m_vbo = vbo, .m_ebo = ebo, .m_baseTexture = 0, .m_elementCount = narrow_into<GLsizei>(planeIndices.size())};

            Mesh mesh {
                .m_primitives = {prim},
                  .m_aabb = {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 1.0f)}
            };

            Node node {.m_position = glm::vec3(-15.0f, -15.0f, -15.0f),
                .m_scale = glm::vec3(35.0f),
                .m_meshID = m_meshes.size(),
                .m_uboOffset = 0,
                .m_texture = m_loadedTextures[0],
                .m_opacity = 1.0f,
                .m_shouldAnimate = false,
                .m_culled = false,
                .m_neverCull = true};

            m_meshes.emplace_back(mesh);
            m_nodes.emplace_back(node);
        }

        CreateFramebuffer(m_windowWidth, m_windowHeight);

        return PrepareResult::Ok;
    }

    void Tick(double dt)
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

        m_currentCamera.Tick(dt);
    }

    void Render()
    {
        // Note: glClear() respects depth-write, therefore depth-write must be enabled to clear the depth buffer.
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Clear the UBO CPU-backing buffer.
        m_uboAllocator.Clear();

        // Calculate View and Projection.
        glm::vec3 eyePos = m_currentCamera.m_position;
        glm::mat4 view = glm::lookAt(eyePos, eyePos + m_currentCamera.m_direction, m_currentCamera.m_up);
        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f), static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight), 1.0f, 200.0f);
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

        glm::vec4 pointLightPosition = glm::vec4(0.0f, std::sinf(static_cast<float>(glfwGetTime())) * 25.0f, 0.0f, 1.0f);
        m_debugData.PushDebugSphere(pointLightPosition, m_pointLightRadius);

        // Write the CommonData into the UBO-backing CPU buffer.
        CommonData commonData
            = {.m_view = view,
                  .m_projection = projection,
                  .m_eyePos = glm::vec4(m_currentCamera.m_position, 1.0f),
                  .m_dirLightDirection = glm::vec4(1.0f, 0.5f, -0.5f, 1.0f),
                  .m_dirLightColor = glm::vec4(0.3f, 0.0f, 0.5f, 1.0f),
                  .m_pointLightPosition = pointLightPosition,
                  .m_pointLightColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
                  .m_spotLightPosition = glm::vec4(0.0f),
                  .m_spotLightColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),
                  .m_spotLightDirection = glm::vec4(std::cosf(static_cast<float>(glfwGetTime())) * 25.0f,
                      std::sinf(static_cast<float>(glfwGetTime())) * 25.0f, 0.0f, 1.0f),
                  .m_pointLightRadius = m_pointLightRadius,
                  .m_spotLightAngleCos = std::cosf(glm::radians(m_spotLightAngle)),
                  .m_spotLightRange = m_spotLightRange,
                  .m_padding = 0.0f
        };
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
                aabbTransform = glm::translate(aabbTransform, node.m_position);
                aabbTransform = glm::scale(aabbTransform, node.m_scale);

                AABB aabb = m_meshes[node.m_meshID].m_aabb;
                std::array aabbCorners = std::to_array({
                    /* 0 */ glm::vec3 {aabb.m_localMin},
                    /* 1 */
                    glm::vec3 {aabb.m_localMax.x, aabb.m_localMin.y, aabb.m_localMin.z},
                    /* 2 */
                    glm::vec3 {aabb.m_localMin.x, aabb.m_localMax.y, aabb.m_localMin.z},
                    /* 3 */
                    glm::vec3 {aabb.m_localMin.x, aabb.m_localMin.y, aabb.m_localMax.z},
                    /* 4 */
                    glm::vec3 {aabb.m_localMax.x, aabb.m_localMin.y, aabb.m_localMax.z},
                    /* 5 */
                    glm::vec3 {aabb.m_localMax.x, aabb.m_localMax.y, aabb.m_localMin.z},
                    /* 6 */
                    glm::vec3 {aabb.m_localMin.x, aabb.m_localMax.y, aabb.m_localMax.z},
                    /* 7 */
                    glm::vec3 {aabb.m_localMax},
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

                if (!node.m_neverCull) {
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
                } else {
                    cullNode = false;
                }
                if (cullNode) {
                    numCulledNodes += 1;
                }
                node.m_culled = cullNode;
            }

            // The Model has to follow the Scale-Rotate-Translate
            // order.
            auto model = glm::mat4(1.0f);
            model = glm::translate(model, node.m_position);
            model = glm::scale(model, node.m_scale);

            PerDrawData shaderData {.m_model = model, .m_opacity = glm::vec4(node.m_opacity)};
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
        ImGui::SliderFloat("Point Light Radius", &m_pointLightRadius, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Spot Light Angle", &m_spotLightAngle, 0.0f, 90.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Spot Light Range", &m_spotLightRange, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
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
        std::vector<Node*> opaqueNodes {};
        std::vector<Node*> transparentNodes {};
        for (Node& node : m_nodes) {
            if (m_frustumCulling) {
                if (node.m_culled) {
                    continue;
                }
            }

            if (node.m_opacity == 1.0f) {
                opaqueNodes.emplace_back(&node);
            } else if (node.m_opacity != 0.0f) {
                transparentNodes.emplace_back(&node);
            } else {
                // A totally transparent Node (opacity = 0.0f).
                continue;
            }
        }

        // Sort each opaque Node from front-to-back.
        std::sort(opaqueNodes.begin(), opaqueNodes.end(), [&eyePos](const Node* a, const Node* b) {
            return glm::distance(eyePos, a->m_position) < glm::distance(eyePos, b->m_position);
        });

        // Sort each transparent Node from back-to-front.
        std::sort(transparentNodes.begin(), transparentNodes.end(), [&eyePos](const Node* a, const Node* b) {
            return glm::distance(eyePos, a->m_position) > glm::distance(eyePos, b->m_position);
        });

        auto renderNodes = [this](const std::vector<Node*>& nodes) {
            for (const Node* node : nodes) {
                size_t meshIdx = node->m_meshID;

                for (const auto& primitive : m_meshes[meshIdx].m_primitives) {
                    // Attach the VBO to the VAO.
                    glVertexArrayVertexBuffer(m_mainVAO, 0, primitive.m_vbo, 0, sizeof(MeshVertex));

                    // Attach the EBO to the VAO.
                    glVertexArrayElementBuffer(m_mainVAO, primitive.m_ebo);

                    // Bind the Common UBO data into the first slot of the UBO.
                    glBindBufferRange(GL_UNIFORM_BUFFER, 0, m_mainUBO, 0, sizeof(CommonData));

                    // Bind the Per-Draw UBO data into the second slot of the UBO.
                    glBindBufferRange(
                        GL_UNIFORM_BUFFER, 1, m_mainUBO, static_cast<GLintptr>(node->m_uboOffset), sizeof(PerDrawData));

                    // Bind the texture.
                    glBindTextureUnit(0, node->m_texture);

                    // Draw the Primitive!
                    glDrawElements(GL_TRIANGLES, primitive.m_elementCount, GL_UNSIGNED_INT, nullptr);
                }
            }
        };

        {
            GL_DEBUG_SCOPE("Main FB Draw");

            glBindFramebuffer(GL_FRAMEBUFFER, *m_fbo);
            // The FBO needs its own independent clear.
            glDepthMask(GL_TRUE);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Render each opaque Node.
            if (!opaqueNodes.empty()) {
                GL_DEBUG_SCOPE("Opaque Nodes");

                glDepthMask(GL_TRUE);
                renderNodes(opaqueNodes);
            }

            // Render each transparent Node.
            if (!transparentNodes.empty()) {
                GL_DEBUG_SCOPE("Transparent Nodes");

                glDepthMask(GL_FALSE);
                renderNodes(transparentNodes);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // Render Post-Processing effects.
        {
            GL_DEBUG_SCOPE("Post-Processing");

            glUseProgram(m_ppfxProgram);

            // uniform layout(location = 0) sampler2D u_ColorTexture;
            // uniform layout(location = 1) float u_Gamma;
            glBindTextureUnit(0, m_fboColor);
            glUniform1f(1, m_sceneGamma);

            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        // Render Debug.
        if (m_debugLines && !m_debugData.m_debugLines.empty()) {
            {
                GL_DEBUG_SCOPE("Debug");

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
        }

        // Render Dear ImGui.
        {
            GL_DEBUG_SCOPE("Dear ImGui");

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

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

        glDeleteFramebuffers(1, &(*m_fbo));
        glDeleteTextures(1, &m_fboColor);
        glDeleteRenderbuffers(1, &m_fboDepth);

        glDeleteTextures(narrow_into<GLsizei>(m_loadedTextures.size()), m_loadedTextures.data());

        // Shutdown GLFW.
        glfwTerminate();
    }

    GLFWwindow* m_window {};

    GLuint m_mainProgram {};
    GLuint m_mainVAO {};
    GLuint m_mainUBO {};

    GLuint m_debugProgram {};
    GLuint m_debugVAO {};

    GLuint m_ppfxProgram {};

    std::optional<GLuint> m_fbo {};
    GLuint m_fboColor {};
    GLuint m_fboDepth {};

    struct DebugVertex {
        float x, y, z;
    };

    struct {
        std::vector<DebugVertex> m_debugLines;

        void PushDebugLine(glm::vec3 a, glm::vec3 b)
        {
            m_debugLines.emplace_back(a.x, a.y, a.z);
            m_debugLines.emplace_back(b.x, b.y, b.z);
        }

        void PushDebugSphere(glm::vec3 position, float radius)
        {
            PushDebugLine(position - glm::vec3(radius, 0.0f, 0.0f), position + glm::vec3(radius, 0.0f, 0.0f));
            PushDebugLine(position - glm::vec3(0.0f, radius, 0.0f), position + glm::vec3(0.0f, radius, 0.0f));
            PushDebugLine(position - glm::vec3(0.0f, 0.0f, radius), position + glm::vec3(0.0f, 0.0f, radius));
        }

        void PushDebugCone(glm::vec3 position, glm::vec3 direction, float angle, float range)
        {
            direction = glm::normalize(direction);
            angle = glm::radians(angle);

            constexpr int segments = 8;
            constexpr float segmentAngle = 2 * glm::pi<float>() / segments;

            glm::vec2 circlePoints[segments];
            for (int i = 0; i < segments; i++) {
                circlePoints[i] = glm::vec2(std::cosf(segmentAngle * i), std::sinf(segmentAngle * i));
            }

            // Generate a matrix that converts from "Circle Space" to "World Space".
            glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(direction, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f) {
                up = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            glm::vec3 right = glm::normalize(glm::cross(direction, up));
            glm::vec3 forward = glm::normalize(glm::cross(right, direction));

            glm::vec3 circlePoints3D[segments];
            for (int i = 0; i < segments; i++) {
                circlePoints3D[i] = circlePoints[i].x * right + circlePoints[i].y * forward;
            }

            glm::vec3 circleCenter = position + direction * range;
            float circleRadius = std::tanf(angle) * range;
            for (int i = 0; i < segments; i++) {
                PushDebugLine(circleCenter + circleRadius * circlePoints3D[i],
                    circleCenter + circleRadius * circlePoints3D[(i + 1) % segments]);
                PushDebugLine(position, circleCenter + circleRadius * circlePoints3D[i]);
            }
        }

        void Clear() { m_debugLines.clear(); }
    } m_debugData;

    int m_windowWidth {1366};
    int m_windowHeight {768};

    glm::mat4 m_currentView {};
    glm::mat4 m_currentProjection {};

    Glitter::Gfx::Camera m_currentCamera {glm::vec3()};

    struct CommonData {
        glm::mat4 m_view;
        glm::mat4 m_projection;
        glm::vec4 m_eyePos;

        // Directional Light.
        glm::vec4 m_dirLightDirection;
        glm::vec4 m_dirLightColor;

        // Point Light.
        glm::vec4 m_pointLightPosition;
        glm::vec4 m_pointLightColor;

        // Spot Light.
        glm::vec4 m_spotLightPosition;
        glm::vec4 m_spotLightColor;
        glm::vec4 m_spotLightDirection;

        // Floats.
        float m_pointLightRadius;
        float m_spotLightAngleCos;
        float m_spotLightRange;
        float m_padding;
    };

    struct PerDrawData {
        glm::mat4 m_model;
        glm::vec4 m_opacity;
    };
    struct ShaderData {
        CommonData m_commonData;
        PerDrawData m_perDrawData;
    };
    Glitter::Gfx::LinearAllocator m_uboAllocator;

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
        bool m_neverCull {false};
    };
    std::vector<Node> m_nodes;

    std::vector<Mesh> m_meshes;

    bool m_frustumCulling {true};
    bool m_debugLines {true};
    bool m_drawAABBs {false};

    float m_sceneGamma {1.0f};
    float m_pointLightRadius {50.0f};
    float m_spotLightAngle {30.0f};
    float m_spotLightRange {50.0f};
    double m_lastTick {0.0f};
};

} // namespace Glitter

int main()
{
    Glitter::Application glitterApp;
    glitterApp.Run();
    return 0;
}
