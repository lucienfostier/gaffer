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

#include "GL/glew.h"
// GLEW #undef's GLAPI at the end; OSMesa/EGL need it for declarations
#ifndef GLAPI
#define GLAPI extern
#endif
#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif
#include <GL/osmesa.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <X11/Xlib.h>
#include <GL/glx.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <algorithm>

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
	m_savedDisplay = nullptr;
	m_savedDrawable = nullptr;
	m_savedContext = nullptr;
	m_savedEglContext = nullptr;
	m_makeCurrentCount = 0;

	// ---- 1. EGL (preferred — GPU + surfaceless, no X11 dependency) ----
	//
	// Strategy: try each enumerated EGL device in order, keeping the first
	// context that works.  Hardware devices (NVIDIA) are preferred over
	// llvmpipe.  If no device works, fall back to the default display.

	// Use EGL_EXT_device_enumeration + EGL_EXT_platform_device to discover
	// real GPU devices (NVIDIA, AMD) that can serve OpenGL headlessly.
	PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT =
		(PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress( "eglQueryDevicesEXT" );
	PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress( "eglGetPlatformDisplayEXT" );

	// Build a list of displays to try:  enumerated device displays + default.
	// We store all that succeeded eglInitialize; later we pick the best one.
	std::vector<EGLDisplay> candidateDisplays;

	if( eglQueryDevicesEXT )
	{
		EGLDeviceEXT devices[16];
		EGLint numDevices = 0;
		eglQueryDevicesEXT( 16, devices, &numDevices );

		for( int i = 0; i < numDevices; ++i )
		{
			EGLDisplay dpy = EGL_NO_DISPLAY;
			if( eglGetPlatformDisplayEXT )
			{
				dpy = eglGetPlatformDisplayEXT( EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr );
			}
			if( dpy != EGL_NO_DISPLAY )
			{
				EGLint major, minor;
				if( eglInitialize( dpy, &major, &minor ) )
				{
					candidateDisplays.push_back( dpy );
				}
			}
		}
	}

	// Add the default display as a software fallback.
	{
		EGLDisplay dpy = eglGetDisplay( EGL_DEFAULT_DISPLAY );
		if( dpy == EGL_NO_DISPLAY )
			dpy = eglGetDisplay( nullptr );
		if( dpy != EGL_NO_DISPLAY )
		{
			EGLint major, minor;
			if( eglInitialize( dpy, &major, &minor ) )
			{
				candidateDisplays.push_back( dpy );
			}
		}
	}

	// Try each display.  Prefer hardware, accept llvmpipe as last resort.
	// The last display in the list is the default (Mesa software fallback).
	for( EGLDisplay dpy : candidateDisplays )
	{
		const EGLint configAttribs[] = {
			EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
			EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
			EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
			EGL_NONE
		};
		EGLConfig eglConfig;
		EGLint numConfigs;
		if( !eglChooseConfig( dpy, configAttribs, &eglConfig, 1, &numConfigs ) || numConfigs == 0 )
			continue;

		eglBindAPI( EGL_OPENGL_API );
		EGLContext eglCtx = eglCreateContext( dpy, eglConfig, EGL_NO_CONTEXT, nullptr );
		if( eglCtx == EGL_NO_CONTEXT )
			continue;

		const EGLint pbAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
		EGLSurface surf = eglCreatePbufferSurface( dpy, eglConfig, pbAttribs );
		if( surf == EGL_NO_SURFACE )
		{
			eglDestroyContext( dpy, eglCtx );
			continue;
		}

		// Test: make the context current and check GL_RENDERER.
		// Skip llvmpipe if a hardware device is still available further in the list.
		eglMakeCurrent( dpy, surf, surf, eglCtx );
		const char *renderer = (const char*)glGetString( GL_RENDERER );
		bool isHW = renderer && !strstr( renderer, "llvmpipe" ) && !strstr( renderer, "soft" );
		eglMakeCurrent( dpy, surf, surf, EGL_NO_CONTEXT );

		// If this is the last candidate or it's hardware, keep it.
		if( isHW || &dpy == &candidateDisplays.back() )
		{
			m_eglDisplay = (void*)dpy;
			m_eglContext = (void*)eglCtx;
			m_eglSurface = (void*)surf;
			break;
		}

		// Otherwise destroy and try the next one.
		eglDestroySurface( dpy, surf );
		eglDestroyContext( dpy, eglCtx );
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
	cleanupTextures();

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
	glewExperimental = GL_TRUE;
	GLenum err = glewInit();

#ifdef GLEW_ERROR_NO_GLX_DISPLAY
	if( err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY )
#else
	if( err != GLEW_OK )
#endif
	{
		std::cerr << "initGLEW(" << backendName << "): glewInit failed: "
		          << glewGetErrorString( err ) << std::endl;
		return false;
	}

	const char *renderer = (const char*)glGetString( GL_RENDERER );
	const char *versionStr = (const char*)glGetString( GL_VERSION );
	int major = 0, minor = 0;
	if( versionStr ) sscanf( versionStr, "%d.%d", &major, &minor );

	bool hasFBO = GLEW_ARB_framebuffer_object || GLEW_EXT_framebuffer_object;
	if( !hasFBO )
	{
		std::cerr << "initGLEW(" << backendName << "): no FBO support" << std::endl;
		return false;
	}

	bool isHardware = renderer && !strstr( renderer, "llvmpipe" ) && !strstr( renderer, "soft" );

	if( isHardware )
	{
		if( major < 3 || ( major == 3 && minor < 2 ) )
		{
			std::cerr << "initGLEW(" << backendName << "): hardware GL "
			          << major << "." << minor << " < 3.2" << std::endl;
			return false;
		}
		return true;
	}

	// Software rendering (llvmpipe) is accepted for EGL and GLX
	// (not OSMesa, which is itself a software fallback).
	if( strcmp( backendName, "EGL" ) == 0 || strcmp( backendName, "GLX" ) == 0 )
	{
		return true;
	}

	std::cerr << "initGLEW(" << backendName << "): software renderer not accepted for this backend" << std::endl;
	return false;
}

bool GLContextManager::makeCurrent()
{
	// Idempotency guard: if our context is already current on this thread,
	// there's nothing to do. This handles nested calls from ClipInstance::loadTexture
	// (called from inside renderAction) without relying on m_makeCurrentCount alone,
	// which could be 0 if the first makeCurrent went through a different backend.
	if( m_eglContext && eglGetCurrentContext() == (EGLContext)m_eglContext )
	{
		return true;
	}
	if( m_glxContext && glXGetCurrentContext() == (GLXContext)m_glxContext )
	{
		return true;
	}
	if( m_osmesaContext && OSMesaGetCurrentContext() == (OSMesaContext)m_osmesaContext )
	{
		return true;
	}

	// Reentrant: if we're already in our context, just bump the counter
	if( m_makeCurrentCount > 0 )
	{
		m_makeCurrentCount++;
		return true;
	}

	// Save the current GL context state before we replace it
	m_savedDisplay = (void*)glXGetCurrentDisplay();
	m_savedDrawable = (void*)(unsigned long)glXGetCurrentDrawable();
	m_savedContext = (void*)glXGetCurrentContext();
	m_savedEglContext = (void*)eglGetCurrentContext();

	// ---- 1. EGL (preferred — GPU + surfaceless) ----

	if( m_eglContext )
	{
		EGLDisplay dpy = (EGLDisplay)m_eglDisplay;
		EGLContext ctx = (EGLContext)m_eglContext;
		EGLSurface surf = m_eglSurface ? (EGLSurface)m_eglSurface : EGL_NO_SURFACE;

		EGLBoolean ok = eglMakeCurrent( dpy, surf, surf, ctx );
		if( ok && initGLEW( "EGL" ) )
		{
			std::cerr << "GL backend: EGL" << std::endl;
			m_usingHardware = true;
			m_makeCurrentCount = 1;
			return true;
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
				std::cerr << "GL backend: GLX" << std::endl;
				m_usingHardware = true;
				m_makeCurrentCount = 1;
				return true;
			}
		}
	}

	// ---- 3. OSMesa fallback (CPU software) ----

	if( !m_osmesaContext )
	{
		std::cerr << "GL backend: no OSMesa available" << std::endl;
		return false;
	}

	if( !OSMesaMakeCurrent( (OSMesaContext)m_osmesaContext, m_osmesaBuffer, GL_UNSIGNED_BYTE, 1, 1 ) )
	{
		std::cerr << "GL backend: OSMesaMakeCurrent failed" << std::endl;
		return false;
	}

	// Always accept OSMesa — it's our last resort, no renderer validation
	static int osmesaGlewState = 0;
	if( osmesaGlewState == 0 )
	{
		glewExperimental = GL_TRUE;
		GLenum err = glewInit();
#ifdef GLEW_ERROR_NO_GLX_DISPLAY
		if( err == GLEW_OK || err == GLEW_ERROR_NO_GLX_DISPLAY )
#else
		if( err == GLEW_OK )
#endif
		{
			osmesaGlewState = 1;
		}
		else
		{
			osmesaGlewState = -1;
		}
	}

	if( osmesaGlewState == 1 )
	{
		std::cerr << "GL backend: OSMesa" << std::endl;
		m_usingHardware = false;
		m_makeCurrentCount = 1;
		return true;
	}

	return false;
}

