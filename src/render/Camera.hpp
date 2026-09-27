#pragma once
#include <glm/glm.hpp>

namespace cnc {
class Camera {
public:
    void fit(glm::dvec3 minimum, glm::dvec3 maximum, double aspect);
    void orbit(double dx, double dy);
    void pan(double dx, double dy, int height);
    void zoom(double amount);
    glm::mat4 view_projection(double aspect) const;
private:
    glm::dvec3 direction() const;
    glm::dvec3 target_{0};
    double distance_{100}, radius_{50}, yaw_{-0.9}, elevation_{0.65};
};
} // namespace cnc
