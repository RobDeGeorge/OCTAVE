#include "graphicsrenderer.h"

#include <QLoggingCategory>
#include <QQuickWindow>
#include <QRunnable>
#include <QSGRendererInterface>
#include <QtGui/qtguiglobal.h>
#if QT_CONFIG(opengl)
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#endif

Q_LOGGING_CATEGORY(lcGraphics, "octave.graphics")

namespace GraphicsRenderer {

bool isGpu(const QString &description)
{
    if (description == QLatin1String("Metal"))
        return true;   // Apple has no software Metal device
    const QLatin1String gl("OpenGL: ");
    if (!description.startsWith(gl))
        return false;
    const QString name = description.mid(gl.size()).trimmed();
    if (name.isEmpty() || name == QLatin1String("?"))
        return false;
    // Mesa (llvmpipe, softpipe, the old swrast "Software Rasterizer"),
    // Google's SwiftShader, Windows' GDI Generic and Basic Render Driver
    static const char *const software[] = { "llvmpipe", "softpipe", "swrast", "software",
                                            "swiftshader", "gdi generic", "basic render" };
    for (const char *s : software) {
        if (name.contains(QLatin1String(s), Qt::CaseInsensitive))
            return false;
    }
    return true;
}

// Render thread, scene graph initialized
static QString describe(QQuickWindow *window)
{
    QSGRendererInterface *ri = window->rendererInterface();
    if (!ri)
        return QString();
    switch (ri->graphicsApi()) {
    case QSGRendererInterface::OpenGL: {
        QString name = QStringLiteral("?");
#if QT_CONFIG(opengl)
        auto *gl = static_cast<QOpenGLContext *>(
            ri->getResource(window, QSGRendererInterface::OpenGLContextResource));
        if (gl && (QOpenGLContext::currentContext() == gl || gl->makeCurrent(window))) {
            if (const GLubyte *s = gl->functions()->glGetString(GL_RENDERER))
                name = QString::fromLatin1(reinterpret_cast<const char *>(s));
        }
#endif
        return QStringLiteral("OpenGL: ") + name;
    }
    case QSGRendererInterface::Metal:
        return QStringLiteral("Metal");
    case QSGRendererInterface::Direct3D11:
        return QStringLiteral("Direct3D 11");
    case QSGRendererInterface::Vulkan:
        return QStringLiteral("Vulkan");
    case QSGRendererInterface::Software:
        return QStringLiteral("Qt Quick software");
    default:
        return QStringLiteral("other");
    }
}

// Render thread: read, then set the properties on the window's own thread
static void readAndPost(QQuickWindow *window)
{
    const QString description = describe(window);
    QMetaObject::invokeMethod(window, [window, description]() {
        if (window->property("graphicsRenderer").toString() == description)
            return;
        qCInfo(lcGraphics).noquote() << "Window drawn by" << (description.isEmpty() ? QStringLiteral("?") : description)
                                     << (isGpu(description) ? "(GPU)" : "(no GPU known)");
        window->setProperty("graphicsRenderer", description);
        window->setProperty("gpuRendered", isGpu(description));
    }, Qt::QueuedConnection);
}

void report(QObject *rootObject)
{
    auto *window = qobject_cast<QQuickWindow *>(rootObject);
    if (!window)
        return;
    QObject::connect(window, &QQuickWindow::sceneGraphInitialized, window,
                     [window]() { readAndPost(window); }, Qt::DirectConnection);
    if (window->isSceneGraphInitialized()) {
        window->scheduleRenderJob(QRunnable::create([window]() { readAndPost(window); }),
                                  QQuickWindow::BeforeSynchronizingStage);
        window->update();
    }
}

} // namespace GraphicsRenderer
