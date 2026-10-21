#include "vis/projectm_opengl.h"

#include <QtGlobal>

#if defined(Q_OS_WIN)
// Before any other OpenGL header, so not in milkdrop_view.cpp next to Qt's.
#include <GL/glew.h>
#endif

namespace Vis {

bool LoadOpenGlForProjectM() {
#if defined(Q_OS_WIN)
    // projectM calls OpenGL through GLEW's pointers there and leaves glewInit() to the application.
    glewExperimental = GL_TRUE;
    const GLenum error = glewInit();
    if (error != GLEW_OK) {
        qWarning("Milkdrop: glewInit failed: %s", reinterpret_cast<const char*>(glewGetErrorString(error)));
        return false;
    }
#endif
    return true;
}

}  // namespace Vis
