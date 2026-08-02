//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2025, Lucien Fostier. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above
//       copyright notice, this list of conditions and the following
//       disclaimer.
//
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided with
//       the distribution.
//
//     * Neither the name of Image Engine Design nor the names of any
//       other contributors to this software may be used to endorse or
//       promote products derived from this software without specific prior
//       written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#pragma once

#include "GafferOFX/Export.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace GafferOFX
{

/// Manages the shared offscreen GL context used for OpenGL-based OFX
/// rendering. Linux EGL with device enumeration (GPU preferred, software
/// device kept as a headless fallback). The context is created with a
/// 1x1 pbuffer so FBO 0 is valid for plugins that render to the
/// default framebuffer.
class GAFFEROFX_API GLContextManager
{
	public :

		/// Render device preference, applied per node: CPU renders
		/// through the software EGL device; GPU forces the hardware
		/// device.  Auto prefers the GPU and falls back to the software
		/// device when no hardware device initialises.  The rebuild
		/// happens lazily in makeCurrent() (last node to render wins).
		enum class RenderMode
		{
			Auto = 0,
			CPU = 1,
			GPU = 2
		};

		static GLContextManager& instance();

		~GLContextManager();

		/// Make the offscreen context current on the calling thread.
		/// Returns true on success.  If a render mode requesting a
		/// different device class is set, the context is rebuilt first.
		bool makeCurrent();

		/// Set the device-class preference (see RenderMode).  The rebuild
		/// happens lazily inside the next makeCurrent() that owns the
		/// context, so this is safe to call from any thread.
		void setRenderMode( int mode ) { m_renderMode.store( mode ); }
		/// The currently requested render mode.
		int renderMode() const { return m_renderMode.load(); }

		/// Release the current context.
		void release();

		/// Register a texture ID for later cleanup.
		void registerTexture( unsigned int id );
		/// Clean up all registered textures.
		void cleanupTextures();

		/// Returns the active backend name ("EGL" on Linux; "none" when no
		/// context is available).
		const char* backendName() const;

		/// Returns the GL_RENDERER string of the active context.
		const char* rendererString() const { return m_rendererString.c_str(); }

		/// True when a hardware GPU was selected at startup (Auto prefers
		/// hardware and only falls back to the software device when no GPU
		/// initialises; a LIBGL_ALWAYS_SOFTWARE=1 forced-software run
		/// reports no hardware).
		bool hardwareAvailable() const;

		// GL function pointers loaded by initGLEW() via eglGetProcAddress(),
		// which is the only way to obtain GL entry points with EGL.  Fetching
		// them from the current context rather than linking directly against
		// libGL routes through the driver's own dispatch: direct links can pick
		// up the software driver's stub exports that are invalid inside a real
		// vendor context.  Pointers are re-fetched when the context is rebuilt
		// onto a different device, where the per-vendor dispatch differs.
		static unsigned int (*glGenFramebuffersF)( unsigned int n, unsigned int *ids );
		static void (*glBindFramebufferF)( unsigned int target, unsigned int fbo );
		static void (*glFramebufferTexture2DF)( unsigned int target, unsigned int attachment, unsigned int textarget, unsigned int texture, int level );
		static unsigned int (*glCheckFramebufferStatusF)( unsigned int target );
		static void (*glDeleteFramebuffersF)( unsigned int n, const unsigned int *ids );

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

		/// Incremented every time the context is torn down and rebuilt.
		/// Callers caching GL object IDs (textures, FBOs) must invalidate
		/// them when this value changes — the old IDs are dead after the
		/// rebuild.
		int contextGeneration() const { return m_contextGeneration; }

	private :

		GLContextManager();
		GLContextManager( const GLContextManager& ) = delete;
		GLContextManager& operator=( const GLContextManager& ) = delete;

		/// Select a GL context.  softwareOnly picks the software device;
		/// hardwareOnly picks a GPU EGL device; both false reverts to the
		/// default selection (GPU preferred, software fallback, default
		/// display).  Teardown of any previous context is the caller's
		/// responsibility.
		bool createContext( bool softwareOnly, bool hardwareOnly );

		/// Destroy the current EGL display/context/surface (no-op if none).
		/// Unbinds on the calling thread first; if another thread owns the
		/// context this does not steal it, it just destroys the objects.
		void teardownContext();

		/// Validate the active context and load the GL entry points used by
		/// the render path.  Returns true if GL is usable.
		bool initGLEW();

		// Linux EGL members (GPU device + pbuffer for FBO 0 compatibility)
		void* m_eglDisplay;
		void* m_eglContext;
		void* m_eglSurface;

		/// Which backend is currently active (set by makeCurrent)
		const char* m_backendName;
		std::string m_rendererString;

		/// True when the active context was created on a software device.
		/// Used to detect when a rebuild is required.
		bool m_contextIsSoftware = false;

		/// Device class selected at startup (see hardwareAvailable()).
		bool m_initialContextIsSoftware = false;

		/// Requested device class (see RenderMode); int for lock-free access.
		std::atomic<int> m_renderMode{ (int)RenderMode::Auto };

		/// Thread that first bound the context (set by makeCurrent).
		/// EGL_BAD_ACCESS when binding from another thread is a dispatch bug.
		std::thread::id m_ownerThread;

		std::vector<unsigned int> m_textures;

		// Current output FBO + texture (set before GL render, used by ClipInstance::loadTexture)
		unsigned int m_outputFBO = 0;
		unsigned int m_outputTex = 0;
		int m_outputTexW = 0;
		int m_outputTexH = 0;

		/// Bumped on every teardown; invalidates cached GL object IDs.
		int m_contextGeneration = 0;
};

} // GafferOFX
