#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
#include "render/Renderer.hpp"
#include "render/Camera.hpp"
#include "simulation/Simulation.hpp"
#include "console/WorkpieceCommands.hpp"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    try {
        cnc::SparseVoxelVolume volume;
        cnc::Renderer renderer(volume);
        cnc::MachineSnapshot snapshot{{0,0,0,0},{400,400,400,400},0};
        if (!renderer.draw(snapshot)) throw std::runtime_error("window closed unexpectedly");
        GLint viewport[4]; glGetIntegerv(GL_VIEWPORT,viewport);
        std::vector<unsigned char> first(static_cast<std::size_t>(viewport[2]*viewport[3]*4));
        glReadPixels(0,0,viewport[2],viewport[3],GL_RGBA,GL_UNSIGNED_BYTE,first.data());
        std::size_t stock = 0, tool = 0;
        for (std::size_t i=0;i<first.size();i+=4) {
            if (first[i+2] > 80 && first[i+2] > first[i] * 1.5) ++stock;
            if (first[i] > 100 && first[i] > first[i+2] * 2) ++tool;
        }
        if (stock < 1000 || tool < 100) throw std::runtime_error("stock/tool missing from framebuffer");
        snapshot.steps = {4000,4000,800,0}; snapshot.accepted_packets = 1;
        if (!renderer.draw(snapshot)) throw std::runtime_error("window closed unexpectedly");
        std::vector<unsigned char> second(first.size());
        glReadPixels(0,0,viewport[2],viewport[3],GL_RGBA,GL_UNSIGNED_BYTE,second.data());
        std::size_t changed = 0;
        for (std::size_t i=0;i<first.size();++i) if (first[i] != second[i]) ++changed;
        if (changed < 1000 || glGetError() != GL_NO_ERROR) throw std::runtime_error("tool did not move or GL error");
        if (argc == 2) {
            std::ofstream image(argv[1], std::ios::binary);
            image << "P6\n" << viewport[2] << ' ' << viewport[3] << "\n255\n";
            for (int y=viewport[3]-1;y>=0;--y) for (int x=0;x<viewport[2];++x)
                image.write(reinterpret_cast<const char*>(second.data() + (y*viewport[2]+x)*4),3);
            if (!image) throw std::runtime_error("framebuffer export failed");
        }
        cnc::Simulation simulation("127.0.0.1", 0, {400,400,400,400});
        auto pixels = second;
        for (const auto* command : {"workpiece position 20 -10 5", "workpiece origin center center max",
                                   "workpiece size 30 40 12", "workpiece voxel 7", "workpiece reset"}) {
            const auto update = cnc::parse_workpiece_command(command, simulation.workpiece_snapshot()->config);
            simulation.configure_workpiece(update.config);
            renderer.set_workpiece(*simulation.workpiece_snapshot()->volume);
            if (!renderer.draw(snapshot)) throw std::runtime_error("window closed during rebuild");
            std::vector<unsigned char> next(pixels.size());
            glReadPixels(0,0,viewport[2],viewport[3],GL_RGBA,GL_UNSIGNED_BYTE,next.data());
            std::size_t difference = 0, blue = 0;
            for (std::size_t i=0;i<next.size();i+=4) {
                if (next[i] != pixels[i] || next[i+1] != pixels[i+1] || next[i+2] != pixels[i+2]) ++difference;
                if (next[i+2] > 80 && next[i+2] > next[i] * 1.5) ++blue;
            }
            if (difference < 100 || blue < 1000 || glGetError() != GL_NO_ERROR)
                throw std::runtime_error(std::string("runtime rebuild not visible: ") + command);
            pixels = std::move(next);
        }
        renderer.present();
        cnc::Camera camera;
        camera.fit({-10,-10,-10},{50,50,40},1.5);
        const auto a = camera.view_projection(1.5);
        camera.orbit(20,30); const auto b = camera.view_projection(1.5);
        camera.pan(30,20,760); const auto c = camera.view_projection(1.5);
        camera.zoom(2); const auto d = camera.view_projection(1.5);
        if (a == b || b == c || c == d) throw std::runtime_error("camera operation ineffective");
        std::cout << "PASS: stock/tool pixels, authoritative-pose movement, runtime position/origin/size/voxel/reset, camera and GL errors\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
