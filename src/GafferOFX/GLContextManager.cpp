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
// GLEW #undef's GLAPI at the end; OSMesa needs it for declarations
#ifndef GLAPI
#define GLAPI extern
#endif
#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif
#include <GL/osmesa.h>

#include <X11/Xlib.h>
#include <GL/glx.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace GafferOFX;

GLContextManager& GLContextManager::instance()
{
	static GLContextManager mgr;
	return mgr;
}

GLContextManager::GLContextManager()
{
	m_glxDisplay = nullptr;
	m_glxContext = nullptr;
	m_glxWindow = 0;
	m_osmesaContext = nullptr;
	m_osmesaBuffer = nullptr;
	m_usingOSMesa = false;
	m_savedDisplay = nullptr;
	m_savedDrawable = nullptr;
	m_savedContext = nullptr;
	m_makeCurrentCount = 0;

	// ---- GLX path ----

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

				fprintf( stderr, "GLContextManager: GLX context created\n" );
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

	// ---- OSMesa fallback (always create, but don't make current) ----

	OSMesaContext osCtx = OSMesaCreateContextExt( OSMESA_RGBA, 24, 8, 0, nullptr );
	if( osCtx )
	{
		unsigned char *osBuffer = (unsigned char*)malloc( 4 );
		fprintf( stderr, "GLContextManager: OSMesa context created\n" );
		m_osmesaContext = (void*)osCtx;
		m_osmesaBuffer = (void*)osBuffer;
	}

	if( !m_glxContext && !m_osmesaContext )
	{
		fprintf( stderr, "GLContextManager: no GL context available\n" );
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
}

bool GLContextManager::initGLEW()
{
	static int glewState = 0;
	if( glewState != 0 )
		return glewState == 1;

	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if( err != GLEW_OK )
	{
		fprintf( stderr, "GLContextManager: glewInit failed: %s\n", glewGetErrorString( err ) );
		glewState = -1;
		return false;
	}

	const char *renderer = (const char*)glGetString( GL_RENDERER );
	const char *versionStr = (const char*)glGetString( GL_VERSION );
	int major = 0, minor = 0;
	if( versionStr ) sscanf( versionStr, "%d.%d", &major, &minor );

	bool hasFBO = GLEW_ARB_framebuffer_object || GLEW_EXT_framebuffer_object;
	bool isHardware = renderer && !strstr( renderer, "llvmpipe" ) && !strstr( renderer, "soft" );
	bool hasGLVersion = major > 3 || ( major == 3 && minor >= 2 );

	if( hasFBO && isHardware && hasGLVersion )
	{
		glewState = 1;
		return true;
	}

	fprintf( stderr, "GLContextManager: GL rejected (renderer=%s, gl=%d.%d, fbo=%d)\n",
		renderer ? renderer : "unknown", major, minor, (int)hasFBO );
	glewState = -1;
	return false;
}

bool GLContextManager::makeCurrent()
{
	// Reentrant: if we're already in our context, just bump the counter
	if( m_makeCurrentCount > 0 )
	{
		m_makeCurrentCount++;
		return true;
	}

	// Save the current GLX context before we replace it
	m_savedDisplay = (void*)glXGetCurrentDisplay();
	m_savedDrawable = (void*)(unsigned long)glXGetCurrentDrawable();
	m_savedContext = (void*)glXGetCurrentContext();

	// ---- Try GLX first (preferred for hardware acceleration) ----

	if( m_glxContext )
	{
		Display *x11dpy = (Display*)m_glxDisplay;
		if( glXMakeCurrent( x11dpy, (GLXDrawable)m_glxWindow, (GLXContext)m_glxContext ) )
		{
			if( initGLEW() )
			{
				m_usingOSMesa = false;
				fprintf( stderr, "GLContextManager: using GLX\n" );
				m_makeCurrentCount = 1;
				return true;
			}
		}
	}

	// ---- Fall back to OSMesa ----

	if( !m_osmesaContext )
		return false;

	if( !OSMesaMakeCurrent( (OSMesaContext)m_osmesaContext, m_osmesaBuffer, GL_UNSIGNED_BYTE, 1, 1 ) )
	{
		fprintf( stderr, "GLContextManager: OSMesaMakeCurrent failed\n" );
		return false;
	}

	// Always accept OSMesa — it's our last resort, no renderer validation
	static int osmesaGlewState = 0;
	if( osmesaGlewState == 0 )
	{
		glewExperimental = GL_TRUE;
		GLenum err = glewInit();
		if( err == GLEW_OK )
		{
			const char *renderer = (const char*)glGetString( GL_RENDERER );
			const char *versionStr = (const char*)glGetString( GL_VERSION );
			int major = 0, minor = 0;
			if( versionStr ) sscanf( versionStr, "%d.%d", &major, &minor );
			bool hasFBO = GLEW_ARB_framebuffer_object || GLEW_EXT_framebuffer_object;
			fprintf( stderr, "GLContextManager: using OSMesa (renderer=%s, gl=%d.%d, fbo=%d)\n",
				renderer ? renderer : "unknown", major, minor, (int)hasFBO );

			if( hasFBO && ( major > 3 || ( major == 3 && minor >= 2 ) ) )
				osmesaGlewState = 1;
			else
				osmesaGlewState = 1; // accept anyway — best we have
		}
		else
		{
			fprintf( stderr, "GLContextManager: OSMesa glewInit failed: %s\n", glewGetErrorString( err ) );
			osmesaGlewState = -1;
		}
	}

	if( osmesaGlewState == 1 )
	{
		m_usingOSMesa = true;
		m_makeCurrentCount = 1;
		return true;
	}

	return false;
}

void GLContextManager::release()
{
	m_makeCurrentCount--;
	if( m_makeCurrentCount > 0 )
	{
		// Nested call — outer frame still needs the context current
		return;
	}

	// Restore the saved GLX context
	if( m_savedContext )
	{
		glXMakeCurrent(
			(Display*)m_savedDisplay,
			(GLXPbuffer)(unsigned long)m_savedDrawable,
			(GLXContext)m_savedContext
		);
	}
	else
	{
		// No previous context — just release ours
		if( !m_usingOSMesa && m_glxContext )
		{
			glXMakeCurrent( (Display*)m_glxDisplay, None, nullptr );
		}
	}
	m_savedDisplay = nullptr;
	m_savedDrawable = nullptr;
	m_savedContext = nullptr;
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
}
