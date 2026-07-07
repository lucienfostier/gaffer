//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2025, Lucien Fostier. All rights reserved.
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

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/osmesa.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <X11/Xlib.h>
#include <GL/glx.h>

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
	m_glxDisplay = nullptr;
	m_glxContext = nullptr;
	m_glxWindow = 0;
	m_osmesaContext = nullptr;
	m_osmesaBuffer = nullptr;
	m_usingHardware = false;
	m_backendName = "none";

	// ---- 1. EGL with device enumeration ----
	//
	// Enumerate all EGL devices via EGL_EXT_device_enumeration and try each
	// one via EGL_EXT_platform_device.  Prefer hardware renderers (NVIDIA,
	// AMD, Intel GPU) over software (llvmpipe).  Keep software only as a last
	// resort when no hardware device initializes successfully.

	{
		PFNEGLQUERYDEVICESEXTPROC queryDevices = (PFNEGLQUERYDEVICESEXTPROC)
			eglGetProcAddress( "eglQueryDevicesEXT" );
		PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
			eglGetProcAddress( "eglGetPlatformDisplayEXT" );
		PFNEGLQUERYDEVICESTRINGEXTPROC queryDeviceString = (PFNEGLQUERYDEVICESTRINGEXTPROC)
			eglGetProcAddress( "eglQueryDeviceStringEXT" );

		// Shared config/pbuffer attribs used for all attempts
		const EGLint configAttribs[] = {
			EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
			EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
			EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
			EGL_NONE
		};
		const EGLint pbAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };

			if( queryDevices && getPlatformDisplay )
			{
				const EGLint maxDevices = 16;
				EGLDeviceEXT devices[maxDevices];
				EGLint numDevices = 0;
				if( queryDevices( maxDevices, devices, &numDevices ) && numDevices > 0 )
				{
					std::cerr << "EGL: found " << numDevices << " device(s)" << std::endl;

					// Two passes: pass 0 = hardware only, pass 1 = software only
					for( int pass = 0; pass < 2 && !m_eglContext; ++pass )
					{
						for( EGLint i = 0; i < numDevices && !m_eglContext; ++i )
						{
							// Log per-device info
							if( queryDeviceString )
							{
								const char *drm = queryDeviceString( devices[i], EGL_DRM_DEVICE_FILE_EXT );
								const char *exts = queryDeviceString( devices[i], EGL_EXTENSIONS );
								bool isSoftware = exts && strstr( exts, "EGL_MESA_device_software" );
								std::cerr << "EGL device " << i << ": drm="
								          << ( drm ? drm : "(none)" )
								          << ( isSoftware ? " [software]" : " [hardware]" )
								          << std::endl;
								if( ( pass == 0 && isSoftware ) || ( pass == 1 && !isSoftware ) )
								{
									std::cerr << "  -> skipped (wrong pass)" << std::endl;
									continue;
								}
							}
							else if( pass == 1 )
							{
								continue;   // can't identify software — skip pass 1
							}

							EGLDisplay dpy = getPlatformDisplay(
								EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr
							);
							if( dpy == EGL_NO_DISPLAY )
							{
								std::cerr << "  -> eglGetPlatformDisplayEXT failed" << std::endl;
								continue;
							}
							std::cerr << "  -> display obtained" << std::endl;

							EGLint major, minor;
							if( !eglInitialize( dpy, &major, &minor ) )
							{
								std::cerr << "  -> eglInitialize failed (err="
								          << eglGetError() << ")" << std::endl;
								eglTerminate( dpy );
								continue;
							}
							std::cerr << "  -> initialized EGL " << major << "." << minor
							          << " vendor=" << eglQueryString( dpy, EGL_VENDOR ) << std::endl;

							EGLConfig config;
							EGLint numConfigs;
							if( !eglChooseConfig( dpy, configAttribs, &config, 1, &numConfigs ) ||
							    numConfigs == 0 )
							{
								std::cerr << "  -> eglChooseConfig failed" << std::endl;
								eglTerminate( dpy );
								continue;
							}
							std::cerr << "  -> config chosen" << std::endl;

							eglBindAPI( EGL_OPENGL_API );

							// Request an OpenGL compatibility profile so legacy GL
							// queries (glGetString(GL_EXTENSIONS), etc.) and fixed-
							// function constructs work.  OFX plugins from the GL 2.1
							// era depend on this.
#ifndef EGL_CONTEXT_OPENGL_PROFILE_MASK
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#endif
#ifndef EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT 0x00000002
#endif
							const EGLint ctxAttribs[] = {
								EGL_CONTEXT_OPENGL_PROFILE_MASK,
								EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
								EGL_NONE
							};
							EGLContext ctx = eglCreateContext( dpy, config, EGL_NO_CONTEXT, ctxAttribs );
							if( ctx == EGL_NO_CONTEXT )
							{
								std::cerr << "  -> eglCreateContext failed (err="
								          << eglGetError() << ")" << std::endl;
								eglTerminate( dpy );
								continue;
							}
							std::cerr << "  -> context created" << std::endl;

							EGLSurface surf = eglCreatePbufferSurface( dpy, config, pbAttribs );
							if( surf == EGL_NO_SURFACE )
							{
								std::cerr << "  -> eglCreatePbufferSurface failed (err="
								          << eglGetError() << ")" << std::endl;
								eglDestroyContext( dpy, ctx );
								eglTerminate( dpy );
								continue;
							}
							std::cerr << "  -> pbuffer surface created" << std::endl;

							if( eglMakeCurrent( dpy, surf, surf, ctx ) )
							{
								const char *renderer = (const char*)glGetString( GL_RENDERER );
								if( renderer && renderer[0] )
								{
									std::cerr
										<< "EGL device " << i << " ("
										<< ( pass == 0 ? "hardware pass" : "software pass" )
										<< "): GL_RENDERER = " << renderer << std::endl;
									m_eglDisplay = (void*)dpy;
									m_eglContext = (void*)ctx;
									m_eglSurface = (void*)surf;
									m_rendererString = renderer;
									m_backendName = "EGL";
									m_usingHardware = !strstr( renderer, "llvmpipe" ) &&
									                  !strstr( renderer, "soft" );
									std::cerr << "  -> SELECTED device " << i
									          << " (backend=EGL, hardware="
									          << m_usingHardware << ")" << std::endl;
									if( initGLEW( "EGL" ) )
									{
										// Probe succeeded.  Unbind properly per
										// EGL §3.7.3: both surfaces must be
										// EGL_NO_SURFACE when ctx is EGL_NO_CONTEXT.
										eglMakeCurrent( dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
									}
									else
									{
										std::cerr << "  -> initGLEW failed, discarding device"
										          << std::endl;
										eglMakeCurrent( dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
										eglDestroySurface( dpy, surf );
										eglDestroyContext( dpy, ctx );
										eglTerminate( dpy );
										m_eglDisplay = nullptr;
										m_eglContext = nullptr;
										m_eglSurface = nullptr;
									}
									break;
								}
								std::cerr << "  -> glGetString(GL_RENDERER) returned NULL" << std::endl;
								eglMakeCurrent( dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
							}
							else
							{
								std::cerr << "  -> eglMakeCurrent failed (err="
								          << eglGetError() << ")" << std::endl;
							}

							eglDestroySurface( dpy, surf );
							eglDestroyContext( dpy, ctx );
							eglTerminate( dpy );
						}
					}
				}
				else
				{
					std::cerr << "EGL: queryDevices returned no devices" << std::endl;
				}
			}
			else
			{
				std::cerr << "EGL: platform_device extension not available" << std::endl;
			}

		// Fallback: if device enumeration didn't produce a working context,
		// try the default EGL display (may pick up Mesa's llvmpipe or
		// whatever is registered as the primary display).
		if( !m_eglContext )
		{
			EGLDisplay eglDpy = eglGetDisplay( EGL_DEFAULT_DISPLAY );
			if( eglDpy == EGL_NO_DISPLAY )
				eglDpy = eglGetDisplay( nullptr );

			if( eglDpy != EGL_NO_DISPLAY )
			{
				EGLint major, minor;
				if( eglInitialize( eglDpy, &major, &minor ) )
				{
					EGLConfig eglConfig;
					EGLint numConfigs;
					if( eglChooseConfig( eglDpy, configAttribs, &eglConfig, 1, &numConfigs ) &&
					    numConfigs > 0 )
					{
						eglBindAPI( EGL_OPENGL_API );
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
								if( eglMakeCurrent( eglDpy, surf, surf, eglCtx ) )
								{
									const char *renderer = (const char*)glGetString( GL_RENDERER );
									if( renderer && renderer[0] )
									{
										std::cerr << "EGL fallback (default display): GL_RENDERER = "
										          << renderer << std::endl;
										m_eglDisplay = (void*)eglDpy;
										m_eglContext = (void*)eglCtx;
										m_eglSurface = (void*)surf;
										m_rendererString = renderer;
										m_backendName = "EGL";
										m_usingHardware = !strstr( renderer, "llvmpipe" ) &&
										                  !strstr( renderer, "soft" );
									}
									eglMakeCurrent( eglDpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
								}
								if( !m_eglContext )
									eglDestroySurface( eglDpy, surf );
							}
							if( !m_eglContext )
								eglDestroyContext( eglDpy, eglCtx );
						}
					}
				}
			}
		}
	}

	// ---- 2. GLX path (hardware GPU with X11) ----

	if( !m_eglContext )
	{
		Display *x11dpy = XOpenDisplay( nullptr );
		if( x11dpy )
		{
			int attribs[] = {
				GLX_RGBA,
				GLX_RED_SIZE, 8,
				GLX_GREEN_SIZE, 8,
				GLX_BLUE_SIZE, 8,
				GLX_ALPHA_SIZE, 8,
				GLX_DEPTH_SIZE, 24,
				GLX_STENCIL_SIZE, 8,
				GLX_DOUBLEBUFFER,
				None
			};

			XVisualInfo *vi = glXChooseVisual( x11dpy, DefaultScreen( x11dpy ), attribs );
			if( vi )
			{
				GLXContext ctx = glXCreateContext( x11dpy, vi, 0, GL_TRUE );
				XFree( vi );

				if( ctx )
				{
					Window win = XCreateSimpleWindow(
						x11dpy, RootWindow( x11dpy, DefaultScreen( x11dpy ) ),
						0, 0, 1, 1, 0, 0, 0
					);

					m_glxDisplay = (void*)x11dpy;
					m_glxContext = (void*)ctx;
					m_glxWindow = win;
				}
			}

			if( !m_glxContext )
			{
				XCloseDisplay( x11dpy );
			}
		}
	}

	// ---- 3. OSMesa fallback (CPU software) ----

	OSMesaContext osCtx = OSMesaCreateContextExt( OSMESA_RGBA, 24, 8, 0, nullptr );
	if( osCtx )
	{
		unsigned char *osBuffer = (unsigned char*)malloc( 4 );
		m_osmesaContext = (void*)osCtx;
		m_osmesaBuffer = (void*)osBuffer;
	}

	if( !m_eglContext && !m_glxContext && !m_osmesaContext )
	{
	}
}

