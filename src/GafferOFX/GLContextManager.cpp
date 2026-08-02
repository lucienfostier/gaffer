//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2026, Lucien Fostier. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
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

#include "GafferOFX/GLContextManager.h"

#include "IECore/MessageHandler.h"

#if defined(__APPLE__) || defined(_WIN32)
	// The macOS (CGL) and Windows (WGL) backends are not implemented yet.
	// The Linux path below is the only backend. Other platforms would
	// mirror it with the CGL and WGL APIs and their own GL function
	// lookup; see include/GafferOFX/GLContextManager.h for the design.
#error "GLContextManager: only the Linux (EGL) backend is implemented"
#endif

#include <GL/gl.h>
#include <GL/glext.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

// Not present in older EGL headers: VFX2023 with Mesa 21 and EGL 1.4
// only defines the _KHR variant in eglext.h. VFX2025 with Mesa 23 and
// EGL 1.5 defines it in egl.h, where this guard is a no-op. It is kept
// for VFX2023 compatibility.
#ifndef EGL_CONTEXT_OPENGL_PROFILE_MASK
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#endif
#ifndef EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT 0x00000002
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace GafferOFX;

GLContextManager& GLContextManager::instance()
{
	static GLContextManager mgr;
	return mgr;
}

GLContextManager::GLContextManager()
{
	m_eglDisplay = nullptr;
	m_eglContext = nullptr;
	m_eglSurface = nullptr;
	m_backendName = "none";
	m_contextIsSoftware = false;

	// Honor the LIBGL_ALWAYS_SOFTWARE=1 convention: only try software
	// devices and skip the default-display fallback, so the GL path can
	// be exercised without a hardware GPU.
	const char *softwareEnv = getenv( "LIBGL_ALWAYS_SOFTWARE" );
	createContext( softwareEnv && softwareEnv[0] == '1', false );

	// Auto selection prefers hardware and only falls back to the software
	// device when no GPU initialises, so the startup device class is the
	// machine's capability signal (a forced-software run reports no
	// hardware). Record whether a context exists at all, so a failed
	// construction does not claim hardware support.
	m_initialContextAvailable = ( m_eglContext != nullptr );
	m_initialContextIsSoftware = m_contextIsSoftware;

	// Construction outcome is the baseline for every later failure:
	// a session that starts without a context fails all GL renders.
	if( m_eglContext )
	{
		IECore::msg( IECore::Msg::Info, "GafferOFX::GLContextManager",
			std::string( "initial context: " ) + m_rendererString );
	}
	else
	{
		IECore::msg( IECore::Msg::Warning, "GafferOFX::GLContextManager",
			std::string( "initial context: none (" ) + m_lastCreateError + "; " +
			m_enumerationSummary + ") - all GL renders will fail" );
	}
}

bool GLContextManager::hardwareAvailable() const
{
	return m_initialContextAvailable && !m_initialContextIsSoftware;
}

