// Linux libGL exports the core entry points. No generated loader or vendor
// code is needed; GLFW creates and verifies the required 3.3 core context.
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
#include "render/Renderer.hpp"
#include "render/Camera.hpp"
#include "meshing/SurfaceMesher.hpp"
#include "simulation/Workpiece.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace cnc {
namespace {
const char* vertex_source = R"GLSL(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
uniform mat4 model;
uniform mat4 vp;
out vec3 world_normal;
void main() {
    gl_Position = vp * model * vec4(position, 1.0);
    world_normal = mat3(transpose(inverse(model))) * normal;
}
)GLSL";
const char* fragment_source = R"GLSL(#version 330 core
in vec3 world_normal;
uniform vec3 color;
uniform bool unlit;
out vec4 fragment;
void main() {
    float light = unlit ? 1.0 : 0.28 + 0.72 * max(0.0, dot(normalize(world_normal), normalize(vec3(0.4,-0.6,1.0))));
    fragment = vec4(color * light, 1.0);
}
)GLSL";
std::string glfw_error(const char* operation) {
    const char* description = nullptr;
    glfwGetError(&description);
    return std::string(operation) + ": " + (description ? description : "unknown GLFW error");
}
GLuint shader(GLenum kind, const char* source) {
    const auto id = glCreateShader(kind);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    GLint ok = 0; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::array<char, 4096> log{}; glGetShaderInfoLog(id, static_cast<GLsizei>(log.size()), nullptr, log.data());
        glDeleteShader(id);
        throw std::runtime_error(std::string("OpenGL shader compilation: ") + log.data());
    }
    return id;
}
struct GpuMesh {
    GLuint vao{}, vbo{}, ebo{};
    GLsizei count{};
    ~GpuMesh() { glDeleteBuffers(1, &ebo); glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao); }
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    explicit GpuMesh(const ChunkMesh& mesh) {
        if (mesh.indices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
            throw std::runtime_error("mesh exceeds OpenGL index limit");
        count = static_cast<GLsizei>(mesh.indices.size());
        glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)), mesh.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)), mesh.indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    }
};
ChunkMesh cylinder(const ToolDefinition& tool) {
    ChunkMesh mesh;
    constexpr unsigned segments = 48;
    const float r = static_cast<float>(tool.diameter_mm * 0.5), length = static_cast<float>(tool.length_mm);
    for (unsigned i = 0; i < segments; ++i) {
        const float a = static_cast<float>(i) * 2 * glm::pi<float>() / segments;
        const float b = static_cast<float>(i + 1) * 2 * glm::pi<float>() / segments;
        glm::vec3 na(std::cos(a), std::sin(a), 0), nb(std::cos(b), std::sin(b), 0), z(0, 0, length);
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(), {{na*r,na},{nb*r,nb},{nb*r+z,nb},{na*r+z,na},
            {{0,0,0},{0,0,-1}},{nb*r,{0,0,-1}},{na*r,{0,0,-1}},
            {z,{0,0,1}},{na*r+z,{0,0,1}},{nb*r+z,{0,0,1}}});
        for (auto index : {0u,1u,2u,0u,2u,3u,4u,5u,6u,7u,8u,9u}) mesh.indices.push_back(base + index);
    }
    return mesh;
}
}
struct Renderer::Impl {
    GLFWwindow* window{};
    GLuint program{};
    GLint model_location{}, vp_location{}, color_location{}, unlit_location{};
    std::unordered_map<ChunkCoord, std::unique_ptr<GpuMesh>, ChunkCoordHash> stock;
    std::unique_ptr<GpuMesh> tool_mesh, table, axes;
    glm::mat4 workpiece{1};
    glm::dvec3 scene_min{}, scene_max{};
    ToolDefinition tool;
    Camera camera;
    bool fit_requested{true}, dragging{};
    double previous_x{}, previous_y{}, title_time{};
    std::chrono::steady_clock::time_point last_frame{};
    ~Impl() {
        if (window) {
            glfwMakeContextCurrent(window);
            stock.clear(); tool_mesh.reset(); table.reset(); axes.reset();
            if (program) glDeleteProgram(program);
            glfwDestroyWindow(window);
        }
        glfwTerminate();
    }
    void paint(const GpuMesh& mesh, glm::mat4 model, glm::vec3 color, GLenum mode = GL_TRIANGLES) {
        glUniformMatrix4fv(model_location, 1, GL_FALSE, glm::value_ptr(model));
        glUniform3fv(color_location, 1, glm::value_ptr(color));
        glUniform1i(unlit_location, mode == GL_LINES);
        glBindVertexArray(mesh.vao);
        glDrawElements(mode, mesh.count, GL_UNSIGNED_INT, nullptr);
    }
};
Renderer::Renderer(const SparseVoxelVolume& volume, ToolDefinition tool) : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    if (tool.kind != ToolKind::FlatEndMill || !std::isfinite(tool.diameter_mm) || !std::isfinite(tool.length_mm) ||
        tool.diameter_mm <= 0 || tool.length_mm <= 0) throw std::invalid_argument("Phase 4A requires a finite positive flat end mill");
    p.tool = tool;
    validate_workpiece_volume(volume);
    if (!glfwInit()) throw std::runtime_error(glfw_error("GLFW initialization failed"));
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    p.window = glfwCreateWindow(1100, 760, "CNC Simulator - Phase 4A", nullptr, nullptr);
    if (!p.window) throw std::runtime_error(glfw_error("OpenGL 3.3 core window creation failed"));
    glfwMakeContextCurrent(p.window);
    glfwSwapInterval(1);
    GLint major{}, minor{}; glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 3 || (major == 3 && minor < 3)) throw std::runtime_error("OpenGL 3.3 core is required");
    std::cout << "Renderer: OpenGL " << glGetString(GL_VERSION) << " / " << glGetString(GL_RENDERER) << std::endl;
    const auto vertex = shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment{};
    try { fragment = shader(GL_FRAGMENT_SHADER, fragment_source); }
    catch (...) { glDeleteShader(vertex); throw; }
    p.program = glCreateProgram();
    glAttachShader(p.program, vertex); glAttachShader(p.program, fragment); glLinkProgram(p.program);
    glDeleteShader(vertex); glDeleteShader(fragment);
    GLint linked{}; glGetProgramiv(p.program, GL_LINK_STATUS, &linked);
    if (!linked) {
        std::array<char, 4096> log{}; glGetProgramInfoLog(p.program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        throw std::runtime_error(std::string("OpenGL shader link: ") + log.data());
    }
    p.model_location = glGetUniformLocation(p.program, "model"); p.vp_location = glGetUniformLocation(p.program, "vp");
    p.color_location = glGetUniformLocation(p.program, "color"); p.unlit_location = glGetUniformLocation(p.program, "unlit");
    glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
    p.tool_mesh = std::make_unique<GpuMesh>(cylinder(tool));
    set_workpiece(volume);
    glfwSetWindowUserPointer(p.window, &p);
    glfwSetScrollCallback(p.window, [](GLFWwindow* window, double, double dy) {
        static_cast<Impl*>(glfwGetWindowUserPointer(window))->camera.zoom(dy);
    });
    glfwSetKeyCallback(p.window, [](GLFWwindow* window, int key, int, int action, int) {
        if (key == GLFW_KEY_HOME && action == GLFW_PRESS)
            static_cast<Impl*>(glfwGetWindowUserPointer(window))->fit_requested = true;
    });
}
void Renderer::set_workpiece(const SparseVoxelVolume& volume) {
    validate_workpiece_volume(volume);
    auto& p = *impl_;
    const auto last = volume.last_chunk();
    p.stock.clear(); // Full invalidation also removes chunks absent in the new volume.
    p.workpiece = glm::mat4(volume.config().workpiece.matrix());
    p.scene_min = glm::dvec3(0); p.scene_max = glm::dvec3(0);
    for (int corner = 0; corner < 8; ++corner) {
        glm::dvec3 local(0);
        for (int axis = 0; axis < 3; ++axis) if (corner & (1 << axis)) local[axis] = volume.extent_mm()[axis];
        const auto world = volume.config().workpiece.to_machine(local);
        p.scene_min = glm::min(p.scene_min, world); p.scene_max = glm::max(p.scene_max, world);
    }
    const auto margin = std::max(10.0, glm::length(p.scene_max - p.scene_min) * 0.15);
    const float x0 = static_cast<float>(p.scene_min.x - margin), x1 = static_cast<float>(p.scene_max.x + margin);
    const float y0 = static_cast<float>(p.scene_min.y - margin), y1 = static_cast<float>(p.scene_max.y + margin);
    const float z = static_cast<float>(p.scene_min.z - 0.05);
    ChunkMesh table{{{{x0,y0,z},{0,0,1}},{{x1,y0,z},{0,0,1}},{{x1,y1,z},{0,0,1}},{{x0,y1,z},{0,0,1}}},{0,1,2,0,2,3}};
    p.table = std::make_unique<GpuMesh>(table);
    p.scene_min = glm::min(p.scene_min, glm::dvec3(x0,y0,z)); p.scene_max = glm::max(p.scene_max, glm::dvec3(x1,y1,z));
    const float axis_length = static_cast<float>(std::max(20.0, glm::length(volume.extent_mm()) * 0.85));
    ChunkMesh axes;
    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 end(0); end[axis] = axis_length;
        axes.vertices.push_back({{0,0,0},{0,0,1}}); axes.vertices.push_back({end,{0,0,1}});
        axes.indices.push_back(static_cast<unsigned>(axis * 2)); axes.indices.push_back(static_cast<unsigned>(axis * 2 + 1));
    }
    p.axes = std::make_unique<GpuMesh>(axes);
    p.scene_max = glm::max(p.scene_max, glm::dvec3(axis_length));
    SurfaceMesher mesher;
    // CPU extraction and GPU upload are separate operations, both outside the
    // UDP worker/mailbox. Every published revision defines fresh raw stock.
    DirtyChunks dirty;
    for (std::int64_t zc = 0; zc <= last.z; ++zc) for (std::int64_t yc = 0; yc <= last.y; ++yc) for (std::int64_t xc = 0; xc <= last.x; ++xc)
        dirty.mark({xc,yc,zc});
    for (const auto coord : dirty.take()) {
        auto mesh = mesher.build(volume, coord);
        if (!mesh.indices.empty()) p.stock.emplace(coord, std::make_unique<GpuMesh>(mesh));
        glfwPollEvents();
        if (glfwWindowShouldClose(p.window)) throw std::runtime_error("renderer closed during scene creation");
    }
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL scene upload failed");
    p.fit_requested = true;
}
Renderer::~Renderer() = default;
bool Renderer::draw(const MachineSnapshot& snapshot) {
    auto& p = *impl_;
    glfwPollEvents();
    if (glfwWindowShouldClose(p.window)) return false;
    int width{}, height{}; glfwGetFramebufferSize(p.window, &width, &height);
    if (!width || !height) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); return true; }
    const auto pose = tool_pose(snapshot);
    const double aspect = static_cast<double>(width) / height;
    if (p.fit_requested) {
        p.camera.fit(glm::min(p.scene_min, pose.tip_mm - glm::dvec3(p.tool.diameter_mm)),
                     glm::max(p.scene_max, pose.tip_mm + glm::dvec3(p.tool.diameter_mm, p.tool.diameter_mm, p.tool.length_mm)), aspect);
        p.fit_requested = false;
    }
    double x{}, y{}; glfwGetCursorPos(p.window, &x, &y);
    const bool orbit = glfwGetMouseButton(p.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool pan = glfwGetMouseButton(p.window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    if (p.dragging) {
        if (orbit) p.camera.orbit(x - p.previous_x, y - p.previous_y);
        if (pan) { int w{}, h{}; glfwGetWindowSize(p.window, &w, &h); p.camera.pan(x - p.previous_x, y - p.previous_y, h); }
    }
    p.dragging = orbit || pan; p.previous_x = x; p.previous_y = y;
    glViewport(0, 0, width, height); glClearColor(0.055f, 0.07f, 0.09f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(p.program);
    const auto vp = p.camera.view_projection(aspect);
    glUniformMatrix4fv(p.vp_location, 1, GL_FALSE, glm::value_ptr(vp));
    p.paint(*p.table, glm::mat4(1), {0.25f,0.29f,0.34f});
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1, 1);
    for (const auto& [coord, mesh] : p.stock) { (void)coord; p.paint(*mesh, p.workpiece, {0.40f,0.65f,0.80f}); }
    glDisable(GL_POLYGON_OFFSET_FILL);
    p.paint(*p.tool_mesh, glm::translate(glm::mat4(1), glm::vec3(pose.tip_mm)), {0.95f,0.72f,0.24f});
    glUniformMatrix4fv(p.model_location, 1, GL_FALSE, glm::value_ptr(glm::mat4(1)));
    glUniform1i(p.unlit_location, 1); glBindVertexArray(p.axes->vao);
    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 color(0.15f); color[axis] = 1;
        glUniform3fv(p.color_location, 1, glm::value_ptr(color));
        glDrawElements(GL_LINES, 2, GL_UNSIGNED_INT, reinterpret_cast<void*>(static_cast<std::uintptr_t>(axis * 2 * sizeof(std::uint32_t))));
    }
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL draw failed");
    if (glfwGetTime() - p.title_time >= 0.2) {
        std::ostringstream title; title.setf(std::ios::fixed); title.precision(3);
        title << "CNC Phase 4A | X " << pose.tip_mm.x << " Y " << pose.tip_mm.y << " Z " << pose.tip_mm.z
              << " mm | XYZ=RGB | LMB orbit / MMB pan / wheel zoom / Home fit";
        glfwSetWindowTitle(p.window, title.str().c_str()); p.title_time = glfwGetTime();
    }
    return true;
}
void Renderer::present() {
    auto& p = *impl_;
    glfwSwapBuffers(p.window);
    // Bound CPU use even when the window system ignores the vsync request.
    const auto deadline = p.last_frame + std::chrono::microseconds(16667);
    std::this_thread::sleep_until(deadline);
    p.last_frame = std::chrono::steady_clock::now();
}
} // namespace cnc