GLContextManager::~GLContextManager()
{
	// Skip GL cleanup if not on the owning thread — the context isn't
	// current here, so glDeleteTextures / glDeleteFramebuffers would be
	// silent no-ops (leaked GPU memory).  At process exit the driver
	// reclaims everything anyway; at module unload the worker has already
	// been joined.
	if( m_ownerThread != std::thread::id() &&
	    m_ownerThread != std::this_thread::get_id() )
	{
		// Let the worker's textures leak — driver cleanup at exit.
		m_textures.clear();
		m_outputFBO = 0;
		m_outputTex = 0;
	}
	else
	{
		cleanupTextures();
	}

	if( m_osmesaContext )
	{
		OSMesaDestroyContext( (OSMesaContext)m_osmesaContext );
		free( m_osmesaBuffer );
	}

	if( m_glxContext )
	{
		glXDestroyContext( (Display*)m_glxDisplay, (GLXContext)m_glxContext );
		if( m_glxWindow )
			XDestroyWindow( (Display*)m_glxDisplay, (Window)m_glxWindow );
		XCloseDisplay( (Display*)m_glxDisplay );
	}

	if( m_eglContext )
	{
		if( m_eglSurface )
			eglDestroySurface( (EGLDisplay)m_eglDisplay, (EGLSurface)m_eglSurface );
		eglDestroyContext( (EGLDisplay)m_eglDisplay, (EGLContext)m_eglContext );
		eglTerminate( (EGLDisplay)m_eglDisplay );
	}
}

