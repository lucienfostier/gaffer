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

// MOVE THIS TO GAFFEROFXUI as it depends on GL
#include "GafferOFX/OFXInteractInstance.h"
#include "GafferOFX/EffectImageInstance.h"

#include <GL/gl.h>
#define D(...) fprintf( stderr, "OFXINTERACT: " __VA_ARGS__ ), fflush( stderr )

// GafferOFX does not link GLEW, so we must declare GL 2.0 functions manually.
extern "C" {
	extern GLint glGetUniformLocation( GLuint program, const char *name );
	extern void glUniform1i( GLint location, GLint v0 );
	extern void glUseProgram( GLuint program );
	extern void glBindFramebuffer( GLenum target, GLuint framebuffer );
}

// Framebuffer object constants not in <GL/gl.h>
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif

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
	  m_displayWindowMinX( 0 ),
	  m_displayWindowMinY( 0 ),
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
		void *handle = getHandle();
		OfxStatus s = _descriptor.callEntry( action, handle, inHandle, NULL );
		fprintf( stderr, "DBG callEntry action=%s state=%d handle=%p ret=%d\n",
			action, (int)_state, handle, (int)s );
		return s;
	}
	fprintf( stderr, "DBG callEntry action=%s FAILED state=%d\n", action, (int)_state );
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
	m_created = false;
}

void GafferOFXInteractInstance::setDisplayWindowOrigin( double x, double y )
{
	m_displayWindowMinX = x;
	m_displayWindowMinY = y;
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
	r = 1.0; g = 1.0; b = 1.0;
	return true;
}

void GafferOFXInteractInstance::debugDraw()
{
}

