#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
#include "render/Renderer.hpp"
#include "render/Camera.hpp"
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
        renderer.present();
        cnc::Camera camera;
        camera.fit({-10,-10,-10},{50,50,40},1.5);
        const auto a = camera.view_projection(1.5);
        camera.orbit(20,30); const auto b = camera.view_projection(1.5);
        camera.pan(30,20,760); const auto c = camera.view_projection(1.5);
        camera.zoom(2); const auto d = camera.view_projection(1.5);
        if (a == b || b == c || c == d) throw std::runtime_error("camera operation ineffective");
        std::cout << "PASS: stock/tool pixels, authoritative-pose movement, camera and GL errors\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
