//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2026, Lucien Fostier. All rights reserved.
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

// Lives in libGafferOFX (not GafferOFXUI) alongside the other GL-dependent
// host code (ClipInstance, GLContextManager): the overlay needs the effect
// instance type, which UI code must not depend on.
#include "GafferOFX/OFXInteractInstance.h"

#include "GafferOFX/EffectImageInstance.h"

#include <GL/gl.h>

#include <vector>

// GafferOFX does not link GLEW, so the GL 2.0 functions are declared
// manually.
extern "C" {
	extern GLint glGetUniformLocation( GLuint program, const char *name );
	extern void glUniform1i( GLint location, GLint v0 );
	extern void glUseProgram( GLuint program );
	extern void glBindFramebuffer( GLenum target, GLuint framebuffer );
	extern void glBlendEquationSeparate( GLenum modeRGB, GLenum modeAlpha );
}

// Framebuffer object constants that are not in <GL/gl.h>.
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_BLEND_EQUATION_RGB
#define GL_BLEND_EQUATION_RGB 0x8009
#endif
#ifndef GL_BLEND_EQUATION_ALPHA
#define GL_BLEND_EQUATION_ALPHA 0x883D
#endif
#ifndef GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
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

OfxStatus GafferOFXInteractInstance::createInstance()
{
	if( m_created )
	{
		return kOfxStatOK;
	}
	// Pass the interact instance handle from `getHandle()` so the plugin's
	// `retrieveEffectFromInteractHandle()` can find
	// `kOfxPropEffectInstance` in the instance property set, which the
	// Host's `Interact::Instance` constructor populates. The descriptor
	// handle lacks this property.
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
	// Save the GL state that an overlay plugin may modify. Prefer explicit
	// saves over matrix-stack assumptions: plugins sometimes leave state
	// behind without matching push/pop calls.
	GLint prog;
	glGetIntegerv( GL_CURRENT_PROGRAM, &prog );

	GLint fboBinding;
	glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &fboBinding );

	GLint viewport[4];
	glGetIntegerv( GL_VIEWPORT, viewport );

	GLint scissorBox[4];
	glGetIntegerv( GL_SCISSOR_BOX, scissorBox );

	GLboolean blendWasEnabled = glIsEnabled( GL_BLEND );
	GLint blendSrc, blendDst;
	glGetIntegerv( GL_BLEND_SRC, &blendSrc );
	glGetIntegerv( GL_BLEND_DST, &blendDst );
	GLint blendEquationRgb, blendEquationAlpha;
	glGetIntegerv( GL_BLEND_EQUATION_RGB, &blendEquationRgb );
	glGetIntegerv( GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha );

	GLboolean depthTestWasEnabled = glIsEnabled( GL_DEPTH_TEST );
	GLboolean cullFaceWasEnabled = glIsEnabled( GL_CULL_FACE );
	GLboolean scissorTestWasEnabled = glIsEnabled( GL_SCISSOR_TEST );
	GLboolean texture2DWasEnabled = glIsEnabled( GL_TEXTURE_2D );
	GLboolean lineSmoothWasEnabled = glIsEnabled( GL_LINE_SMOOTH );

	GLfloat currentColour[4];
	glGetFloatv( GL_CURRENT_COLOR, currentColour );
	GLfloat lineWidth;
	glGetFloatv( GL_LINE_WIDTH, &lineWidth );
	GLfloat pointSize;
	glGetFloatv( GL_POINT_SIZE, &pointSize );

	// Fixed-function drawing can use any texture unit, so preserve the
	// binding on all units rather than only the currently active one.
	GLint activeTexture;
	glGetIntegerv( GL_ACTIVE_TEXTURE, &activeTexture );
	GLint maxTextureUnits = 1;
	glGetIntegerv( GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTextureUnits );
	if( maxTextureUnits < 1 )
	{
		maxTextureUnits = 1;
	}
	else if( maxTextureUnits > 32 )
	{
		maxTextureUnits = 32;
	}
	std::vector<GLint> textureBindings( maxTextureUnits );
	for( int i = 0; i < maxTextureUnits; ++i )
	{
		glActiveTexture( GL_TEXTURE0 + i );
		glGetIntegerv( GL_TEXTURE_BINDING_2D, &textureBindings[i] );
	}
	glActiveTexture( activeTexture );

	// Save both matrices and their stack depths. `RectangleInteract`, for
	// example, modifies the projection without push/pop and relies on a
	// second translation to undo it.
	GLdouble projMatrix[16];
	glMatrixMode( GL_PROJECTION );
	glGetDoublev( GL_PROJECTION_MATRIX, projMatrix );
	GLint projDepth;
	glGetIntegerv( GL_PROJECTION_STACK_DEPTH, &projDepth );

	GLdouble modelviewMatrix[16];
	glMatrixMode( GL_MODELVIEW );
	glGetDoublev( GL_MODELVIEW_MATRIX, modelviewMatrix );
	GLint modelviewDepth;
	glGetIntegerv( GL_MODELVIEW_STACK_DEPTH, &modelviewDepth );

	// Disable Gaffer's shader; the plugins use fixed-function GL
	// (glBegin/glEnd).
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

	// The ImageGadget renders in "world space" where display-window pixel
	// (x, y) maps to world position (x * pixelAspect, y). OFX plugins draw
	// in PROJECT FORMAT coordinates, which may differ from the image size
	// and are always anchored at the format origin. Translate by the
	// display-window minimum before scaling, so format coordinate (0, 0)
	// lands at the display-window origin instead of world (0, 0).
	double formatW = (double)imageWidth, formatH = (double)imageHeight;
	{
		auto *gafferEffect = dynamic_cast<EffectImageInstance *>( &_instance );
		if( gafferEffect )
		{
			gafferEffect->getProjectSize( formatW, formatH );
		}
	}
	const float scaleX = formatW > 0 ? pixelAspect * imageWidth / formatW : pixelAspect;
	const float scaleY = formatH > 0 ? (float)imageHeight / formatH : 1.0f;

	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glTranslatef( m_displayWindowMinX * pixelAspect, m_displayWindowMinY, 0.0f );
	glScalef( scaleX, scaleY, 1.0f );

	// Dispatch draw to the plugin. The plugin draws at format coordinates,
	// for example 960,540 for the centre of HD.
	{
		OfxPointD renderScale = { renderScaleX, renderScaleY };
		drawAction( time, renderScale );
	}

	// Restore the matrix stacks to their original depths, then restore the
	// top matrices themselves. This also handles plugins that leave extra
	// matrices pushed.
	glMatrixMode( GL_MODELVIEW );
	GLint depth;
	glGetIntegerv( GL_MODELVIEW_STACK_DEPTH, &depth );
	while( depth > modelviewDepth )
	{
		glPopMatrix();
		glGetIntegerv( GL_MODELVIEW_STACK_DEPTH, &depth );
	}
	while( depth < modelviewDepth )
	{
		glPushMatrix();
		++depth;
	}
	glLoadMatrixd( modelviewMatrix );

	glMatrixMode( GL_PROJECTION );
	glGetIntegerv( GL_PROJECTION_STACK_DEPTH, &depth );
	while( depth > projDepth )
	{
		glPopMatrix();
		glGetIntegerv( GL_PROJECTION_STACK_DEPTH, &depth );
	}
	while( depth < projDepth )
	{
		glPushMatrix();
		++depth;
	}
	glLoadMatrixd( projMatrix );
	glMatrixMode( GL_MODELVIEW );

	// Restore the remaining GL state that the plugin may have changed.
	glViewport( viewport[0], viewport[1], viewport[2], viewport[3] );
	glScissor( scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3] );

	if( depthTestWasEnabled )
	{
		glEnable( GL_DEPTH_TEST );
	}
	else
	{
		glDisable( GL_DEPTH_TEST );
	}
	if( cullFaceWasEnabled )
	{
		glEnable( GL_CULL_FACE );
	}
	else
	{
		glDisable( GL_CULL_FACE );
	}
	if( scissorTestWasEnabled )
	{
		glEnable( GL_SCISSOR_TEST );
	}
	else
	{
		glDisable( GL_SCISSOR_TEST );
	}
	if( texture2DWasEnabled )
	{
		glEnable( GL_TEXTURE_2D );
	}
	else
	{
		glDisable( GL_TEXTURE_2D );
	}
	if( lineSmoothWasEnabled )
	{
		glEnable( GL_LINE_SMOOTH );
	}
	else
	{
		glDisable( GL_LINE_SMOOTH );
	}

	glBlendEquationSeparate( blendEquationRgb, blendEquationAlpha );
	glBlendFunc( blendSrc, blendDst );
	if( blendWasEnabled )
	{
		glEnable( GL_BLEND );
	}
	else
	{
		glDisable( GL_BLEND );
	}

	glColor4fv( currentColour );
	glLineWidth( lineWidth );
	glPointSize( pointSize );

	for( int i = 0; i < maxTextureUnits; ++i )
	{
		glActiveTexture( GL_TEXTURE0 + i );
		glBindTexture( GL_TEXTURE_2D, textureBindings[i] );
	}
	glActiveTexture( activeTexture );

	// Restore the FBO binding, which the plugin may have changed.
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, fboBinding );

	// Re-enable Gaffer's shader.
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
	{
		return;
	}

	const auto &interacted = gafferEffect->interactedParams();
	if( interacted.empty() )
	{
		return;
	}

	const double frame = effect.getFrameRecursive();
	OfxPointD renderScale;
	effect.getRenderScaleRecursive( renderScale.x, renderScale.y );

	effect.beginInstanceChangedAction( kOfxChangePluginEdited );

	for( const auto &name : interacted )
	{
		effect.paramInstanceChangedAction(
			name, kOfxChangePluginEdited, frame, renderScale
		);
	}

	effect.endInstanceChangedAction( kOfxChangePluginEdited );
	gafferEffect->clearInteractedParams();
}