void GafferOFXInteractInstance::renderOverlay( double time, double renderScaleX, double renderScaleY, double pixelAspect, int imageWidth, int imageHeight )
{
	// Manually save the GL state that the plugin overlay may modify,
	// so we can restore it regardless of push/pop stack limitations.
	GLint prog;
	glGetIntegerv( GL_CURRENT_PROGRAM, &prog );

	GLint fboBinding;
	glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &fboBinding );

	GLboolean blendWasEnabled = glIsEnabled( GL_BLEND );
	GLint blendSrc, blendDst;
	glGetIntegerv( GL_BLEND_SRC, &blendSrc );
	glGetIntegerv( GL_BLEND_DST, &blendDst );

	GLboolean depthTestWasEnabled = glIsEnabled( GL_DEPTH_TEST );
	GLboolean cullFaceWasEnabled = glIsEnabled( GL_CULL_FACE );
	GLboolean scissorTestWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
	GLboolean texture2DWasEnabled = glIsEnabled( GL_TEXTURE_2D );
	GLboolean lineSmoothWasEnabled = glIsEnabled( GL_LINE_SMOOTH );

	// Disable Gaffer's shader — plugins use fixed-function GL (glBegin/glEnd).
	if( prog )
	{
		glUseProgram( 0 );
	}

	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_BLEND );
	glDisable( GL_TEXTURE_2D );
	glDisable( GL_LINE_SMOOTH );

	// Save projection matrix — plugins like RectangleInteract modify it
	// via glTranslated without push/pop, relying on a second translation
	// to undo.  If that doesn't execute (early return, exception, etc.),
	// the projection is left corrupted for our subsequent drawing.
	GLdouble projMatrix[16];
	glMatrixMode( GL_PROJECTION );
	glGetDoublev( GL_PROJECTION_MATRIX, projMatrix );
	glMatrixMode( GL_MODELVIEW );

	// The ImageGadget renders in "world space" where pixel (x, y) maps to
	// world position (x * pixelAspect, y).  Push a modelview scale so that
	// the plugin's pixel-space drawing (0..imageWidth, 0..imageHeight)
	// lands in the same world space as the image.
	const bool scalePixelAspect = ( pixelAspect > 0.0 && fabs( pixelAspect - 1.0 ) > 1e-6 );
	if( scalePixelAspect )
	{
		glMatrixMode( GL_MODELVIEW );
		glPushMatrix();
		glScalef( pixelAspect, 1.0, 1.0 );
	}

	// Debug: print GL state and draw reference crosshair at image center
	{
		GLdouble mv[16], proj[16];
		glGetDoublev( GL_MODELVIEW_MATRIX, mv );
		glGetDoublev( GL_PROJECTION_MATRIX, proj );
		GLint vp[4];
		glGetIntegerv( GL_VIEWPORT, vp );
		fprintf( stderr, "DBG overlay: img=%dx%d pa=%.3f vp=%d,%d %dx%d\n",
			imageWidth, imageHeight, pixelAspect,
			vp[0], vp[1], vp[2], vp[3] );
		fprintf( stderr, "DBG overlay MV: [%.2f %.2f %.2f %.2f] [%.2f %.2f %.2f %.2f] [%.2f %.2f %.2f %.2f] [%.2f %.2f %.2f %.2f]\n",
			mv[0],mv[1],mv[2],mv[3], mv[4],mv[5],mv[6],mv[7],
			mv[8],mv[9],mv[10],mv[11], mv[12],mv[13],mv[14],mv[15] );
		fprintf( stderr, "DBG overlay PJ: [%.2f %.2f %.2f %.2f] [%.2f %.2f %.2f %.2f] [%.2f %.2f %.2f %.2f] [%.2f %.2f %.2f %.2f]\n",
			proj[0],proj[1],proj[2],proj[3], proj[4],proj[5],proj[6],proj[7],
			proj[8],proj[9],proj[10],proj[11], proj[12],proj[13],proj[14],proj[15] );
	}

	// Dispatch draw to the plugin
	{
		OfxPointD renderScale = { renderScaleX, renderScaleY };
		drawAction( time, renderScale );
	}

	// Draw reference crosshair at image center
	{
		const float cx = imageWidth * 0.5f;
		const float cy = imageHeight * 0.5f;
		const float s = 10.0f;
		glColor3f( 0.0f, 1.0f, 0.0f );
		glBegin( GL_LINES );
		glVertex2f( cx - s, cy ); glVertex2f( cx + s, cy );
		glVertex2f( cx, cy - s ); glVertex2f( cx, cy + s );
		glEnd();
		// Also mark (0,0) with a tiny dot
		glPointSize( 4.0f );
		glBegin( GL_POINTS );
		glVertex2f( 0.0f, 0.0f );
		glEnd();
		fprintf( stderr, "DBG overlay: drawn reference crosshair at (%.1f, %.1f), dot at (0,0)\n", cx, cy );
	}

	// Restore projection matrix — override any corruption from the plugin.
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixd( projMatrix );
	glMatrixMode( GL_MODELVIEW );

	// Restore modelview
	if( scalePixelAspect )
	{
		glPopMatrix();
	}
	glMatrixMode( GL_MODELVIEW );

	// Restore GL state that the plugin may have changed.
	if( depthTestWasEnabled )
		glEnable( GL_DEPTH_TEST );
	else
		glDisable( GL_DEPTH_TEST );
	if( cullFaceWasEnabled )
		glEnable( GL_CULL_FACE );
	else
		glDisable( GL_CULL_FACE );
	if( scissorTestWasEnabled )
		glEnable( GL_SCISSOR_TEST );
	else
		glDisable( GL_SCISSOR_TEST );
	if( texture2DWasEnabled )
		glEnable( GL_TEXTURE_2D );
	else
		glDisable( GL_TEXTURE_2D );
	if( lineSmoothWasEnabled )
		glEnable( GL_LINE_SMOOTH );
	else
		glDisable( GL_LINE_SMOOTH );

	glBlendFunc( blendSrc, blendDst );
	if( blendWasEnabled )
		glEnable( GL_BLEND );
	else
		glDisable( GL_BLEND );

	// Restore FBO binding (plugin may have changed it)
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, fboBinding );

	// Re-enable Gaffer's shader
	if( prog )
	{
		glUseProgram( prog );
	}
}

OfxStatus GafferOFXInteractInstance::swapBuffers()
{
	return kOfxStatReplyDefault;
}

OfxStatus GafferOFXInteractInstance::redraw()
{
	return kOfxStatReplyDefault;
}

void GafferOFXInteractInstance::notifyPluginEdited()
{
	OFX::Host::ImageEffect::Instance &effect = _instance;
	auto *gafferEffect = dynamic_cast<GafferOFX::EffectImageInstance *>( &effect );
	if( !gafferEffect )
		return;

	const auto &interacted = gafferEffect->interactedParams();
	if( interacted.empty() )
		return;

	fprintf( stderr, "DBG notifyPluginEdited: %zu interacted params\n", interacted.size() );

	const double frame = effect.getFrameRecursive();
	OfxPointD renderScale;
	effect.getRenderScaleRecursive( renderScale.x, renderScale.y );

	effect.beginInstanceChangedAction( kOfxChangePluginEdited );

	for( const auto &name : interacted )
	{
		fprintf( stderr, "DBG notifyPluginEdited dispatching '%s'\n", name.c_str() );
		effect.paramInstanceChangedAction(
			name, kOfxChangePluginEdited, frame, renderScale
		);
	}
	fprintf( stderr, "DBG notifyPluginEdited exit\n" );

	effect.endInstanceChangedAction( kOfxChangePluginEdited );
	gafferEffect->clearInteractedParams();
}
