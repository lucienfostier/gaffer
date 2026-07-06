#pragma once

#include "GafferOFX/Export.h"

#include <string>
#include <vector>

namespace GafferOFX
{

/// Manages offscreen GL contexts for OpenGL-based OFX rendering.
/// Priority: EGL (GPU + surfaceless) > GLX (GPU with X11) > OSMesa (CPU).
class GAFFEROFX_API GLContextManager
{
	public :

		static GLContextManager& instance();

		~GLContextManager();

		/// Make the offscreen context current on the calling thread.
		/// Returns true on success.
		bool makeCurrent();

		/// Release the current context.
		void release();

		/// Register a texture ID for later cleanup.
		void registerTexture( unsigned int id );
		/// Clean up all registered textures.
		void cleanupTextures();

		/// Returns true if the active context is a hardware-accelerated
		/// (EGL or GLX) rather than OSMesa software fallback.
		bool usingHardware() const { return m_usingHardware; }

		/// Returns the active backend name ("EGL", "GLX", "OSMesa", or "none").
		const char* backendName() const;

		/// Returns the GL_RENDERER string of the active context.
		const char* rendererString() const { return m_rendererString.c_str(); }

		/// Set the current output FBO+texture for GL rendering.
		/// The texture is the color attachment of the FBO; plugins that call
		/// loadTexture("Output") receive this texture as their render target.
		void setOutputFBO( unsigned int fbo, unsigned int tex, int width, int height );
		/// Get the current output FBO (color attachment is the render texture).
		unsigned int outputFBO() const { return m_outputFBO; }
		/// Get the current output texture (plugin render target).
		unsigned int outputTexture() const { return m_outputTex; }
		/// Get the current output texture dimensions.
		int outputTexWidth() const { return m_outputTexW; }
		int outputTexHeight() const { return m_outputTexH; }

	private :

		GLContextManager();
		GLContextManager( const GLContextManager& ) = delete;
		GLContextManager& operator=( const GLContextManager& ) = delete;

		/// Try to initialize GLEW and validate. Returns true if GL is usable.
		bool initGLEW( const char *backendName );

		// EGL members (preferred — GPU + pbuffer for FBO 0 compatibility)
		void* m_eglDisplay;
		void* m_eglContext;
		void* m_eglSurface;

		// GLX members (hardware with X11)
		void* m_glxDisplay;
		void* m_glxContext;
		unsigned long m_glxWindow;

		// OSMesa members (CPU fallback)
		void* m_osmesaContext;
		void* m_osmesaBuffer;

		/// Which backend is currently active (set by makeCurrent)
		bool m_usingHardware;
		const char* m_backendName;
		std::string m_rendererString;

		/// Previous GL context state (saved before makeCurrent, restored by release)
		void* m_savedDisplay;
		void* m_savedDrawable;
		void* m_savedContext;
		void* m_savedEglContext;

		/// Reentrancy counter; makeCurrent/release are reference-counted so that
		/// calls from the OFX plugin (e.g. ClipInstance::loadTexture) don't
		/// overwrite the outer saved state.
		int m_makeCurrentCount;

		std::vector<unsigned int> m_textures;

		// Current output FBO + texture (set before GL render, used by ClipInstance::loadTexture)
		unsigned int m_outputFBO = 0;
		unsigned int m_outputTex = 0;
		int m_outputTexW = 0;
		int m_outputTexH = 0;
};

} // GafferOFX
