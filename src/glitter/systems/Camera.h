#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace Glitter::Gfx {

class Camera {
public:
    explicit Camera(glm::vec3 position);

    void ProcessKeys(int key, int action);
    void ProcessMouse(double x, double y);
    void ProcessMouseButton(int button, int action, int mods);
    void Tick(double dt);

    glm::vec3 m_position;
    glm::vec3 m_up;
    glm::vec3 m_direction;

private:
    bool firstMove = true;

    // Euler Angles
    float m_pitch;
    float m_yaw;

    struct CameraInputState {
        // keyboard
        bool w {false};
        bool s {false};
        bool a {false};
        bool d {false};
        bool q {false};
        bool e {false};

        // mouse
        bool rightButton {false};
        double prevX {};
        double prevY {};
        double x {};
        double y {};
    } m_inputState;
};

} // namespace Glitter::Gfx