bool GLContextManager::initGLEW( const char *backendName )
{
	// We do NOT call glewInit() — on NVIDIA EGL it corrupts glGetString
	// (returns NULL for all queries) and invalidates function pointers.
	// Instead we verify GL state directly and load FBO functions manually.

	const char *renderer = (const char*)glGetString( GL_RENDERER );
	const char *versionStr = (const char*)glGetString( GL_VERSION );
	const char *vendor = (const char*)glGetString( GL_VENDOR );

	if( !renderer )
	{
		std::cerr << "initGLEW(" << backendName << "): glGetString(GL_RENDERER) returned NULL"
		          << std::endl;
		return false;
	}

	std::cerr << "initGLEW(" << backendName << "): "
	          << ( vendor ? vendor : "?" ) << " / " << renderer
	          << " / " << ( versionStr ? versionStr : "?" ) << std::endl;

	bool isHardware = !strstr( renderer, "llvmpipe" ) && !strstr( renderer, "soft" );

	if( isHardware )
	{
		int major = 0, minor = 0;
		if( versionStr ) sscanf( versionStr, "%d.%d", &major, &minor );
		if( major < 3 || ( major == 3 && minor < 2 ) )
		{
			std::cerr << "initGLEW(" << backendName << "): hardware GL "
			          << major << "." << minor << " < 3.2" << std::endl;
			return false;
		}
	}

	// Load FBO functions via eglGetProcAddress (not GLEW)
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
		std::cerr << "initGLEW(" << backendName << "): FBO functions not available via eglGetProcAddress"
		          << std::endl;
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

bool GLContextManager::makeCurrent()
{
	// Always call eglMakeCurrent.  The constructor unbinds before returning,
	// so no thread owns the context at rest.  Rebinding the same context on
	// the same thread is a valid no-op per EGL spec (no error).

	// ---- 1. EGL (preferred — GPU + surfaceless) ----

	if( m_eglContext )
	{
		EGLDisplay dpy = (EGLDisplay)m_eglDisplay;
		EGLContext ctx = (EGLContext)m_eglContext;
		EGLSurface surf = m_eglSurface ? (EGLSurface)m_eglSurface : EGL_NO_SURFACE;

		EGLBoolean ok = eglMakeCurrent( dpy, surf, surf, ctx );
		if( !ok )
		{
			EGLint err = eglGetError();
			std::cerr << "makeCurrent: eglMakeCurrent failed (err=0x" << std::hex << err
			          << std::dec << ")";
			if( err == 0x3002 )  // EGL_BAD_ACCESS
			{
				std::cerr << " — context owned by thread "
				          << ( m_ownerThread != std::thread::id()
				               ? std::to_string( *(uint64_t*)&m_ownerThread )
				               : "(none)" )
				          << "; GL work dispatched to wrong thread?" << std::endl;
				return false;  // Dispatch bug — do NOT fall back to GLX/OSMesa
			}
			std::cerr << std::endl;
			// Fall through to GLX below
		}
		else if( initGLEW( "EGL" ) )
		{
			m_ownerThread = std::this_thread::get_id();
			m_backendName = "EGL";
			std::cerr << "GL backend: EGL" << std::endl;
			return true;
		}
		else
		{
			std::cerr << "makeCurrent: EGL initGLEW failed" << std::endl;
		}
	}

	// ---- 2. GLX path (hardware GPU with X11) ----

	if( m_glxContext )
	{
		Display *x11dpy = (Display*)m_glxDisplay;
		if( glXMakeCurrent( x11dpy, (GLXDrawable)m_glxWindow, (GLXContext)m_glxContext ) )
		{
			if( initGLEW( "GLX" ) )
			{
				m_ownerThread = std::this_thread::get_id();
				m_backendName = "GLX";
				std::cerr << "GL backend: GLX" << std::endl;
				return true;
			}
		}
	}

	// ---- 3. OSMesa fallback (CPU software) ----

	m_backendName = "OSMesa";
	std::cerr << "makeCurrent: all backends failed, returning false" << std::endl;
	return false;
}

void GLContextManager::release()
{
	// Worker-owned context: we never unbind.  The context stays bound
	// on the worker thread persistently.
}

void GLContextManager::registerTexture( unsigned int id )
{
	m_textures.push_back( id );
}

void GLContextManager::cleanupTextures()
{
	if( !m_textures.empty() )
	{
		makeCurrent();
		glDeleteTextures( (GLsizei)m_textures.size(), (const GLuint*)m_textures.data() );
		m_textures.clear();
	}

	if( m_outputFBO || m_outputTex )
	{
		makeCurrent();
		if( m_outputFBO && glDeleteFramebuffersF )
			glDeleteFramebuffersF( 1, &m_outputFBO );
		if( m_outputTex )
			glDeleteTextures( 1, &m_outputTex );
		m_outputFBO = 0;
		m_outputTex = 0;
		m_outputTexW = 0;
		m_outputTexH = 0;
	}
}

void GLContextManager::setOutputFBO( unsigned int fbo, unsigned int tex, int width, int height )
{
	// Clean up previous FBO/texture if different
	if( m_outputFBO && m_outputFBO != fbo )
	{
		GLuint oldFbo = m_outputFBO;
		GLuint oldTex = m_outputTex;
		m_outputFBO = 0;
		m_outputTex = 0;
		makeCurrent();
		if( glDeleteFramebuffersF )
			glDeleteFramebuffersF( 1, &oldFbo );
		glDeleteTextures( 1, &oldTex );
	}
	m_outputFBO = fbo;
	m_outputTex = tex;
	m_outputTexW = width;
	m_outputTexH = height;
}

const char* GLContextManager::backendName() const
{
	return m_backendName;
}
