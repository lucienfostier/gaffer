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
		// Use the effect instance handle for interact-specific actions so the
		// plugin can dispatch to the correct effect context.
		void *handle = ( strncmp( action, "OfxInteractAction", 17 ) == 0 )
			? _effectInstance
			: getHandle();
		return _descriptor.callEntry( action, handle, inHandle, NULL );
	}
	return kOfxStatFailed;
}

OfxStatus GafferOFXInteractInstance::createInstance()
{
	if( m_created )
		return kOfxStatOK;
	// For create instance, use the overlay descriptor handle that was
	// registered during describe(). The plugin maps descriptor handles
	// to overlay interact creation.
	OfxStatus s = _descriptor.callEntry( kOfxActionCreateInstance, _descriptor.getHandle(), NULL, NULL );
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
	return false;
}

void GafferOFXInteractInstance::setupGLProjection()
{
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glOrtho( 0, m_viewportWidth, 0, m_viewportHeight, -1, 1 );
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glLoadIdentity();
}

void GafferOFXInteractInstance::restoreGLProjection()
{
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPopMatrix();
}

OfxStatus GafferOFXInteractInstance::swapBuffers()
{
	return kOfxStatOK;
}

OfxStatus GafferOFXInteractInstance::redraw()
{
	return kOfxStatOK;
}
