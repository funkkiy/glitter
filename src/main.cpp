#include "glitter/util/File.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cgltf.h>
#include <spdlog/spdlog.h>
#include <stb_image.h>

#include <algorithm>
#include <array>
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
        if (!m_initializedAlignment) {
            glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &m_alignment);
            m_initializedAlignment = true;
        }

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
        assert(m_initializedAlignment);
        return m_alignment;
    }
    void Clear() { m_buffer.clear(); }

private:
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

struct Mesh {
    glm::mat4 m_position;
    std::vector<Primitive> m_primitives;
};

// Vertex Attributes!
struct MeshVertex {
    float x, y, z;
    float u, v;
    float nx, ny, nz;
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
                    // We only accept up to 999 nodes.
                    if (app->m_nodes.size() >= 999) {
                        break;
                    }

                    static float opacity = 1.0f;

                    app->m_nodes.push_back(Node {.m_position = glm::sphericalRand(6.0f),
                        .m_texture = app->m_loadedTextures[std::rand() % app->m_loadedTextures.size()],
                        .m_meshID = std::rand() % app->m_meshes.size(),
                        .m_uboOffset = 0,
                        .m_opacity = opacity,
                        .m_scale = glm::vec3(0.25f)});

                    if (opacity == 1.0f) {
                        opacity = 0.5f;
                    } else {
                        opacity = 1.0f;
                    }
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
        glEnable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

        // glTF mesh!
        std::array meshPaths(std::to_array<const char*>({"meshes/Cube.gltf", "meshes/Teapot.gltf"}));
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
            };

            Mesh glitterMesh {};
            std::vector<GltfMesh> parsedMeshes;
            if (result == cgltf_result_success) {
                // Iterate through each meshes, then through its primitives and their attributes, creating one VBO per primitive and
                // filling it with data pointed by the attribute buffer views. A mesh can have several primitives.
                for (int meshIdx = 0; meshIdx < data->meshes_count; meshIdx++) {
                    cgltf_mesh mesh = data->meshes[meshIdx];

                    GltfMesh gltfMesh {};
                    for (int primIdx = 0; primIdx < mesh.primitives_count; primIdx++) {
                        cgltf_primitive prim = mesh.primitives[primIdx];

                        size_t vertexCount {0};
                        // Fill out the accessor pointers.
                        cgltf_accessor* positionAccessor = nullptr;
                        cgltf_accessor* texCoordAccessor = nullptr;
                        cgltf_accessor* normalAccessor = nullptr;
                        for (int attribIdx = 0; attribIdx < prim.attributes_count; attribIdx++) {
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
                        for (int vertexIdx = 0; vertexIdx < vertexCount; vertexIdx++) {
                            MeshVertex vertex {};

                            if (positionAccessor) {
                                cgltf_accessor_read_float(positionAccessor, vertexIdx, &vertex.x, 3);
                            }
                            if (texCoordAccessor) {
                                cgltf_accessor_read_float(texCoordAccessor, vertexIdx, &vertex.u, 2);
                            }
                            if (normalAccessor) {
                                cgltf_accessor_read_float(normalAccessor, vertexIdx, &vertex.nx, 3);
                            }

                            gltfPrim.m_vertexData.emplace_back(vertex);
                        }

                        for (int indexIdx = 0; indexIdx < prim.indices->count; indexIdx++) {
                            gltfPrim.m_vertexIndices.emplace_back(cgltf_accessor_read_index(prim.indices, indexIdx));
                        }

                        gltfMesh.m_primitives.emplace_back(gltfPrim);
                    } // Iterating through the primitives.

                    parsedMeshes.emplace_back(gltfMesh);
                } // Iterating through the meshes.

                cgltf_free(data);

                for (auto& primitives : parsedMeshes[0].m_primitives) {
                    Primitive primitive {.m_elementCount = narrow_into<GLsizei>(primitives.m_vertexIndices.size())};

                    // Create VBO.
                    GLuint VBO;
                    glCreateBuffers(1, &VBO);
                    glNamedBufferStorage(VBO, sizeof(MeshVertex) * parsedMeshes[0].m_primitives[0].m_vertexData.size(),
                        parsedMeshes[0].m_primitives[0].m_vertexData.data(), 0);
                    primitive.m_vbo = VBO;

                    // Create EBO.
                    GLuint EBO;
                    glCreateBuffers(1, &EBO);
                    glNamedBufferStorage(EBO, sizeof(uint32_t) * parsedMeshes[0].m_primitives[0].m_vertexIndices.size(),
                        parsedMeshes[0].m_primitives[0].m_vertexIndices.data(), 0);
                    primitive.m_ebo = EBO;

                    // Add primitive to the Mesh.
                    glitterMesh.m_primitives.emplace_back(primitive);
                }

                m_meshes.emplace_back(glitterMesh);
            }
        }

        // Create VAO.
        GLuint VAO;
        glCreateVertexArrays(1, &VAO);

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

        // Just enough for the Common stuff and 999 Nodes.
        glNamedBufferData(ubo, sizeof(CommonData) + (sizeof(PerDrawData) * 999), nullptr, GL_DYNAMIC_DRAW);
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
            .m_lightColor = glm::vec4(1.0, 1.0, 1.0, 1.0)};
        m_uboAllocator.Push(commonData);

        // Write each Node's PerDrawData into the buffer.
        for (auto& node : m_nodes) {
            // The Model has to follow the Scale-Rotate-Translate
            // order.
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::scale(model, node.m_scale);
            model = glm::translate(model, node.m_position);

            PerDrawData shaderData {.m_model = model, .m_opacity = node.m_opacity};
            node.m_uboOffset = m_uboAllocator.Push(shaderData);
        }

        // Upload the CPU-backing buffer into the UBO.
        glNamedBufferSubData(m_currentUBO, 0, sizeof(uint8_t) * m_uboAllocator.Size(), m_uboAllocator.Data());

        // Bind the Program and VAO.
        glUseProgram(m_currentProgram);
        glBindVertexArray(m_currentVAO);

        // Split Node elements between opaque and transparent.
        std::vector<Node> m_opaqueNodes {};
        std::vector<Node> m_transparentNodes {};
        for (Node& node : m_nodes) {
            if (node.m_opacity == 1.0f) {
                m_opaqueNodes.emplace_back(node);
            } else {
                m_transparentNodes.emplace_back(node);
            }
        }

        // Sort each transparent Node by their distance to the camera.
        std::sort(m_transparentNodes.begin(), m_transparentNodes.end(),
            [&eyePos](Node& a, Node& b) { return glm::distance(eyePos, a.m_position) < glm::distance(eyePos, b.m_position); });

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
        glEnable(GL_DEPTH_TEST);
        renderNodes(m_opaqueNodes);

        // Render each transparent Node.
        glDisable(GL_DEPTH_TEST);
        renderNodes(m_transparentNodes);

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
        float m_opacity;
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
    };
    std::vector<Node> m_nodes {};

    std::vector<Mesh> m_meshes {};
};

int main()
{
    GlitterApplication glitterApp;
    glitterApp.Run();
    return 0;
}