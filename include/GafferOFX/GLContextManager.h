#pragma once

#include <vector>

namespace GafferOFX
{

/// Manages offscreen GLX/OSMesa contexts for OpenGL-based OFX rendering.
/// Tries GLX first (hardware GPU); falls back to OSMesa if GLX is software-only.
class GLContextManager
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

	private :

		GLContextManager();
		GLContextManager( const GLContextManager& ) = delete;
		GLContextManager& operator=( const GLContextManager& ) = delete;

		/// Try to initialize GLEW and validate. Returns true if GL is usable.
		bool initGLEW();

		// GLX members (preferred)
		void* m_glxDisplay;
		void* m_glxContext;
		unsigned long m_glxWindow;

		// OSMesa members (fallback)
		void* m_osmesaContext;
		void* m_osmesaBuffer;

		/// Which backend is currently active (set by makeCurrent)
		bool m_usingOSMesa;

		/// Previous GL context state (saved before makeCurrent, restored by release)
		void* m_savedDisplay;
		void* m_savedDrawable;
		void* m_savedContext;

		/// Reentrancy counter; makeCurrent/release are reference-counted so that
		/// calls from the OFX plugin (e.g. ClipInstance::loadTexture) don't
		/// overwrite the outer saved state.
		int m_makeCurrentCount;

		std::vector<unsigned int> m_textures;
};

} // GafferOFX