bool GLContextManager::createContext( bool softwareOnly, bool hardwareOnly )
{
	// Create the shared offscreen context from a concrete EGL device
	// rather than EGL_DEFAULT_DISPLAY, which resolves to whichever device
	// the platform registers first, often the software device. Enumerating
	// devices lets us prefer hardware and record whether the context is
	// hardware or software, which the render-mode rebuild depends on.
	//
	// Devices are tried in two passes, hardware then software, so a
	// working GPU takes precedence. For each candidate we create a
	// display, choose an RGBA8 configuration with depth and stencil,
	// request a compatibility profile (many OFX plugins rely on
	// fixed-function GL), create a 1x1 pbuffer so FBO 0 is valid, make
	// the context current and probe the renderer string. A device that
	// fails any step is torn down and the next one is tried. If
	// enumeration produces no context, we fall back to the default
	// display unless a hardware-only context was requested.
	//
	// The context is unbound before returning, so no thread owns it at
	// rest; `makeCurrent()` binds it on the thread that renders.

	// Fetch the EGL extension entry points through `eglGetProcAddress()`.
	// They are exported only by the vendor driver chosen by the loader,
	// not by the linked GLVND libEGL.
	PFNEGLQUERYDEVICESEXTPROC queryDevices = (PFNEGLQUERYDEVICESEXTPROC)
		eglGetProcAddress( "eglQueryDevicesEXT" );
	PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
		eglGetProcAddress( "eglGetPlatformDisplayEXT" );
	PFNEGLQUERYDEVICESTRINGEXTPROC queryDeviceString = (PFNEGLQUERYDEVICESTRINGEXTPROC)
		eglGetProcAddress( "eglQueryDeviceStringEXT" );

	{
		// Report the entry points once: under a Qt-loaded GUI they can
		// resolve differently, or not at all, compared with headless, which
		// changes which devices are attempted.
		static bool logged = false;
		if( !logged )
		{
			logged = true;
			EGLint numDevices = -1;
			if( queryDevices && getPlatformDisplay )
			{
				EGLDeviceEXT devices[16];
				if( !queryDevices( 16, devices, &numDevices ) )
				{
					numDevices = -1;
				}
			}
			m_enumerationSummary =
				std::string( "queryDevices=" ) + ( queryDevices ? "yes" : "no" ) +
				" platformDisplay=" + ( getPlatformDisplay ? "yes" : "no" ) +
				" deviceString=" + ( queryDeviceString ? "yes" : "no" ) +
				" devices=" + std::to_string( numDevices );
			IECore::msg( IECore::Msg::Info, "GafferOFX::GLContextManager",
				std::string( "EGL enumeration: " ) + m_enumerationSummary );
		}
	}
	m_lastCreateError.clear();

	// The framebuffer config is shared by every device attempt: 8-bit
	// RGBA + 24-bit depth + 8-bit stencil, and must be renderable to a
	// pbuffer (offscreen) surface.
	const EGLint configAttribs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};
	// A 1x1 pbuffer provides the minimum backing store needed to make
	// FBO 0 valid, because some plugins render to the default framebuffer
	// rather than to a host FBO. `EGL_NONE` terminates the attribute list,
	// as in `configAttribs`.
	const EGLint pbAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };

	// Device enumeration is optional because some platforms lack the
	// extension. Without it, we fall through to the default-display path
	// below.
	if( !( queryDevices && getPlatformDisplay ) )
	{
		m_lastCreateError = "no device enumeration entry points";
	}
	if( queryDevices && getPlatformDisplay )
	{
		const EGLint maxDevices = 16;
		EGLDeviceEXT devices[maxDevices];
		EGLint numDevices = 0;
		if( queryDevices( maxDevices, devices, &numDevices ) && numDevices > 0 )
		{
			// Two passes: pass 0 tries hardware only and pass 1 tries software
			// only. The loop conditions also check `m_eglContext`, so the first
			// successful device ends every remaining attempt.
			const int firstPass = softwareOnly ? 1 : 0;
			const int lastPass = hardwareOnly ? 0 : 1;
			for( int pass = firstPass; pass <= lastPass && !m_eglContext; ++pass )
			{
				for( EGLint i = 0; i < numDevices && !m_eglContext; ++i )
				{
					// Classify the device from its extension string so
					// each pass only tries devices of its own class.
					if( queryDeviceString )
					{
						const char *exts = queryDeviceString( devices[i], EGL_EXTENSIONS );
						bool isSoftware = exts && strstr( exts, "EGL_MESA_device_software" );
						if( ( pass == 0 && isSoftware ) || ( pass == 1 && !isSoftware ) )
						{
							continue;
						}
					}
					else if( pass == 1 )
					{
						continue;   // can't identify software - skip pass 1
					}

					// Create a display handle bound to this specific
					// device, then initialise it. Every failure below
					// tears down whatever was created and tries the next
					// device, so a broken vendor driver cannot abort the
					// whole selection.
					EGLDisplay dpy = getPlatformDisplay(
						EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr
					);
					if( dpy == EGL_NO_DISPLAY )
					{
						m_lastCreateError = "device: no display";
						continue;
					}

					EGLint major, minor;
					if( !eglInitialize( dpy, &major, &minor ) )
					{
						m_lastCreateError = "device: eglInitialize failed";
						eglTerminate( dpy );
						continue;
					}

					EGLConfig config;
					EGLint numConfigs;
					if( !eglChooseConfig( dpy, configAttribs, &config, 1, &numConfigs ) ||
					numConfigs == 0 )
					{
						m_lastCreateError = "device: no fb config";
						eglTerminate( dpy );
						continue;
					}

					// Create the GL context on this display.
					eglBindAPI( EGL_OPENGL_API );

					// Request an OpenGL compatibility profile so legacy GL
					// queries such as glGetString(GL_EXTENSIONS) and
					// fixed-function constructs work. OFX plugins from the
					// GL 2.1 era depend on this.
					const EGLint ctxAttribs[] = {
						EGL_CONTEXT_OPENGL_PROFILE_MASK,
						EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
						EGL_NONE
					};
					EGLContext ctx = eglCreateContext( dpy, config, EGL_NO_CONTEXT, ctxAttribs );
					if( ctx == EGL_NO_CONTEXT )
					{
						m_lastCreateError = "device: context creation failed";
						eglTerminate( dpy );
						continue;
					}
					// The context needs a surface to be made current
					// against; the pbuffer supplies it offscreen.
					EGLSurface surf = eglCreatePbufferSurface( dpy, config, pbAttribs );
					if( surf == EGL_NO_SURFACE )
					{
						m_lastCreateError = "device: pbuffer creation failed";
						eglDestroyContext( dpy, ctx );
						eglTerminate( dpy );
						continue;
					}

					// Make the context current, then probe the renderer
					// string as the acceptance test for this device. A
					// context that cannot be bound, or that reports no
					// renderer, is torn down and the next device is tried.
					if( eglMakeCurrent( dpy, surf, surf, ctx ) )
					{
						auto getStringFn = (const GLubyte* (*)(GLenum))eglGetProcAddress( "glGetString" );
						const char *renderer = getStringFn ? (const char*)getStringFn( GL_RENDERER ) : nullptr;
						if( !getStringFn )
						{
							m_lastCreateError = "device: no glGetString";
						}
						else if( !renderer || !renderer[0] )
						{
							m_lastCreateError = "device: empty renderer string";
						}
						if( renderer && renderer[0] )
						{
							m_eglDisplay = (void*)dpy;
							m_eglContext = (void*)ctx;
							m_eglSurface = (void*)surf;
							m_rendererString = renderer;
							m_backendName = "EGL";

							// Device accepted. Load the GL entry points
							// we need (FBO functions) and keep the
							// context only if they resolve.
							if( initGLFunctions() )
							{
								// Probe succeeded. Unbind both surfaces to
								// EGL_NO_SURFACE before releasing the context,
								// as EGL requires when the context is
								// EGL_NO_CONTEXT.
								eglMakeCurrent( dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
								break;	// device accepted - stop searching
							}
							else
							{
								// GL is unusable after all, so discard this
								// device's objects and try the next one.
								// Continue explicitly, because the shared
								// cleanup below would destroy them twice.
								eglMakeCurrent( dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
								eglDestroySurface( dpy, surf );
								eglDestroyContext( dpy, ctx );
								eglTerminate( dpy );
								m_eglDisplay = nullptr;
								m_eglContext = nullptr;
								m_eglSurface = nullptr;
								continue;
							}
						}
						// Renderer probe failed - unbind and fall through
						// to the cleanup below.
						eglMakeCurrent( dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
					}

					eglDestroySurface( dpy, surf );
					eglDestroyContext( dpy, ctx );
					eglTerminate( dpy );
				}
			}
		}
	}

	// Fallback: if device enumeration did not produce a working context,
	// try the default EGL display, whatever is registered as the primary
	// display. This is skipped for an explicit GPU request, because the
	// default display could be software.
	if( !m_eglContext && !hardwareOnly )
	{
		EGLDisplay eglDpy = eglGetDisplay( EGL_DEFAULT_DISPLAY );
		if( eglDpy == EGL_NO_DISPLAY )
		{
			eglDpy = eglGetDisplay( nullptr );
		}

		if( eglDpy == EGL_NO_DISPLAY )
		{
			m_lastCreateError = "fallback: no display";
		}
		if( eglDpy != EGL_NO_DISPLAY )
		{
			EGLint major, minor;
			if( !eglInitialize( eglDpy, &major, &minor ) )
			{
				m_lastCreateError = "fallback: eglInitialize failed";
			}
			else
			{
				EGLConfig eglConfig;
				EGLint numConfigs;
				if( eglChooseConfig( eglDpy, configAttribs, &eglConfig, 1, &numConfigs ) &&
				numConfigs > 0 )
				{
					eglBindAPI( EGL_OPENGL_API );
					// Same compatibility-profile context as the device
					// path - legacy OFX plugin GL needs it.
					const EGLint fallbackCtxAttribs[] = {
						EGL_CONTEXT_OPENGL_PROFILE_MASK,
						EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
						EGL_NONE
					};
					EGLContext eglCtx = eglCreateContext( eglDpy, eglConfig, EGL_NO_CONTEXT, fallbackCtxAttribs );
					if( eglCtx != EGL_NO_CONTEXT )
					{
						EGLSurface surf = eglCreatePbufferSurface( eglDpy, eglConfig, pbAttribs );
						if( surf != EGL_NO_SURFACE )
						{
							// Acceptance probe, same as the device path:
							// the context counts only if it binds and
							// reports a renderer string.
							if( eglMakeCurrent( eglDpy, surf, surf, eglCtx ) )
							{
								auto getStringFn = (const GLubyte* (*)(GLenum))eglGetProcAddress( "glGetString" );
								const char *renderer = getStringFn ? (const char*)getStringFn( GL_RENDERER ) : nullptr;
								if( renderer && renderer[0] )
								{
									// A software-only request must not accept
									// a hardware fallback (e.g. NVIDIA-only
									// GLVND setups where the default display
									// is a GPU).
									const bool fallbackIsSoftware =
										strstr( renderer, "llvmpipe" ) != nullptr ||
										strstr( renderer, "softpipe" ) != nullptr ||
										strstr( renderer, "swrast" ) != nullptr ||
										strstr( renderer, "Software" ) != nullptr;
									if( softwareOnly && !fallbackIsSoftware )
									{
										m_lastCreateError = "fallback: default display is not software";
									}
									else
									{
										m_eglDisplay = (void*)eglDpy;
										m_eglContext = (void*)eglCtx;
										m_eglSurface = (void*)surf;
										m_rendererString = renderer;
										m_backendName = "EGL";

										// Apply the same acceptance test as the
										// enumerated-device path. A fallback
										// context without usable GL entry points
										// must be rejected now, not on the first
										// render.
										if( !initGLFunctions() )
										{
											m_lastCreateError = "fallback: GL functions failed";
											m_eglDisplay = nullptr;
											m_eglContext = nullptr;
											m_eglSurface = nullptr;
											m_rendererString.clear();
											m_backendName = "none";
										}
									}
								}
								// Unbind so no thread owns it at rest.
								eglMakeCurrent( eglDpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
							}
							if( !m_eglContext )
							{
								eglDestroySurface( eglDpy, surf );
								eglDestroyContext( eglDpy, eglCtx );
								eglTerminate( eglDpy );
							}
						}
						else if( !m_eglContext )
						{
							eglDestroyContext( eglDpy, eglCtx );
							eglTerminate( eglDpy );
						}
					}
				}
			}
		}
	}

	if( !m_eglContext && m_lastCreateError.compare( 0, 9, "fallback:" ) != 0 &&
	m_lastCreateError.compare( 0, 7, "device:" ) != 0 )
	{
		// Device path untouched (or hardware-only request skipped the
		// fallback) and no step recorded - the fallback failed inside
		// config/context/surface/probe nesting.
		m_lastCreateError = "fallback: config, context, surface or probe failed";
	}

	if( m_eglContext )
	{
		m_contextIsSoftware =
			strstr( m_rendererString.c_str(), "llvmpipe" ) != nullptr ||
			strstr( m_rendererString.c_str(), "softpipe" ) != nullptr ||
			strstr( m_rendererString.c_str(), "swrast" ) != nullptr ||
			strstr( m_rendererString.c_str(), "Software" ) != nullptr;
	}

	return m_eglContext != nullptr;
}

void GLContextManager::teardownContext()
{
	std::lock_guard<std::recursive_mutex> lock( m_mutex );
	if( m_eglDisplay )
	{
		// Best-effort unbind on the calling thread. If another thread owns the
		// context, this fails silently and we destroy the objects regardless;
		// the owning thread must not touch them afterwards.
		eglMakeCurrent( (EGLDisplay)m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
		if( m_eglSurface )
		{
			eglDestroySurface( (EGLDisplay)m_eglDisplay, (EGLSurface)m_eglSurface );
		}
		eglDestroyContext( (EGLDisplay)m_eglDisplay, (EGLContext)m_eglContext );
		eglTerminate( (EGLDisplay)m_eglDisplay );
	}
	m_eglDisplay = nullptr;
	m_eglContext = nullptr;
	m_eglSurface = nullptr;
	m_contextIsSoftware = false;

	// GL objects created on the destroyed context are no longer valid.
	// Invalidate everything that caches their IDs so the next render
	// recreates them.
	m_outputFBO = 0;
	m_outputTex = 0;
	m_outputTexW = 0;
	m_outputTexH = 0;
	m_textures.clear();
	++m_contextGeneration;
}

GLContextManager::~GLContextManager()
{
	// Skip GL cleanup if not on the owning thread. The context is not
	// current here, so glDeleteTextures and glDeleteFramebuffers would be
	// silent no-ops and leak GPU memory. At process exit the driver
	// reclaims everything; at module unload the worker has already been
	// joined.
	if( m_ownerThread != std::thread::id() &&
	m_ownerThread != std::this_thread::get_id() )
	{
		// Let the worker's textures leak - driver cleanup at exit.
		m_textures.clear();
		m_outputFBO = 0;
		m_outputTex = 0;
	}
	else
	{
		cleanupTextures();
	}

	if( m_eglContext )
	{
		teardownContext();
	}
}

bool GLContextManager::initGLFunctions()
{
	// `glewInit()` is deliberately not called. Under RDP, or any X server
	// without the NVIDIA GLX driver, Qt's GLX initialisation loads
	// libGLX_mesa.so.0 before our code runs. That library's GL* exports
	// capture global-scope symbol bindings for all subsequently loaded
	// modules. GLEW resolves through those captured bindings, and its stub
	// sees no current context under our EGL setup. Loading through
	// `eglGetProcAddress()` routes directly to the NVIDIA EGL driver and
	// avoids the global-scope GLX capture.

	auto getStringFn = (const GLubyte* (*)(GLenum))eglGetProcAddress( "glGetString" );
	const char *renderer = getStringFn ? (const char*)getStringFn( GL_RENDERER ) : nullptr;
	const char *versionStr = getStringFn ? (const char*)getStringFn( GL_VERSION ) : nullptr;
	if( !renderer )
	{
		return false;
	}

	// Re-fetch the FBO entry points whenever the driver dispatch changes.
	// The context can be rebuilt onto a different device (e.g. GPU ->
	// software device), where the per-vendor function pointers differ.
	static std::string s_dispatchRenderer;
	if( s_dispatchRenderer != m_rendererString )
	{
		glGenFramebuffersF = nullptr;
		glBindFramebufferF = nullptr;
		glFramebufferTexture2DF = nullptr;
		glCheckFramebufferStatusF = nullptr;
		glDeleteFramebuffersF = nullptr;
		s_dispatchRenderer = m_rendererString;
	}

	bool isHardware = !strstr( renderer, "llvmpipe" ) && !strstr( renderer, "softpipe" ) &&
		!strstr( renderer, "swrast" ) && !strstr( renderer, "Software" );

	if( isHardware )
	{
		int major = 0, minor = 0;
		if( versionStr )
		{
			sscanf( versionStr, "%d.%d", &major, &minor );
		}
		if( major < 3 || ( major == 3 && minor < 2 ) )
		{
			return false;
		}
	}

	// Load the FBO entry points via eglGetProcAddress (not GLEW), so the
	// same calls work on every backend. OFX plugins that bind FBO 0 (the
	// pbuffer-backed default framebuffer) during rendering rely on these.
	if( !glGenFramebuffersF )
	{
		glGenFramebuffersF = (unsigned int (*)( unsigned int, unsigned int* ))
			eglGetProcAddress( "glGenFramebuffers" );
	}
	if( !glBindFramebufferF )
	{
		glBindFramebufferF = (void (*)( unsigned int, unsigned int ))
			eglGetProcAddress( "glBindFramebuffer" );
	}
	if( !glFramebufferTexture2DF )
	{
		glFramebufferTexture2DF = (void (*)( unsigned int, unsigned int, unsigned int, unsigned int, int ))
			eglGetProcAddress( "glFramebufferTexture2D" );
	}
	if( !glCheckFramebufferStatusF )
	{
		glCheckFramebufferStatusF = (unsigned int (*)( unsigned int ))
			eglGetProcAddress( "glCheckFramebufferStatus" );
	}
	if( !glDeleteFramebuffersF )
	{
		glDeleteFramebuffersF = (void (*)( unsigned int, const unsigned int* ))
			eglGetProcAddress( "glDeleteFramebuffers" );
	}

	if( !glGenFramebuffersF || !glBindFramebufferF || !glFramebufferTexture2DF ||
	!glCheckFramebufferStatusF || !glDeleteFramebuffersF )
	{
		return false;
	}

	return true;
}

// Static GL function pointer definitions
unsigned int (*GLContextManager::glGenFramebuffersF)( unsigned int, unsigned int* ) = nullptr;
void (*GLContextManager::glBindFramebufferF)( unsigned int, unsigned int ) = nullptr;
void (*GLContextManager::glFramebufferTexture2DF)( unsigned int, unsigned int, unsigned int, unsigned int, int ) = nullptr;
unsigned int (*GLContextManager::glCheckFramebufferStatusF)( unsigned int ) = nullptr;
void (*GLContextManager::glDeleteFramebuffersF)( unsigned int, const unsigned int* ) = nullptr;

bool GLContextManager::makeCurrentFailed( const char *reason )
{
	// Log transitions only. A lost context would otherwise log on every
	// render, while a single state change identifies when GL was lost or
	// regained.
	if( m_lastMakeCurrentResult )
	{
		IECore::msg( IECore::Msg::Warning, "GafferOFX::GLContextManager",
			std::string( "makeCurrent failed (" ) + reason + ")" );
	}
	m_lastMakeCurrentResult = false;
	return false;
}

bool GLContextManager::makeCurrentSucceeded()
{
	if( !m_lastMakeCurrentResult )
	{
		IECore::msg( IECore::Msg::Info, "GafferOFX::GLContextManager",
			"makeCurrent recovered" );
	}
	m_lastMakeCurrentResult = true;
	return true;
}

bool GLContextManager::makeCurrent()
{
	std::lock_guard<std::recursive_mutex> lock( m_mutex );
	if( !m_eglContext )
	{
		// Construction-time initialisation can fail transiently, for example
		// when racing Qt GL setup in a GUI session. A single failure must not
		// disable the session: retry creation at a throttled interval, then
		// fall through to the normal bind below on success.
		const auto now = std::chrono::steady_clock::now();
		if( now - m_lastCreateAttempt > std::chrono::seconds( 5 ) )
		{
			m_lastCreateAttempt = now;
			createContext( false, false );
		}
		if( !m_eglContext )
		{
			m_backendName = "none";
			return makeCurrentFailed( "null context" );
		}
	}

	// Rebuild the context if the requested device class differs from the
	// active one. Unbind-on-this-thread only succeeds if we own it; if
	// another thread currently holds the context we defer the rebuild to
	// that thread's next makeCurrent.
	const int requestedMode = renderMode();
	const char *softwareEnv = getenv( "LIBGL_ALWAYS_SOFTWARE" );
	const bool forceSoftware = softwareEnv && softwareEnv[0] == '1';

	// CPU render through the software EGL device.
	if( requestedMode == (int)RenderMode::CPU )
	{
		// Once software has proven unavailable, skip the teardown cycle
		// instead of rebuilding and failing on every render.
		if( !m_contextIsSoftware && !m_softwareUnavailable )
		{
			if( !eglMakeCurrent( (EGLDisplay)m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT ) )
			{
				return makeCurrentFailed( "CPU unbind failed" );
			}
			teardownContext();
			if( !createContext( true, false ) )
			{
				// No software device exists. Remember this so later renders skip the
				// teardown cycle instead of rebuilding on every render.
				m_softwareUnavailable = true;
				// Restore the default selection rather than leaving GL
				// disabled for the rest of the process (mirrors the
				// GPU/Auto branches).
				createContext( false, false );
				if( !m_eglContext )
				{
					m_backendName = "none";
				}
				return makeCurrentFailed( "CPU software rebuild failed" );
			}
		}
	}
	else if( requestedMode == (int)RenderMode::GPU )
	{
		// No hardware device on this machine: fall through and bind the
		// current software context (GPU degrades to software here, as in
		// Auto). Returning false would leave kOfxImageEffectPropOpenGLEnabled
		// set from an earlier attach while nothing is bound.
		if( m_contextIsSoftware && !m_initialContextIsSoftware )
		{
			if( !eglMakeCurrent( (EGLDisplay)m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT ) )
			{
				return makeCurrentFailed( "GPU unbind failed" );
			}
			teardownContext();
			if( !createContext( false, true ) )
			{
				// Rebuild failed - restore a usable context rather
				// than leaving GL disabled for the rest of the process.
				createContext( false, false );
				if( !m_eglContext )
				{
					m_backendName = "none";
				}
				return makeCurrentFailed( "GPU rebuild failed" );
			}
		}
	}
	else
	{
		// Auto prefers the GPU and falls back to the software EGL device when
		// no hardware device initialises. Skip the teardown cycle entirely
		// when the machine never had hardware, because it can only fail and
		// force an unnecessary plugin re-initialisation on every render.
		if( m_contextIsSoftware && !forceSoftware && !m_initialContextIsSoftware )
		{
			if( !eglMakeCurrent( (EGLDisplay)m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT ) )
			{
				return makeCurrentFailed( "Auto unbind failed" );
			}
			teardownContext();
			if( !createContext( false, true ) )
			{
				// GPU unavailable - fall back to the software device.
				if( !createContext( true, false ) )
				{
					m_backendName = "none";
					return makeCurrentFailed( "Auto rebuild failed" );
				}
			}
		}
	}

	// Always call eglMakeCurrent. The constructor unbinds before returning,
	// so no thread owns the context at rest. Rebinding the same context on
	// the same thread is a valid no-op per EGL spec (no error).
	EGLDisplay dpy = (EGLDisplay)m_eglDisplay;
	EGLContext ctx = (EGLContext)m_eglContext;
	EGLSurface surf = m_eglSurface ? (EGLSurface)m_eglSurface : EGL_NO_SURFACE;

	EGLBoolean ok = eglMakeCurrent( dpy, surf, surf, ctx );
	if( !ok && eglGetError() == 0x3002 )   // EGL_BAD_ACCESS
	{
		// The context is current on another thread, normally the render
		// worker, so this thread cannot bind it. This is routine and benign,
		// for example when texture cleanup runs on a non-worker thread. Stay
		// silent, leave the transition state untouched and keep the previous
		// backend name for diagnostics. The caller should skip GL work.
		return false;
	}
	else if( ok && initGLFunctions() )
	{
		m_ownerThread = std::this_thread::get_id();
		m_backendName = "EGL";
		return makeCurrentSucceeded();
	}

	m_backendName = "none";
	return makeCurrentFailed( !ok ? "eglMakeCurrent failed" : "GL functions failed" );
}

void GLContextManager::release()
{
	// Worker-owned context: we never unbind. The context stays bound
	// on the worker thread persistently.
}

void GLContextManager::registerTexture( unsigned int id )
{
	if( id == 0 )
	{
		return;
	}
	std::lock_guard<std::recursive_mutex> lock( m_mutex );
	if( std::find( m_textures.begin(), m_textures.end(), id ) == m_textures.end() )
	{
		m_textures.push_back( id );
	}
}

void GLContextManager::unregisterTexture( unsigned int id )
{
	std::lock_guard<std::recursive_mutex> lock( m_mutex );
	m_textures.erase(
		std::remove( m_textures.begin(), m_textures.end(), id ),
		m_textures.end()
	);
}

void GLContextManager::cleanupTextures()
{
	if( !m_textures.empty() )
	{
		// Only delete when bound to our context: deleting while another
		// context is current would destroy that context's objects.
		if( makeCurrent() )
		{
			glDeleteTextures( (GLsizei)m_textures.size(), (const GLuint*)m_textures.data() );
		}
		m_textures.clear();
	}

	if( m_outputFBO || m_outputTex )
	{
		if( !makeCurrent() )
		{
			m_outputFBO = 0;
			m_outputTex = 0;
			m_outputTexW = 0;
			m_outputTexH = 0;
			return;
		}
		if( m_outputFBO && glDeleteFramebuffersF )
		{
			glDeleteFramebuffersF( 1, &m_outputFBO );
		}
		if( m_outputTex )
		{
			glDeleteTextures( 1, &m_outputTex );
		}
		m_outputFBO = 0;
		m_outputTex = 0;
		m_outputTexW = 0;
		m_outputTexH = 0;
	}
}

void GLContextManager::setOutputFBO( unsigned int fbo, unsigned int tex, int width, int height )
{
	std::lock_guard<std::recursive_mutex> lock( m_mutex );
	// Clean up the previous FBO and texture if they differ.
	if( m_outputFBO && m_outputFBO != fbo )
	{
		GLuint oldFbo = m_outputFBO;
		GLuint oldTex = m_outputTex;
		m_outputFBO = 0;
		m_outputTex = 0;
		// Skip deletion when not bound to our context (see cleanupTextures).
		if( makeCurrent() )
		{
			if( glDeleteFramebuffersF )
			{
				glDeleteFramebuffersF( 1, &oldFbo );
			}
			glDeleteTextures( 1, &oldTex );
		}
	}
	m_outputFBO = fbo;
	m_outputTex = tex;
	m_outputTexW = width;
	m_outputTexH = height;
}

std::string GLContextManager::backendName() const
{
	std::lock_guard<std::recursive_mutex> lock( m_mutex );
	return m_backendName;
}
