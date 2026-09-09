package org.octave.phoneserver.opengl;

public interface OpenGLFilter {

    /**
     * Initialize the OpenGL filter (typically compile the shaders and create the program).
     *
     * @throws OpenGLException if an initialization error occurs
     */
    void init() throws OpenGLException;

    /**
     * Render a frame (call for each frame).
     */
    void draw(int textureId, float[] texMatrix);

    /**
     * Release resources.
     */
    void release();
}
