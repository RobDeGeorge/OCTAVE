// graphicsrenderer.h
//
// What draws the window: "OpenGL: <GL_RENDERER>", "Metal", "Direct3D 11", ...
// Read on the render thread once the scene graph is up, logged, and handed to
// Main.qml as its `graphicsRenderer` / `gpuRendered` properties. Main.qml
// builds the 3D vehicle page in the background only when a GPU is known to
// draw it: on Mesa's llvmpipe (a machine or VM with no GPU) that build took
// 14-19 s with the UI blocked, and 0.9.6-rc1 did it on the first visit to
// Sensor Home.

#ifndef GRAPHICSRENDERER_H
#define GRAPHICSRENDERER_H

#include <QString>

class QObject;

namespace GraphicsRenderer {

// True only when `description` names a GPU: an OpenGL renderer that is not a
// software rasterizer, or Metal. Direct3D and Vulkan say nothing about the
// device here (WARP, lavapipe), and "" is not known yet: all false.
bool isGpu(const QString &description);

// `rootObject` is the QML root (a QQuickWindow; anything else is ignored).
// Sets its graphicsRenderer / gpuRendered properties, now if the scene graph
// is already up and again each time it is rebuilt.
void report(QObject *rootObject);

} // namespace GraphicsRenderer

#endif // GRAPHICSRENDERER_H
