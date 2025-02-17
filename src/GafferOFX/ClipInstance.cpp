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

using namespace GafferOFX;

namespace
{
  const double    kPalPixelAspect = 1.0;
  const int       kPalSizeXPixels = 720;
  const int       kPalSizeYPixels = 576;
  const OfxRectI  kPalRegionPixels = {0, 0, kPalSizeXPixels, kPalSizeYPixels};
}

GafferOFX::Image::Image( ClipInstance &clip, OfxTime time, int view )
	: OFX::Host::ImageEffect::Image( clip )
	, m_data(nullptr)
{
	m_data.reset( new OfxRGBAColourF[kPalSizeXPixels * kPalSizeYPixels] );

	// render scale x and y of 1.0
	setDoubleProperty(kOfxImageEffectPropRenderScale, 1.0, 0);
	setDoubleProperty(kOfxImageEffectPropRenderScale, 1.0, 1); 

	// data ptr
	setPointerProperty(kOfxImagePropData, m_data.get());

	// bounds and rod
	setIntProperty(kOfxImagePropBounds, kPalRegionPixels.x1, 0);
	setIntProperty(kOfxImagePropBounds, kPalRegionPixels.y1, 1);
	setIntProperty(kOfxImagePropBounds, kPalRegionPixels.x2, 2);
	setIntProperty(kOfxImagePropBounds, kPalRegionPixels.y2, 3);

	setIntProperty(kOfxImagePropRegionOfDefinition, kPalRegionPixels.x1, 0);
	setIntProperty(kOfxImagePropRegionOfDefinition, kPalRegionPixels.y1, 1);
	setIntProperty(kOfxImagePropRegionOfDefinition, kPalRegionPixels.x2, 2);
	setIntProperty(kOfxImagePropRegionOfDefinition, kPalRegionPixels.y2, 3);        

	// row bytes
	setIntProperty(kOfxImagePropRowBytes, kPalSizeXPixels * sizeof(OfxRGBAColourF));
}

OfxRGBAColourF* Image::pixel( int x, int y ) const
{
	OfxRectI bounds = getBounds();

	if ((x >= bounds.x1) && ( x< bounds.x2) && ( y >= bounds.y1) && ( y < bounds.y2) )
	{
		int rowBytes = getIntProperty(kOfxImagePropRowBytes);
		int offset = (y - bounds.y1) * rowBytes + (x - bounds.x1) * sizeof(OfxRGBAColourF);

		return reinterpret_cast<OfxRGBAColourF*>(&(reinterpret_cast<char*>(m_data.get())[offset]));
	}

	return 0;
}

Image::~Image() 
{
}

GafferOFX::ClipInstance::ClipInstance(
  GafferOFX::EffectImageInstance* effect,
  OFX::Host::ImageEffect::ClipDescriptor* desc )
  : OFX::Host::ImageEffect::ClipInstance( effect, *desc ), m_effect( effect ), m_name( desc->getName() ), m_outputImage( nullptr )
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
	return 1.0;
}

double ClipInstance::getFrameRate() const
{
	return 24.0;
}

void ClipInstance::getFrameRange(double &startFrame, double &endFrame) const
{
	startFrame = 0;
	endFrame = 24;
}

const std::string &ClipInstance::getFieldOrder() const
{
	static const std::string v( kOfxImageFieldNone );
	return v;
}

bool ClipInstance::getConnected() const
{
	return true;
}

double ClipInstance::getUnmappedFrameRate() const
{
	return 24;
}

void ClipInstance::getUnmappedFrameRange(double &unmappedStartFrame, double &unmappedEndFrame) const
{
	unmappedStartFrame = 0;
	unmappedEndFrame = 24;
}

bool ClipInstance::getContinuousSamples() const
{
	return false;
}


OfxRectD ClipInstance::getRegionOfDefinition(OfxTime time) const
{
	OfxRectD v;
	v.x1 = v.y1 = 0;
	v.x2 = 768;
	v.y2 = 576;
	return v;
}

OFX::Host::ImageEffect::Image* ClipInstance::getImage(OfxTime time, const OfxRectD *optionalBounds)
{
	if ( m_name == "Output" )
	{
		if ( !m_outputImage )
		{
			m_outputImage = new Image( *this, 0 );
		}

		// add another reference to the member image for this fetch
		// as we have a ref count of 1 due to construction, this will
		// cause the output image never to delete by the plugin
		// when it releases the image
		m_outputImage->addReference();

		// return it
		return m_outputImage;
	}
	else
	{
		Image *image = new Image( *this, time );
		return image;
	}
}
