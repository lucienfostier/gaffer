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
#include <GL/gl.h>

#include <iostream>

#include "GafferOFX/ClipInstance.h"
#include "GafferOFX/Host.h"
#include "GafferOFX/EffectImageInstance.h"
#include "GafferOFX/GLContextManager.h"

#include "Gaffer/Context.h"

using namespace GafferOFX;

namespace
{
  const double    kPalPixelAspect = 1.0;
  const int       kPalSizeXPixels = 720;
  const int       kPalSizeYPixels = 576;
  const OfxRectI  kPalRegionPixels = {0, 0, kPalSizeXPixels, kPalSizeYPixels};
}

GafferOFX::Image::Image( ClipInstance &clip, OfxTime time, int view, const OfxRectI *bounds )
	: OFX::Host::ImageEffect::Image( clip )
	, m_data(nullptr)
{
	int width = kPalSizeXPixels;
	int height = kPalSizeYPixels;

	if( bounds )
	{
		width = bounds->x2 - bounds->x1;
		height = bounds->y2 - bounds->y1;
	}

	m_data.reset( new OfxRGBAColourF[width * height] );

	OfxRectI imageBounds;
	if( bounds )
	{
		imageBounds = *bounds;
	}
	else
	{
		imageBounds.x1 = 0; imageBounds.y1 = 0;
		imageBounds.x2 = width; imageBounds.y2 = height;
	}

	// render scale x and y of 1.0
	setDoubleProperty(kOfxImageEffectPropRenderScale, 1.0, 0);
	setDoubleProperty(kOfxImageEffectPropRenderScale, 1.0, 1);

	// data ptr
	setPointerProperty(kOfxImagePropData, m_data.get());

	// bounds and rod
	setIntProperty(kOfxImagePropBounds, imageBounds.x1, 0);
	setIntProperty(kOfxImagePropBounds, imageBounds.y1, 1);
	setIntProperty(kOfxImagePropBounds, imageBounds.x2, 2);
	setIntProperty(kOfxImagePropBounds, imageBounds.y2, 3);

	setIntProperty(kOfxImagePropRegionOfDefinition, imageBounds.x1, 0);
	setIntProperty(kOfxImagePropRegionOfDefinition, imageBounds.y1, 1);
	setIntProperty(kOfxImagePropRegionOfDefinition, imageBounds.x2, 2);
	setIntProperty(kOfxImagePropRegionOfDefinition, imageBounds.y2, 3);

	// pixel depth — we always use float
	setStringProperty(kOfxImageEffectPropPixelDepth, kOfxBitDepthFloat);
	// Use the clip's resolved component type (set by setDefaultClipPreferences),
	// falling back to RGBA for the Source clip (created before clip preferences
	// are evaluated) or whenever the clip hasn't set a specific component.
	// Note: getClipBits() reads from the property set which stays at
	// kOfxImageComponentNone because setComponents() only updates _components.
	const std::string &clipComps = clip.getComponents();
	if( clipComps != kOfxImageComponentNone && !clipComps.empty() )
	{
		setStringProperty(kOfxImageEffectPropComponents, clipComps);
	}
	else
	{
		setStringProperty(kOfxImageEffectPropComponents, kOfxImageComponentRGBA);
	}

	// row bytes
	setIntProperty(kOfxImagePropRowBytes, width * sizeof(OfxRGBAColourF));

	// field order — unfielded/progressive
	setStringProperty(kOfxImagePropField, kOfxImageFieldNone);
}

OfxRGBAColourF* Image::pixel( int x, int y ) const
{
	OfxRectI bounds = getBounds();

	if ((x >= bounds.x1) && ( x< bounds.x2) && ( y >= bounds.y1) && ( y < bounds.y2) )
	{
		int rowBytes = getIntProperty(kOfxImagePropRowBytes);
		OfxRGBAColourF* data = reinterpret_cast<OfxRGBAColourF*>( getPointerProperty( kOfxImagePropData ) );
		int offset = (y - bounds.y1) * (rowBytes / (int)sizeof(OfxRGBAColourF)) + (x - bounds.x1);
		return &data[offset];
	}

	return 0;
}

