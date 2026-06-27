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
#include "GafferOFX/ClipInstance.h"
#include "GafferOFX/Host.h"
#include "GafferOFX/EffectImageInstance.h"

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


#ifdef OFX_SUPPORTS_OPENGLRENDER
OFX::Host::ImageEffect::Texture* ClipInstance::loadTexture(OfxTime time, const char *format, const OfxRectD *optionalBounds)
{
	return nullptr;
}
#endif


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
