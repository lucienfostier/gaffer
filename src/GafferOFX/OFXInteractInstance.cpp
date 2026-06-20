//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2025, Lucien Fostier. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      * Redistributions of source code must retain the above
//        copyright notice, this list of conditions and the following
//        disclaimer.
//
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following
//        disclaimer in the documentation and/or other materials provided with
//        the distribution.
//
//      * Neither the name of John Haddon nor the names of
//        any other contributors to this software may be used to endorse or
//        promote products derived from this software without specific prior
//        written permission.
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

#include "GafferOFX/OFXInteractInstance.h"

#include <GL/gl.h>

#include <iostream>

// GafferOFX does not link GLEW, so we must declare GL 2.0 functions manually.
extern "C" {
	extern GLint glGetUniformLocation( GLuint program, const char *name );
	extern void glUniform1i( GLint location, GLint v0 );
	extern void glUseProgram( GLuint program );
}

using namespace GafferOFX;

GafferOFXInteractInstance::GafferOFXInteractInstance(
	OFX::Host::ImageEffect::Instance &effectInstance,
	int bitDepthPerComponent,
	bool hasAlpha
)
	: OFX::Host::ImageEffect::OverlayInteract( effectInstance, bitDepthPerComponent, hasAlpha ),
	  m_created( false ),
	  m_viewportWidth( 100 ),
	  m_viewportHeight( 100 ),
	  m_time( 1.0 )
{
}

GafferOFXInteractInstance::~GafferOFXInteractInstance()
{
	destroyInstance();
}

OfxStatus GafferOFXInteractInstance::callEntry( const char *action, OFX::Host::Property::Set *inArgs )
{
	if( _state != OFX::Host::Interact::eFailed )
	{
		OfxPropertySetHandle inHandle = inArgs ? inArgs->getHandle() : NULL;
		// The plugin's support library expects the interact instance handle
		// (this) so it can retrieve the Interact pointer via the interact suite.
		void *handle = getHandle();
		return _descriptor.callEntry( action, handle, inHandle, NULL );
	}
	return kOfxStatFailed;
}

OfxStatus GafferOFXInteractInstance::createInstance()
{
	if( m_created )
		return kOfxStatOK;
	// Pass the interact instance handle (getHandle()) so the plugin's
	// retrieveEffectFromInteractHandle can find kOfxPropEffectInstance
	// in the instance's property set (set by the Host's Interact::Instance
	// constructor). The descriptor handle lacks this property.
	void *handle = getHandle();
	OfxStatus s = _descriptor.callEntry( kOfxActionCreateInstance, handle, NULL, NULL );
	if( s == kOfxStatOK || s == kOfxStatReplyDefault )
	{
		_state = OFX::Host::Interact::eCreated;
		m_created = true;
	}
	else
	{
		_state = OFX::Host::Interact::eFailed;
	}
	return s;
}

void GafferOFXInteractInstance::destroyInstance()
{
	std::cerr << "DEBUG destroyInstance called, m_created=" << m_created << std::endl;
	m_created = false;
}

void GafferOFXInteractInstance::setViewportSize( double width, double height )
{
	m_viewportWidth = width;
	m_viewportHeight = height;
}

void GafferOFXInteractInstance::setTime( OfxTime time )
{
	m_time = time;
}

OfxTime GafferOFXInteractInstance::getTime() const
{
	return m_time;
}

void GafferOFXInteractInstance::getViewportSize( double &width, double &height ) const
{
	width = m_viewportWidth;
	height = m_viewportHeight;
}

void GafferOFXInteractInstance::getPixelScale( double &xScale, double &yScale ) const
{
	xScale = 1.0;
	yScale = 1.0;
}

void GafferOFXInteractInstance::getBackgroundColour( double &r, double &g, double &b ) const
{
	r = 0.3;
	g = 0.3;
	b = 0.3;
}

bool GafferOFXInteractInstance::getSuggestedColour( double &r, double &g, double &b ) const
{
	r = 1.0; g = 0.0; b = 0.0;
	return true;
}