Image::~Image() 
{
}

void Image::setExternalData( const void* externalData, int width, int height, const OfxRectI &bounds )
{
	setPointerProperty( kOfxImagePropData, const_cast<void*>( externalData ) );

	setIntProperty( kOfxImagePropBounds, bounds.x1, 0 );
	setIntProperty( kOfxImagePropBounds, bounds.y1, 1 );
	setIntProperty( kOfxImagePropBounds, bounds.x2, 2 );
	setIntProperty( kOfxImagePropBounds, bounds.y2, 3 );

	setIntProperty( kOfxImagePropRegionOfDefinition, bounds.x1, 0 );
	setIntProperty( kOfxImagePropRegionOfDefinition, bounds.y1, 1 );
	setIntProperty( kOfxImagePropRegionOfDefinition, bounds.x2, 2 );
	setIntProperty( kOfxImagePropRegionOfDefinition, bounds.y2, 3 );

	// Components are left as set by ImageBase::getClipBits() — do not override.
	setIntProperty( kOfxImagePropRowBytes, width * sizeof( OfxRGBAColourF ) );
}

GafferOFX::ClipInstance::ClipInstance(
  GafferOFX::EffectImageInstance* effect,
  OFX::Host::ImageEffect::ClipDescriptor* desc )
   : OFX::Host::ImageEffect::ClipInstance( effect, *desc ), m_effect( effect ), m_name( desc->getName() ), m_outputImage( nullptr ), m_externalBuffer( nullptr ), m_bufferWidth( 0 ), m_bufferHeight( 0 ), m_renderWindow( {0,0,0,0} ), m_isConnected( false ), m_renderWindowSet( false )
{
}

GafferOFX::ClipInstance::~ClipInstance()
{
	if( m_outputImage )
	{
		m_outputImage->releaseReference();
	}

	if( m_inputTexture )
	{
		GLContextManager& mgr = GLContextManager::instance();
		if( mgr.makeCurrent() )
		{
			GLuint tex = m_inputTexture;
			glDeleteTextures( 1, &tex );
		}
	}
}

const std::string &ClipInstance::getUnmappedBitDepth() const
{
	static const std::string v( kOfxBitDepthFloat );
	return v;
}

const std::string &ClipInstance::getUnmappedComponents() const
{
	// Always RGBA — plugins expect 4-component pixels regardless of
	// what channels the input image actually has.
	static const std::string v( kOfxImageComponentRGBA );
	return v;
}

const std::string &ClipInstance::getPremult() const
{
	static const std::string v( kOfxImagePreMultiplied );
	return v;
}

double ClipInstance::getAspectRatio() const
{
	return m_effect->getProjectPixelAspectRatio();
}

double ClipInstance::getFrameRate() const
{
	if( auto ctx = Gaffer::Context::current() )
		return ctx->getFramesPerSecond();
	return 24.0;
}

void ClipInstance::getFrameRange(double &startFrame, double &endFrame) const
{
	startFrame = 1;
	endFrame = 100;
	if( auto sn = m_effect->scriptNode() )
	{
		startFrame = sn->frameStartPlug()->getValue();
		endFrame = sn->frameEndPlug()->getValue();
	}
}

const std::string &ClipInstance::getFieldOrder() const
{
	static const std::string v( kOfxImageFieldNone );
	return v;
}

bool ClipInstance::getConnected() const
{
	return m_isConnected;
}

double ClipInstance::getUnmappedFrameRate() const
{
	return getFrameRate();
}

void ClipInstance::getUnmappedFrameRange(double &unmappedStartFrame, double &unmappedEndFrame) const
{
	getFrameRange( unmappedStartFrame, unmappedEndFrame );
}

bool ClipInstance::getContinuousSamples() const
{
	return false;
}


