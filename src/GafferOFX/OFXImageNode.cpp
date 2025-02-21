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

#include "GafferOFX/OFXImageNode.h"
#include "GafferOFX/Host.h"
#include "GafferOFX/ClipInstance.h"

#include "Gaffer/Metadata.h"
#include "Gaffer/ArrayPlug.h"

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace GafferImage;
using namespace Gaffer;
using namespace GafferOFX;

//////////////////////////////////////////////////////////////////////////
// OFXImageNode implementation
//////////////////////////////////////////////////////////////////////////

GAFFER_NODE_DEFINE_TYPE( OFXImageNode );

size_t OFXImageNode::g_firstPlugIndex = 0;

OFXImageNode::OFXImageNode( const std::string &name )
	: ImageProcessor( name, 0, 1000 )
{
	storeIndexOfNextChild( g_firstPlugIndex );
	addChild( new StringPlug( "pluginId" ) );
	addChild( new Plug( "parameters", Plug::In, Plug::Default & ~Plug::AcceptsInputs ) );
}

OFXImageNode::~OFXImageNode()
{
}

bool OFXImageNode::createPluginInstance()
{
	Host& host = Host::instance();
	auto plugin = host.m_pluginCache.getPluginById(pluginIdPlug()->getValue());
	if( plugin )
	{
		m_instance.reset(
			static_cast<EffectImageInstance*>(
				plugin->createInstance(kOfxImageEffectContextFilter, this)
				)
		);

		const auto& instanceDesc = m_instance->getDescriptor();
		const auto& clips = instanceDesc.getClips();
		size_t numInputs = 0;

		for ( const auto& clip : clips )
		{
			if ( clip.first == "Output" )
			{
				continue;
			}
			numInputs++;
			inPlugs()->resize( std::max( inPlugs()->children().size(), numInputs ) ); // Add new plug if needed.
			IECore::ConstStringDataPtr label = new StringData( clip.first );
			Metadata::registerValue( inPlugs()->getChild( numInputs - 1 ), "label", label );
			Metadata::registerValue( inPlugs()->getChild( numInputs - 1 ), "noduleLayout:label", label );
		}
		inPlugs()->resize( numInputs );

		//m_instance->getClipPreferences();
		//OfxStatus stat = m_instance->createInstanceAction();
		//std::cout << "create instance action: " << stat << std::endl;

		// now we need to to call getClipPreferences on the instance so that it does the clip component/depth
		// logic and caches away the components and depth on each clip.
		//bool ok = m_instance->getClipPreferences();
		//std::cout << "get clip preference: " << ok << std::endl;

		//// current render scale of 1
		//OfxPointD renderScale;
		//renderScale.x = renderScale.y = 1.0;
		//int numFramesToRender = 1;
	
		//// say we are about to render a bunch of frames 
		//stat = m_instance->beginRenderAction(0, numFramesToRender, 1.0, false, renderScale, /*sequential=*/true, /*interactive=*/false);
		//std::cout << "begin render action: " << stat << std::endl;
	
		return true;
	}
	return false;
}

Gaffer::StringPlug* OFXImageNode::pluginIdPlug()
{
	return getChild<StringPlug>( g_firstPlugIndex );
}

const Gaffer::StringPlug* OFXImageNode::pluginIdPlug() const
{
	return getChild<StringPlug>( g_firstPlugIndex );
}

Gaffer::Plug *OFXImageNode::parametersPlug()
{
	return getChild<Plug>( g_firstPlugIndex + 1 );
}

const Gaffer::Plug *OFXImageNode::parametersPlug() const
{
	return getChild<Plug>( g_firstPlugIndex + 1 );
}

void OFXImageNode::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	ImageProcessor::affects( input, outputs );
}

void OFXImageNode::hashViewNames( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageProcessor::hashViewNames( output, context, h );
}

IECore::ConstStringVectorDataPtr OFXImageNode::computeViewNames( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return ImagePlug::defaultViewNames();
}

void OFXImageNode::hashFormat( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageProcessor::hashFormat( output, context, h );
}

GafferImage::Format OFXImageNode::computeFormat( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return GafferImage::Format();
}

void OFXImageNode::hashDataWindow( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageProcessor::hashDataWindow( output, context, h );
}

Imath::Box2i OFXImageNode::computeDataWindow( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return Box2i();
}

IECore::ConstCompoundDataPtr OFXImageNode::computeMetadata( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return outPlug()->metadataPlug()->defaultValue();
}

bool OFXImageNode::computeDeep( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return true;
}

void OFXImageNode::hashSampleOffsets( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	h = ImagePlug::emptyTileSampleOffsets()->Object::hash();
}

IECore::ConstIntVectorDataPtr OFXImageNode::computeSampleOffsets( const Imath::V2i &tileOrigin, const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return ImagePlug::emptyTileSampleOffsets();
}

void OFXImageNode::hashChannelNames( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageProcessor::hashChannelNames( output, context, h );
}

IECore::ConstStringVectorDataPtr OFXImageNode::computeChannelNames( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return new IECore::StringVectorData();
}

void OFXImageNode::hashChannelData( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	h = ImagePlug::emptyTile()->Object::hash();
}

IECore::ConstFloatVectorDataPtr OFXImageNode::computeChannelData( const std::string &channelName, const Imath::V2i &tileOrigin, const Gaffer::Context *context, const ImagePlug *parent ) const
{

	if ( m_instance )
	{
		// The render window is in pixel coordinates
		OfxPointD renderScale;
		renderScale.x = renderScale.y = 1.0;

		// ie: render scale and a PAR of not 1
		OfxRectI  renderWindow;
		renderWindow.x1 = renderWindow.y1 = 0;
		renderWindow.x2 = 720;
		renderWindow.y2 = 576;
	
		/// RoI is in canonical coords, 
		OfxRectD  regionOfInterest;
		regionOfInterest.x1 = regionOfInterest.y1 = 0;
		regionOfInterest.x2 = renderWindow.x2 * m_instance->getProjectPixelAspectRatio();
		regionOfInterest.y2 = 576;
		
	
		// get the output clip
		GafferOFX::ClipInstance* outputClip = dynamic_cast<GafferOFX::ClipInstance*>(m_instance->getClip("Output"));
		std::cout << "output clip: " << outputClip << std::endl;

		OfxTime frame = context->getFrame();
		std::map<OFX::Host::ImageEffect::ClipInstance *, OfxRectD> rois;
		m_instance->getRegionOfInterestAction(frame, renderScale, regionOfInterest, rois);

		// render a frame
		m_instance->renderAction(frame, kOfxImageFieldBoth, renderWindow, renderScale, /*sequential=*/true, /*interactive=*/false, /*draft=*/false);
		// get the output image buffer
		GafferOFX::Image *outputImage = outputClip->getOutputImage();
		std::cout << "output image : " << outputImage << std::endl;
	}
	return ImagePlug::emptyTile();
}

const GafferOFX::EffectImageInstance* OFXImageNode::effectInstance() const
{
	return m_instance.get();
}