void GafferOFXInteractInstance::debugDraw()
{
	GLint prog;
	glGetIntegerv( GL_CURRENT_PROGRAM, &prog );
	if( prog )
	{
		glUniform1i( glGetUniformLocation( prog, "isCurve" ), 0 );
		glUniform1i( glGetUniformLocation( prog, "border" ), 0 );
		glUniform1i( glGetUniformLocation( prog, "edgeAntiAliasing" ), 0 );
		glUniform1i( glGetUniformLocation( prog, "textureType" ), 0 );
	}

	// Origin marker: magenta cross at (0,0) in current coordinate system
	glColor3f( 1.0f, 0.0f, 1.0f );
	glBegin( GL_LINES );
	glVertex2f( -20.0f, 0.0f );
	glVertex2f( 20.0f, 0.0f );
	glVertex2f( 0.0f, -20.0f );
	glVertex2f( 0.0f, 20.0f );
	glEnd();

	// Red rect at (100,100,200,200)
	glColor3f( 1.0f, 0.0f, 0.0f );
	glBegin( GL_LINE_LOOP );
	glVertex2f( 100.0f, 100.0f );
	glVertex2f( 200.0f, 100.0f );
	glVertex2f( 200.0f, 200.0f );
	glVertex2f( 100.0f, 200.0f );
	glEnd();

	// Green rect at (500,500,600,600)  
	glColor3f( 0.0f, 1.0f, 0.0f );
	glBegin( GL_LINE_LOOP );
	glVertex2f( 500.0f, 500.0f );
	glVertex2f( 600.0f, 500.0f );
	glVertex2f( 600.0f, 600.0f );
	glVertex2f( 500.0f, 600.0f );
	glEnd();
}

void GafferOFXInteractInstance::renderOverlay( double time, double renderScaleX, double renderScaleY, double pixelAspect, int /*imageWidth*/, int /*imageHeight*/ )
{
	std::cerr << "DEBUG renderOverlay called" << std::endl;

	// Save GL state to avoid corrupting Gaffer's rendering pipeline
	glPushAttrib( GL_ALL_ATTRIB_BITS );
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();

	// Gaffer's ImageView camera already maps pixel coordinates to screen.
	// We leave the projection and modelview matrices as-is so the plugin's
	// pixel-coordinate drawing (e.g. glVertex2f(32,32)) maps to the correct
	// image pixel position, accounting for zoom/pan in the viewport.
	// Only apply pixelAspect for non-square pixels.
	glMatrixMode( GL_MODELVIEW );
	glScalef( pixelAspect, 1.0f, 1.0f );

	// Disable Gaffer's shader — plugins use fixed-function GL (glBegin/glEnd).
	GLint prog;
	glGetIntegerv( GL_CURRENT_PROGRAM, &prog );
	if( prog )
	{
		glUseProgram( 0 );
	}

	// Clear accumulated GL errors before plugin draw
	while( glGetError() != GL_NO_ERROR ) {}

	// Dispatch draw to the plugin
	OfxPointD renderScale = { renderScaleX, renderScaleY };
	drawAction( time, renderScale );

	// Log GL errors that the plugin may have left behind
	GLenum err;
	while( ( err = glGetError() ) != GL_NO_ERROR )
	{
		std::cerr << "DEBUG GL error after draw: 0x" << std::hex << err << std::dec << std::endl;
	}

	// Re-enable Gaffer's shader
	if( prog )
	{
		glUseProgram( prog );
	}

	// Restore Gaffer's GL state
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPopMatrix();
	glPopAttrib();

	std::cerr << "DEBUG renderOverlay done" << std::endl;
}

OfxStatus GafferOFXInteractInstance::swapBuffers()
{
	// Gaffer manages its own buffer swapping — tell the plugin we handled it.
	return kOfxStatReplyDefault;
}

OfxStatus GafferOFXInteractInstance::redraw()
{
	// Returning kOfxStatOK tells the plugin the redraw was *scheduled*, causing
	// it to call redraw() again on the next draw, creating an infinite loop.
	// kOfxStatReplyDefault tells the plugin the host doesn't support
	// plugin-requested redraws, which is the correct behaviour here.
	return kOfxStatReplyDefault;
}