OfxRectD ClipInstance::getRegionOfDefinition(OfxTime time) const
{
	if( m_renderWindowSet )
	{
		return m_renderWindow;
	}

	double projectWidth = kPalSizeXPixels;
	double projectHeight = kPalSizeYPixels;
	// Try to use actual project format size
	auto* effect = m_effect;
	if( effect )
	{
		effect->getProjectSize( projectWidth, projectHeight );
	}

	OfxRectD v;
	v.x1 = v.y1 = 0;
	v.x2 = projectWidth;
	v.y2 = projectHeight;
	return v;
}

OFX::Host::ImageEffect::Image* ClipInstance::getImage(OfxTime time, const OfxRectD *optionalBounds)
{
	// Ensure the clip's property set has the correct pixel depth
	// (getClipBits reads from the property set, not from _pixelDepth)
	getProps().setStringProperty( kOfxImageEffectPropPixelDepth, getUnmappedBitDepth() );

	OfxRectI imageBounds;
	const OfxRectI *useBounds = nullptr;

	if( optionalBounds )
	{
		imageBounds.x1 = (int)optionalBounds->x1;
		imageBounds.y1 = (int)optionalBounds->y1;
		imageBounds.x2 = (int)optionalBounds->x2;
		imageBounds.y2 = (int)optionalBounds->y2;
		useBounds = &imageBounds;
	}
	else
	{
		// Use clip's region of definition instead of hardcoded PAL size
		OfxRectD rod = getRegionOfDefinition( time );
		imageBounds.x1 = (int)rod.x1;
		imageBounds.y1 = (int)rod.y1;
		imageBounds.x2 = (int)rod.x2;
		imageBounds.y2 = (int)rod.y2;
		useBounds = &imageBounds;
	}

	if ( m_name == "Output" )
	{
		std::lock_guard<std::mutex> lock( m_outputImageMutex );
		if ( m_outputImage )
		{
			m_outputImage->releaseReference();
			m_outputImage = nullptr;
		}
		m_outputImage = new Image( *this, time, 0, useBounds );
		// One reference to keep m_outputImage alive after
		// the SDK's clipReleaseImage drops its reference.
		m_outputImage->addReference();

		return m_outputImage;
	}
	else
	{
		// Check frame cache first (for temporal clip access plugins like FrameBlend)
		if( !m_frameCache.empty() )
		{
			auto it = m_frameCache.find( time );
			if( it != m_frameCache.end() && m_frameCacheWidth > 0 && m_frameCacheHeight > 0 )
			{
				OfxRectI cacheBounds;
				cacheBounds.x1 = m_frameCacheDataWindow.min.x;
				cacheBounds.y1 = m_frameCacheDataWindow.min.y;
				cacheBounds.x2 = m_frameCacheDataWindow.max.x;
				cacheBounds.y2 = m_frameCacheDataWindow.max.y;
				Image *image = new Image( *this, time, 0, useBounds );
				image->setExternalData( it->second.get(), m_frameCacheWidth, m_frameCacheHeight, cacheBounds );
				return image;
			}
		}

		if ( m_externalBuffer && m_bufferWidth > 0 && m_bufferHeight > 0 )
		{
			Image *image = new Image( *this, time, 0, useBounds );
			image->setExternalData( m_externalBuffer, m_bufferWidth, m_bufferHeight, imageBounds );
			return image;
		}

		Image *image = new Image( *this, time, 0, useBounds );
		return image;
	}
}

