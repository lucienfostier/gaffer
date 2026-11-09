//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2026, Lucien Fostier. All rights reserved.
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
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace GafferOFX
{

/// Manages the shared offscreen GL context used for OpenGL-based OFX
/// rendering. Linux uses EGL with device enumeration: the GPU is
/// preferred, and the software device is kept as a headless fallback.
/// The context is created with a 1x1 pbuffer so FBO 0 is valid for
/// plugins that render to the default framebuffer.
class GAFFEROFX_API GLContextManager
{
	public :

		/// Render device preference, applied per node. CPU renders through
		/// the software EGL device and GPU forces the hardware device. Auto
		/// prefers the GPU and falls back to the software device when no
		/// hardware device initialises. The rebuild happens lazily in
		/// `makeCurrent()`, and the last node to render wins.
		enum class RenderMode
		{
			Auto = 0,
			CPU = 1,
			GPU = 2
		};

		static GLContextManager& instance();

		~GLContextManager();

		/// Makes the offscreen context current on the calling thread.
		/// Returns true on success. If a render mode requesting a different
		/// device class is set, the context is rebuilt first. Failures log
		/// once per transition rather than once per render, including the
		/// failing step, so the cause of a lost context is identifiable.
		bool makeCurrent();

		/// Sets the device-class preference (see `RenderMode`). The rebuild
		/// happens lazily inside the next `makeCurrent()` that owns the
		/// context, so this is safe to call from any thread.
		void setRenderMode( int mode ) { m_renderMode.store( mode ); }
		/// The currently requested render mode.
		int renderMode() const { return m_renderMode.load(); }

		/// No-op by design: the context stays bound on the render worker
		/// persistently, because unbinding per render would drop the
		/// plugin's GL state. Teardown happens in `teardownContext()` on
		/// rebuild and in the destructor.
		void release();

		/// Registers a texture ID for later cleanup. Duplicate IDs are
		/// ignored.
		void registerTexture( unsigned int id );
		/// Removes a texture ID from the cleanup registry. Call this before
		/// deleting a texture that was registered here.
		void unregisterTexture( unsigned int id );
		/// Cleans up all registered textures.
		void cleanupTextures();

		/// Returns the active backend name ("EGL" on Linux; "none" when no
		/// context is available). Returns by value because worker-thread
		/// rebuilds may change the backend.
		std::string backendName() const;

		/// Returns the GL_RENDERER string of the active context by value.
		/// Worker-thread rebuilds reassign the member while compute threads
		/// may read it, for example in failure logs.
		std::string rendererString() const
		{
			std::lock_guard<std::recursive_mutex> lock( m_mutex );
			return m_rendererString;
		}

		/// Returns true when a hardware GPU was selected at startup. Auto
		/// prefers hardware and only falls back to the software device when
		/// no GPU initialises, so a forced-software run with
		/// `LIBGL_ALWAYS_SOFTWARE=1` reports no hardware.
		bool hardwareAvailable() const;

		// GL function pointers loaded by `initGLFunctions()` through
		// `eglGetProcAddress()`, which is the only way to obtain GL entry
		// points with EGL. Fetching them from the current context rather
		// than linking directly against libGL routes through the driver's
		// own dispatch: direct links can pick up the software driver's stub
		// exports, which are invalid inside a real vendor context. Pointers
		// are re-fetched when the context is rebuilt onto a different
		// device, where the per-vendor dispatch differs.
		static unsigned int (*glGenFramebuffersF)( unsigned int n, unsigned int *ids );
		static void (*glBindFramebufferF)( unsigned int target, unsigned int fbo );
		static void (*glFramebufferTexture2DF)( unsigned int target, unsigned int attachment, unsigned int textarget, unsigned int texture, int level );
		static unsigned int (*glCheckFramebufferStatusF)( unsigned int target );
		static void (*glDeleteFramebuffersF)( unsigned int n, const unsigned int *ids );

		/// Sets the current output FBO and texture for GL rendering. The
		/// texture is the colour attachment of the FBO; plugins that call
		/// `loadTexture("Output")` receive this texture as their render
		/// target.
		void setOutputFBO( unsigned int fbo, unsigned int tex, int width, int height );
		/// Returns the current output FBO. Its colour attachment is the
		/// render texture.
		unsigned int outputFBO() const { return m_outputFBO; }
		/// Returns the current output texture, which is the plugin's render
		/// target.
		unsigned int outputTexture() const { return m_outputTex; }
		/// Returns the current output texture dimensions.
		int outputTexWidth() const { return m_outputTexW; }
		int outputTexHeight() const { return m_outputTexH; }

		/// Incremented every time the context is torn down and rebuilt.
		/// Callers caching GL object IDs such as textures and FBOs must
		/// invalidate them when this value changes, because the old IDs no
		/// longer refer to valid objects after the rebuild.
		int contextGeneration() const { return m_contextGeneration.load(); }

	private :

		GLContextManager();
		GLContextManager( const GLContextManager& ) = delete;
		GLContextManager& operator=( const GLContextManager& ) = delete;

		// Serialises `makeCurrent()` and teardown, together with all GL
		// object bookkeeping. Recursive, because `cleanupTextures()` and
		// `setOutputFBO()` call `makeCurrent()` while holding it.
		// `makeCurrent()` runs on both the render worker and the UI thread.
		mutable std::recursive_mutex m_mutex;

		/// Selects a GL context. `softwareOnly` selects the software
		/// device and `hardwareOnly` selects a GPU EGL device. If both are
		/// false, the default selection applies: GPU preferred, software
		/// fallback and the default display. Teardown of any previous
		/// context is the caller's responsibility.
		bool createContext( bool softwareOnly, bool hardwareOnly );

		/// Destroys the current EGL display, context and surface, and does
		/// nothing if none exists. Unbinds on the calling thread first. If
		/// another thread owns the context, this does not steal it; it only
		/// destroys the objects.
		void teardownContext();

		/// Validates the active context and loads the GL entry points used
		/// by the render path through `eglGetProcAddress()` rather than
		/// GLEW. Returns true if GL is usable.
		bool initGLFunctions();

		// Linux EGL members: the selected device and the pbuffer that keeps
		// FBO 0 valid.
		void* m_eglDisplay;
		void* m_eglContext;
		void* m_eglSurface;

		/// The backend that is currently active, set by `makeCurrent()`.
		const char* m_backendName;
		std::string m_rendererString;

		/// True when the active context was created on a software device.
		/// Used to detect when a rebuild is required.
		bool m_contextIsSoftware = false;

		/// Device class selected at startup (see `hardwareAvailable()`).
		bool m_initialContextAvailable = false;
		bool m_initialContextIsSoftware = false;

		/// Requested device class (see `RenderMode`), stored as an int for
		/// lock-free access.
		std::atomic<int> m_renderMode{ (int)RenderMode::Auto };

		/// The thread that first bound the context, set by `makeCurrent()`.
		/// Binding from another thread reports EGL_BAD_ACCESS and indicates
		/// a dispatch bug.
		std::thread::id m_ownerThread;

		bool makeCurrentFailed( const char *reason );
		bool makeCurrentSucceeded();
		bool m_lastMakeCurrentResult = true;
		// Latched when a software-only build finds no software device.
		// All access under m_mutex.
		bool m_softwareUnavailable = false;

		// Last createContext failure step (device or fallback path).
		// Reported when construction ends without a context.
		std::string m_lastCreateError;
		// One-time EGL entry-point/device census for the same report.
		std::string m_enumerationSummary;
		// Throttle for createContext retries after transient init
		// failures (see makeCurrent).
		std::chrono::steady_clock::time_point m_lastCreateAttempt{};

		std::vector<unsigned int> m_textures;

		// Current output FBO and texture, set before a GL render and used
		// by `ClipInstance::loadTexture()`.
		unsigned int m_outputFBO = 0;
		unsigned int m_outputTex = 0;
		int m_outputTexW = 0;
		int m_outputTexH = 0;

		/// Incremented on every teardown, which invalidates cached GL object
		/// IDs.
		std::atomic<int> m_contextGeneration{ 0 };
};

} // GafferOFX
