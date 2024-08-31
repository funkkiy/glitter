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
    LinearAllocator() {};

    // Returns the offset after the object in the buffer.
    template <typename T> size_t Push(T& t)
    {
        InitializeAlignment();

        // Calculate total amount of bytes that will be pushed.
        size_t futureSize = m_buffer.size() + sizeof(T);
        size_t paddingRequired = futureSize % m_alignment == 0 ? 0 : m_alignment - (futureSize % m_alignment);
        size_t bytesRequired = futureSize + paddingRequired;
        m_buffer.reserve(m_buffer.size() + bytesRequired);

        size_t offsetBeforePush = m_buffer.size();

        // Push the object.
        m_buffer.insert(m_buffer.end(), reinterpret_cast<uint8_t*>(&t), reinterpret_cast<uint8_t*>(&t) + sizeof(T));

        // Push the padding.
        m_buffer.resize(m_buffer.size() + paddingRequired);

        return offsetBeforePush;
    }

    uint8_t* Data() { return m_buffer.data(); }
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

    std::vector<uint8_t> m_buffer {};

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
            GLchar error[512];
            GLsizei errorLen = 0;
            glGetShaderInfoLog(shader, 512, &errorLen, error);
            spdlog::error("[{}] {}", type == GL_VERTEX_SHADER ? "vertex" : "fragment", error);
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

            if (width == 0 || height == 0) {
                return;
            }

            app->m_windowWidth = width;
            app->m_windowHeight = height;
            glViewport(0, 0, width, height);
        });

        glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
            auto app = static_cast<GlitterApplication*>(glfwGetWindowUserPointer(window));
            switch (key) {
            case GLFW_KEY_SPACE:
                if (action == GLFW_RELEASE) {
                    // We only accept up to 9999 nodes.
                    if (app->m_nodes.size() >= 9999) {
                        break;
                    }

                    for (int i = 0; i < 500; i++) {
                        app->m_nodes.push_back(Node {.m_position = glm::sphericalRand(45.0f),
                            .m_texture = app->m_loadedTextures[std::rand() % app->m_loadedTextures.size()],
                            .m_meshID = std::rand() % app->m_meshes.size(),
                            .m_uboOffset = 0,
                            .m_opacity = 1.0f,
                            .m_scale = glm::vec3(0.25f),
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

        // Create the Vertex and Fragment shaders.
        
        // Vertex Shader.
        GLuint vertexShader = CreateShaderFromPath(GL_VERTEX_SHADER, "shaders/VertexShader.glsl").value_or(0);
        if (!vertexShader) {
            return PrepareResult::ShaderCompileError;
        }

        // Fragment Shader.
        GLuint fragmentShader = CreateShaderFromPath(GL_FRAGMENT_SHADER, "shaders/FragShader.glsl").value_or(0);
        if (!fragmentShader) {
            return PrepareResult::ShaderCompileError;
        }

        // Link the shaders into a Program.
        GLuint shaderProgram = glCreateProgram();
        glObjectLabel(GL_PROGRAM, shaderProgram, -1, "Shader Program");
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
                            gltfPrim.m_vertexIndices.emplace_back(cgltf_accessor_read_index(prim.indices, indexIdx));
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
                    GLuint VBO;
                    glCreateBuffers(1, &VBO);
                    glNamedBufferStorage(VBO, sizeof(MeshVertex) * parsedMeshes[0].m_primitives[0].m_vertexData.size(),
                        parsedMeshes[0].m_primitives[0].m_vertexData.data(), 0);
                    glObjectLabel(GL_BUFFER, VBO, -1, "VBO");
                    primitive.m_vbo = VBO;

                    // Create EBO.
                    GLuint EBO;
                    glCreateBuffers(1, &EBO);
                    glNamedBufferStorage(EBO, sizeof(uint32_t) * parsedMeshes[0].m_primitives[0].m_vertexIndices.size(),
                        parsedMeshes[0].m_primitives[0].m_vertexIndices.data(), 0);
                    glObjectLabel(GL_BUFFER, EBO, -1, "EBO");
                    primitive.m_ebo = EBO;

                    // Add primitive to the Mesh.
                    glitterMesh.m_primitives.emplace_back(primitive);
                }

                glitterMesh.m_aabb = parsedMeshes[0].m_aabb;

                m_meshes.emplace_back(glitterMesh);
            }
        }

        // Create VAO.
        GLuint VAO;
        glCreateVertexArrays(1, &VAO);
        glObjectLabel(GL_VERTEX_ARRAY, VAO, -1, "VAO");

        // Declare the Position Attribute.
        glEnableVertexArrayAttrib(VAO, 0);
        glVertexArrayAttribFormat(VAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(MeshVertex, x));
        glVertexArrayAttribBinding(VAO, 0, 0);

        // Declare the UV Attribute.
        glEnableVertexArrayAttrib(VAO, 1);
        glVertexArrayAttribFormat(VAO, 1, 2, GL_FLOAT, GL_FALSE, offsetof(MeshVertex, u));
        glVertexArrayAttribBinding(VAO, 1, 0);

        // Declare the Normal attribute
        glEnableVertexArrayAttrib(VAO, 2);
        glVertexArrayAttribFormat(VAO, 2, 3, GL_FLOAT, GL_FALSE, offsetof(MeshVertex, nx));
        glVertexArrayAttribBinding(VAO, 2, 0);

        m_currentVAO = VAO;

        // Create empty UBO buffer.
        GLuint ubo {};
        glCreateBuffers(1, &ubo);
        glObjectLabel(GL_BUFFER, ubo, -1, "UBO");

        // Just enough for the Common stuff and 9999 Nodes.
        glNamedBufferData(
            ubo, sizeof(CommonData) + ((sizeof(PerDrawData) + m_uboAllocator.GetAlignment()) * 9999), nullptr, GL_DYNAMIC_DRAW);
        m_currentUBO = ubo;

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

    void Tick()
    {
        glfwPollEvents();

        // Start Dear Imgui frame.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Add Dear ImGui Demo UI.
        ImGui::ShowDemoWindow();

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
                glm::mat4 aabbTransform = glm::mat4(1.0f);
                aabbTransform = glm::scale(aabbTransform, node.m_scale);
                aabbTransform = glm::translate(aabbTransform, node.m_position);

                AABB aabb = m_meshes[node.m_meshID].m_aabb;
                std::array aabbCorners = std::to_array({
                    glm::vec3 {aabb.m_localMin},
                    glm::vec3 {aabb.m_localMax.x, aabb.m_localMin.y, aabb.m_localMin.z},
                    glm::vec3 {aabb.m_localMin.x, aabb.m_localMax.y, aabb.m_localMin.z},
                    glm::vec3 {aabb.m_localMin.x, aabb.m_localMin.y, aabb.m_localMax.z},
                    glm::vec3 {aabb.m_localMax.x, aabb.m_localMin.y, aabb.m_localMax.z},
                    glm::vec3 {aabb.m_localMax.x, aabb.m_localMax.y, aabb.m_localMin.z},
                    glm::vec3 {aabb.m_localMin.x, aabb.m_localMax.y, aabb.m_localMax.z},
                    glm::vec3 {aabb.m_localMax},
                });

                // Transform the corners in `aabbCorners` into world space.
                for (auto& corner : aabbCorners) {
                    corner = glm::vec3(aabbTransform * glm::vec4(corner, 1.0f));
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
                    node.m_culled = true;
                    numCulledNodes += 1;
                    continue;
                } else {
                    node.m_culled = false;
                }
            }

            // The Model has to follow the Scale-Rotate-Translate
            // order.
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::scale(model, node.m_scale);
            model = glm::translate(model, node.m_position);

            PerDrawData shaderData {.m_model = model, .m_opacity = node.m_opacity, .m_unused = {0}};
            node.m_uboOffset = m_uboAllocator.Push(shaderData);
        }

        // Add Debug UI.
        ImGui::Begin("Glitter Debug");
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Frustum Culling", &m_frustumCulling);
            ImGui::Text("Culled Nodes: %d/%d (%.2f%%)", numCulledNodes, m_nodes.size(),
                m_nodes.size() != 0 ? static_cast<float>(numCulledNodes) / m_nodes.size() * 100.0f : 0.0f);
            if (ImGui::Button("Clear Nodes", ImVec2(-1.0f, 0.0f))) {
                m_nodes.clear();
            }
        }
        ImGui::End();

        // Upload the CPU-backing buffer into the UBO.
        glNamedBufferSubData(m_currentUBO, 0, sizeof(uint8_t) * m_uboAllocator.Size(), m_uboAllocator.Data());

        // Bind the Program and VAO.
        glUseProgram(m_currentProgram);
        glBindVertexArray(m_currentVAO);

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
        std::sort(opaqueNodes.begin(), opaqueNodes.end(),
            [&eyePos](Node& a, Node& b) { return glm::distance(eyePos, a.m_position) < glm::distance(eyePos, b.m_position); });

        // Sort each transparent Node from back-to-front.
        std::sort(transparentNodes.begin(), transparentNodes.end(),
            [&eyePos](Node& a, Node& b) { return glm::distance(eyePos, a.m_position) > glm::distance(eyePos, b.m_position); });

        auto renderNodes = [this](std::vector<Node> nodes) {
            for (Node& node : nodes) {
                size_t meshIdx = node.m_meshID;

                for (auto& primitive : m_meshes[meshIdx].m_primitives) {
                    // Attach the VBO to the VAO.
                    glVertexArrayVertexBuffer(m_currentVAO, 0, primitive.m_vbo, 0, sizeof(MeshVertex));

                    // Attach the EBO to the VAO.
                    glVertexArrayElementBuffer(m_currentVAO, primitive.m_ebo);

                    // Bind the Common UBO data into the first slot of the UBO.
                    glBindBufferRange(GL_UNIFORM_BUFFER, 0, m_currentUBO, 0, sizeof(CommonData));

                    // Bind the Per-Draw UBO data into the second slot of the UBO.
                    glBindBufferRange(GL_UNIFORM_BUFFER, 1, m_currentUBO, node.m_uboOffset, sizeof(PerDrawData));

                    // Bind the texture.
                    glBindTextureUnit(0, node.m_texture);

                    // Draw the Primitive!
                    glDrawElements(GL_TRIANGLES, primitive.m_elementCount, GL_UNSIGNED_INT, 0);
                }
            }
        };

        // Render each opaque Node.
        if (!opaqueNodes.empty()) {
            glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Opaque Nodes");
            {
                glDepthMask(GL_TRUE);
                renderNodes(opaqueNodes);
            }
            glPopDebugGroup();
        }

        // Render each transparent Node.
        if (!transparentNodes.empty()) {
            glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 1, -1, "Transparent Nodes");
            {
                glDepthMask(GL_FALSE);
                renderNodes(transparentNodes);
            }
            glPopDebugGroup();
        }

        // Render Dear ImGui.
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 2, -1, "Dear ImGui");
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

        // Shutdown GLFW.
        glfwTerminate();
    }

    GLFWwindow* m_window {};
    GLuint m_currentProgram {};
    GLuint m_currentVAO {};
    GLuint m_currentUBO {};

    uint32_t m_windowWidth {1366};
    uint32_t m_windowHeight {768};

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
        float m_unused[3];
    };
    struct ShaderData {
        CommonData m_commonData;
        PerDrawData m_perDrawData;
    };
    LinearAllocator m_uboAllocator {};

    std::vector<GLuint> m_loadedTextures {};

    struct Node {
        glm::vec3 m_position;
        GLuint m_texture;
        size_t m_meshID;
        size_t m_uboOffset;
        float m_opacity;
        glm::vec3 m_scale;
        bool m_shouldAnimate;
        bool m_culled;
    };
    std::vector<Node> m_nodes {};

    std::vector<Mesh> m_meshes {};

    bool m_frustumCulling = true;
};

int main()
{
    GlitterApplication glitterApp;
    glitterApp.Run();
    return 0;
}