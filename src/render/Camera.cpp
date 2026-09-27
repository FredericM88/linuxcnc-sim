#include "render/Camera.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace cnc {
glm::dvec3 Camera::direction() const {
    return {std::cos(elevation_) * std::cos(yaw_), std::cos(elevation_) * std::sin(yaw_), std::sin(elevation_)};
}
void Camera::fit(glm::dvec3 minimum, glm::dvec3 maximum, double aspect) {
    target_ = (minimum + maximum) * 0.5;
    radius_ = std::max(1.0, glm::length(maximum - minimum) * 0.5);
    const auto half_angle = std::atan(std::tan(glm::radians(22.5)) * std::min(1.0, aspect));
    distance_ = radius_ / std::sin(half_angle) * 1.12;
}
void Camera::orbit(double dx, double dy) {
    yaw_ -= dx * 0.006;
    elevation_ = std::clamp(elevation_ + dy * 0.006, -1.50, 1.50);
}
void Camera::pan(double dx, double dy, int height) {
    const auto right = glm::normalize(glm::cross(-direction(), glm::dvec3(0, 0, 1)));
    const auto up = glm::cross(right, -direction());
    const auto scale = 2 * distance_ * std::tan(glm::radians(22.5)) / std::max(1, height);
    target_ += (-right * dx + up * dy) * scale;
}
void Camera::zoom(double amount) { distance_ = std::clamp(distance_ * std::exp(-amount * 0.12), 0.1, 1e9); }
glm::mat4 Camera::view_projection(double aspect) const {
    const auto near_plane = std::max(0.001, distance_ * 0.0001);
    const auto far_plane = std::max(distance_ * 10, distance_ + radius_ * 4);
    return glm::mat4(glm::perspective(glm::radians(45.0), aspect, near_plane, far_plane) *
                     glm::lookAt(target_ + direction() * distance_, target_, glm::dvec3(0, 0, 1)));
}
} // namespace cnc