#ifdef OFX_SUPPORTS_OPENGLRENDER
OFX::Host::ImageEffect::Texture* ClipInstance::loadTexture( OfxTime time, const char *format, const OfxRectD *optionalBounds )
{
	std::cerr << "loadTexture: clip=\"" << m_name << "\" connected=" << getConnected()
	          << " buf=" << (void*)m_externalBuffer << " w=" << m_bufferWidth << " h=" << m_bufferHeight
	          << " hasTex=" << m_inputTexture << " texW=" << m_inputTexW << " texH=" << m_inputTexH << std::endl;

	// For the Output clip, return the host-managed FBO texture so the plugin
	// renders directly into our render target. The plugin creates its own FBO
	// and attaches this texture as its color attachment; after render we
	// glReadPixels from our FBO (same texture) to get the result.
	if( m_name == "Output" )
	{
		GLContextManager& mgr = GLContextManager::instance();
		unsigned int tex = mgr.outputTexture();
		int texW = mgr.outputTexWidth();
		int texH = mgr.outputTexHeight();
		if( !tex || texW <= 0 || texH <= 0 )
		{
			std::cerr << "loadTexture exit: Output invalid — tex=" << tex
			          << " w=" << texW << " h=" << texH << std::endl;
			return nullptr;
		}

		OfxRectI bounds;
		bounds.x1 = 0; bounds.y1 = 0;
		bounds.x2 = texW;
		bounds.y2 = texH;

		GafferTexture* ret = new GafferTexture(
			*this,
			1.0, 1.0,
			tex, GL_TEXTURE_2D,
			bounds, bounds,
			bounds.x2 * 4 * (int)sizeof(float),
			"none",
			""
		);
		std::cerr << "loadTexture exit: Output ok tex=" << tex << " w=" << texW << " h=" << texH << std::endl;
		return ret;
	}

	if( !m_externalBuffer || m_bufferWidth <= 0 || m_bufferHeight <= 0 )
	{
		std::cerr << "loadTexture exit: no buffer" << std::endl;
		return nullptr;
	}

	GLContextManager& mgr = GLContextManager::instance();
	if( !mgr.makeCurrent() )
	{
		std::cerr << "loadTexture exit: makeCurrent failed" << std::endl;
		return nullptr;
	}

	// Reuse cached input texture if dimensions match, otherwise re-allocate
	if( !m_inputTexture || m_inputTexW != m_bufferWidth || m_inputTexH != m_bufferHeight )
	{
		if( m_inputTexture )
			glDeleteTextures( 1, &m_inputTexture );

		glGenTextures( 1, &m_inputTexture );
		glBindTexture( GL_TEXTURE_2D, m_inputTexture );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, m_bufferWidth, m_bufferHeight, 0, GL_RGBA, GL_FLOAT, nullptr );

		m_inputTexW = m_bufferWidth;
		m_inputTexH = m_bufferHeight;

		mgr.registerTexture( m_inputTexture );
	}

	// Upload current buffer data every render (buffer content changes each frame)
	glBindTexture( GL_TEXTURE_2D, m_inputTexture );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, m_bufferWidth, m_bufferHeight, GL_RGBA, GL_FLOAT, m_externalBuffer );

	OfxRectI bounds;
	bounds.x1 = 0; bounds.y1 = 0;
	bounds.x2 = m_bufferWidth; bounds.y2 = m_bufferHeight;

	GLenum gle = glGetError();
	std::cerr << "loadTexture exit: Source ok tex=" << m_inputTexture << " w=" << m_bufferWidth
	          << " h=" << m_bufferHeight << " glErr=0x" << std::hex << gle << std::dec << std::endl;

	return new GafferTexture(
		*this,
		1.0, 1.0,
		m_inputTexture, GL_TEXTURE_2D,
		bounds, bounds,
		m_bufferWidth * 4 * (int)sizeof(float),
		"none",
		""
	);
}
#endif

GafferTexture::GafferTexture(
	ClipInstance& instance,
	double renderScaleX,
	double renderScaleY,
	unsigned int index,
	unsigned int target,
	const OfxRectI &bounds,
	const OfxRectI &rod,
	int rowBytes,
	const std::string &field,
	const std::string &uniqueIdentifier
)
	: OFX::Host::ImageEffect::Texture(
		instance,
		renderScaleX,
		renderScaleY,
		index,
		target,
		bounds,
		rod,
		rowBytes,
		field,
		uniqueIdentifier
	),
	m_textureId( index )
{
}

GafferTexture::~GafferTexture()
{
	// Don't delete texture here — it's cleaned up via GLContextManager::cleanupTextures()
}