void GLContextManager::release()
{
	if( m_makeCurrentCount > 0 )
		m_makeCurrentCount--;
	if( m_makeCurrentCount > 0 )
	{
		return;
	}

	// Unbind OUR context first
	if( m_usingHardware )
	{
		if( m_eglContext )
		{
			EGLSurface surf = m_eglSurface ? (EGLSurface)m_eglSurface : EGL_NO_SURFACE;
			eglMakeCurrent( (EGLDisplay)m_eglDisplay, surf, surf, EGL_NO_CONTEXT );
		}
		else if( m_glxContext )
		{
			glXMakeCurrent( (Display*)m_glxDisplay, None, nullptr );
		}
	}
	else if( m_osmesaContext )
	{
		OSMesaMakeCurrent( nullptr, nullptr, GL_UNSIGNED_BYTE, 0, 0 );
	}

	// Restore the saved context (if any)
	if( m_savedContext )
	{
		glXMakeCurrent(
			(Display*)m_savedDisplay,
			(GLXPbuffer)(unsigned long)m_savedDrawable,
			(GLXContext)m_savedContext
		);
	}
	else if( m_savedEglContext )
	{
		eglMakeCurrent( (EGLDisplay)m_savedDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, (EGLContext)m_savedEglContext );
	}
	m_savedDisplay = nullptr;
	m_savedDrawable = nullptr;
	m_savedContext = nullptr;
	m_savedEglContext = nullptr;
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
		if( m_outputFBO )
			glDeleteFramebuffers( 1, &m_outputFBO );
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
		glDeleteFramebuffers( 1, &oldFbo );
		glDeleteTextures( 1, &oldTex );
	}
	m_outputFBO = fbo;
	m_outputTex = tex;
	m_outputTexW = width;
	m_outputTexH = height;
}
