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

#include "Gaffer/Context.h"
#include "Gaffer/Metadata.h"
#include "Gaffer/ArrayPlug.h"
// CompoundObjectPlug provided via OFXImageNode.h which includes Gaffer/TypedObjectPlug.h

#include "GafferImage/ImageAlgo.h"
#include "GafferImage/Sampler.h"

#include "IECore/BoxOps.h"

#include <algorithm>
#include <iostream>
#include <vector>

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
    : ImageProcessor( name )
{
	storeIndexOfNextChild( g_firstPlugIndex );
	addChild( new StringPlug( "pluginId" ) );
	addChild( new Plug( "parameters", Plug::In, Plug::Default & ~Plug::AcceptsInputs ) );
	addChild( new CompoundObjectPlug( "__ofxRenderBuffer", Plug::Out, new CompoundObject, Plug::Default & ~Plug::Serialisable ) );
	plugSetSignal().connect( [this]( Gaffer::Plug *plug ) { plugSet( plug ); } );
}

void OFXImageNode::plugSet( Gaffer::Plug *plug )
{
	if( plug == pluginIdPlug() )
	{
		m_instance.reset();
		createPluginInstance();
	}
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
		// Use the first available context supported by the plugin
		const std::set<std::string> &contexts = plugin->getContexts();
		std::vector<std::string> contextPriority;
		if( contexts.find( kOfxImageEffectContextFilter ) != contexts.end() )
			contextPriority.push_back( kOfxImageEffectContextFilter );
		if( contexts.find( kOfxImageEffectContextGeneral ) != contexts.end() )
			contextPriority.push_back( kOfxImageEffectContextGeneral );
		if( contexts.find( kOfxImageEffectContextGenerator ) != contexts.end() )
			contextPriority.push_back( kOfxImageEffectContextGenerator );
		for( const auto &c : contexts )
		{
			if( c != kOfxImageEffectContextFilter &&
			    c != kOfxImageEffectContextGeneral &&
			    c != kOfxImageEffectContextGenerator )
			{
				contextPriority.push_back( c );
			}
		}

		if( contextPriority.empty() )
			return false;

		OFX::Host::ImageEffect::Instance *instance = nullptr;
		for( const auto &context : contextPriority )
		{
			instance = plugin->createInstance( context, this );
			if( instance )
				break;
		}

		if( !instance )
			return false;

		m_instance.reset( static_cast<EffectImageInstance*>( instance ) );

		m_instance->createInstanceAction();

		// Mark all clips as connected if the "in" plug has a connection
		const bool hasInput = inPlug()->getInput() != nullptr;
		for( int i = 0; i < m_instance->getNClips(); ++i )
		{
			if( auto *clip = dynamic_cast<GafferOFX::ClipInstance*>( m_instance->getNthClip( i ) ) )
			{
				// Only mark non-output clips based on actual input connection
				if( clip->getName() != "Output" )
				{
					clip->setConnected( hasInput );
				}
			}
		}

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

CompoundObjectPlug *OFXImageNode::ofxRenderBufferPlug()
{
	return getChild<CompoundObjectPlug>( g_firstPlugIndex + 2 );
}

const CompoundObjectPlug *OFXImageNode::ofxRenderBufferPlug() const
{
	return getChild<CompoundObjectPlug>( g_firstPlugIndex + 2 );
}

void OFXImageNode::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	ImageProcessor::affects( input, outputs );

	// Guard against uninitialized inPlug when minInputs=0
	if( !inPlug() )
	{
		return;
	}

	// Input image data affects the render buffer
	if( input == inPlug()->formatPlug() || input == inPlug()->dataWindowPlug() || input == inPlug()->channelNamesPlug() )
	{
		outputs.push_back( ofxRenderBufferPlug() );
	}

	// Input channel data affects the render buffer
	if( input == inPlug()->channelDataPlug() )
	{
		outputs.push_back( ofxRenderBufferPlug() );
	}

	// Parameters and plugin ID affect the render buffer
	if( input == pluginIdPlug() || parametersPlug()->isAncestorOf( input ) )
	{
		outputs.push_back( ofxRenderBufferPlug() );
	}

	// Render buffer affects all output image properties
	if( input == ofxRenderBufferPlug() )
	{
		outputs.push_back( outPlug()->formatPlug() );
		outputs.push_back( outPlug()->dataWindowPlug() );
		outputs.push_back( outPlug()->channelNamesPlug() );
		outputs.push_back( outPlug()->channelDataPlug() );
	}
}

void OFXImageNode::hash( const Gaffer::ValuePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	if( output == ofxRenderBufferPlug() )
	{
		hashOfxRenderBuffer( context, h );
	}
	else
	{
		ImageProcessor::hash( output, context, h );
	}
}

void OFXImageNode::compute( Gaffer::ValuePlug *output, const Gaffer::Context *context ) const
{
	if( output == ofxRenderBufferPlug() )
	{
		IECore::ConstCompoundObjectPtr renderBuffer = computeOfxRenderBuffer( context );
		static_cast<Gaffer::CompoundObjectPlug *>( output )->setValue( renderBuffer );
	}
	else
	{
		ImageProcessor::compute( output, context );
	}
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
	if( output == outPlug() )
	{
		ofxRenderBufferPlug()->hash( h );
	}
}

GafferImage::Format OFXImageNode::computeFormat( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	ImagePlug::GlobalScope globalScope( context );
	IECore::ConstCompoundObjectPtr renderBuffer = ofxRenderBufferPlug()->getValue();
	if( !renderBuffer )
	{
		return inPlug()->formatPlug()->getValue();
	}
	Box2iDataPtr dataWindowData = runTimeCast<Box2iData>(
		const_cast<Data*>( renderBuffer->member<Data>( "dataWindow" ) )
	);
	FloatDataPtr parData = runTimeCast<FloatData>(
		const_cast<Data*>( renderBuffer->member<Data>( "pixelAspect" ) )
	);
	if( dataWindowData && parData )
	{
		const Box2i &dw = dataWindowData->readable();
		return Format( dw.size().x, dw.size().y, parData->readable() );
	}
	return inPlug()->formatPlug()->getValue();
}

void OFXImageNode::hashDataWindow( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageProcessor::hashDataWindow( output, context, h );
	if( output == outPlug() )
	{
		ofxRenderBufferPlug()->hash( h );
	}
}

Imath::Box2i OFXImageNode::computeDataWindow( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	ImagePlug::GlobalScope globalScope( context );
	IECore::ConstCompoundObjectPtr renderBuffer = ofxRenderBufferPlug()->getValue();
	if( !renderBuffer )
	{
		return inPlug()->dataWindowPlug()->getValue();
	}
	Box2iDataPtr dataWindowData = runTimeCast<Box2iData>(
		const_cast<Data*>( renderBuffer->member<Data>( "dataWindow" ) )
	);
	if( dataWindowData )
	{
		return dataWindowData->readable();
	}
	return inPlug()->dataWindowPlug()->getValue();
}

IECore::ConstCompoundDataPtr OFXImageNode::computeMetadata( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return outPlug()->metadataPlug()->defaultValue();
}

bool OFXImageNode::computeDeep( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return false;
}

void OFXImageNode::hashSampleOffsets( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	h = ImagePlug::emptyTileSampleOffsets()->Object::hash();
}

IECore::ConstIntVectorDataPtr OFXImageNode::computeSampleOffsets( const Imath::V2i &tileOrigin, const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return ImagePlug::flatTileSampleOffsets();
}

void OFXImageNode::hashChannelNames( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageProcessor::hashChannelNames( output, context, h );
	if( output == outPlug() )
	{
		ofxRenderBufferPlug()->hash( h );
	}
}

IECore::ConstStringVectorDataPtr OFXImageNode::computeChannelNames( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	ImagePlug::GlobalScope globalScope( context );
	IECore::ConstCompoundObjectPtr renderBuffer = ofxRenderBufferPlug()->getValue();
	if( !renderBuffer )
	{
		return inPlug()->channelNamesPlug()->getValue();
	}
	vector<string> names;
	if( renderBuffer->member<Data>( "R" ) ) names.push_back( "R" );
	if( renderBuffer->member<Data>( "G" ) ) names.push_back( "G" );
	if( renderBuffer->member<Data>( "B" ) ) names.push_back( "B" );
	if( renderBuffer->member<Data>( "A" ) ) names.push_back( "A" );
	if( names.empty() )
	{
		return inPlug()->channelNamesPlug()->getValue();
	}
	return new StringVectorData( names );
}

void OFXImageNode::hashChannelData( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageProcessor::hashChannelData( output, context, h );
	if( output == outPlug() )
	{
		if( !m_instance )
		{
			const std::string channelName = context->get<std::string>( ImagePlug::channelNameContextName );
			const Imath::V2i tileOrigin = context->get<V2i>( ImagePlug::tileOriginContextName );
			h.append( inPlug()->channelDataHash( channelName, tileOrigin ) );
		}
		else
		{
			ofxRenderBufferPlug()->hash( h );
		}
		h.append( context->get<V2i>( ImagePlug::tileOriginContextName ) );
		h.append( context->get<std::string>( ImagePlug::channelNameContextName ) );
	}
}

IECore::ConstFloatVectorDataPtr OFXImageNode::computeChannelData( const std::string &channelName, const Imath::V2i &tileOrigin, const Gaffer::Context *context, const ImagePlug *parent ) const
{
	// Read the full cached render buffer in global scope
	ImagePlug::GlobalScope globalScope( context );

	if( !m_instance )
	{
		return inPlug()->channelData( channelName, tileOrigin );
	}

	IECore::ConstCompoundObjectPtr renderBuffer = ofxRenderBufferPlug()->getValue();

	if( !renderBuffer )
	{
		return ImagePlug::emptyTile();
	}

	// Determine which channel to extract
	const std::string channelKey = ( channelName == "R" || channelName == "G" || channelName == "B" || channelName == "A" )
		? channelName : "";

	if( channelKey.empty() )
	{
		return ImagePlug::emptyTile();
	}

	FloatVectorDataPtr channelData = runTimeCast<FloatVectorData>(
		const_cast<Data*>( renderBuffer->member<Data>( channelKey ) )
	);

	Box2iDataPtr dataWindowData = runTimeCast<Box2iData>(
		const_cast<Data*>( renderBuffer->member<Data>( "dataWindow" ) )
	);

	if( !channelData || !dataWindowData )
	{
		return ImagePlug::emptyTile();
	}

	const Box2i &dataWindow = dataWindowData->readable();
	int width = dataWindow.size().x;
	const vector<float> &values = channelData->readable();

	// Extract the 64x64 tile region
	FloatVectorDataPtr tileData = new FloatVectorData();
	vector<float> &tile = tileData->writable();
	tile.reserve( ImagePlug::tileSize() * ImagePlug::tileSize() );

	Box2i tileBound(
		V2i( tileOrigin.x, tileOrigin.y ),
		V2i( tileOrigin.x + ImagePlug::tileSize(), tileOrigin.y + ImagePlug::tileSize() )
	);

	for( int y = tileOrigin.y; y < tileOrigin.y + ImagePlug::tileSize(); ++y )
	{
		for( int x = tileOrigin.x; x < tileOrigin.x + ImagePlug::tileSize(); ++x )
		{
			if( dataWindow.intersects( V2i( x, y ) ) )
			{
				int srcIdx = ( y - dataWindow.min.y ) * width + ( x - dataWindow.min.x );
				if( srcIdx < (int)values.size() )
				{
					tile.push_back( values[srcIdx] );
				}
				else
				{
					tile.push_back( 0.0f );
				}
			}
			else
			{
				tile.push_back( 0.0f );
			}
		}
	}

	return tileData;
}

void OFXImageNode::hashOfxRenderBuffer( const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImagePlug::GlobalScope globalScope( context );

	if( !m_instance )
	{
		// No valid OFX instance — pass through input data.
		// Hash input plugs to invalidate cache when input changes.
		pluginIdPlug()->hash( h );
		inPlug()->formatPlug()->hash( h );
		inPlug()->dataWindowPlug()->hash( h );
		inPlug()->channelNamesPlug()->hash( h );
		return;
	}

	// Hash input metadata
	inPlug()->formatPlug()->hash( h );
	inPlug()->dataWindowPlug()->hash( h );
	inPlug()->channelNamesPlug()->hash( h );

	// Hash all input channel data tiles, since the OFX render
	// processes all channels over the entire data window
	Box2i dataWindow = inPlug()->dataWindowPlug()->getValue();
	if( dataWindow.size().x > 0 && dataWindow.size().y > 0 )
	{
		IECore::ConstStringVectorDataPtr channelNamesData = inPlug()->channelNamesPlug()->getValue();
		const auto &channelNames = channelNamesData->readable();
		for( int y = dataWindow.min.y; y < dataWindow.max.y; y += ImagePlug::tileSize() )
		{
			for( int x = dataWindow.min.x; x < dataWindow.max.x; x += ImagePlug::tileSize() )
			{
				V2i tileOrigin( x, y );
				for( const auto &channel : channelNames )
				{
					h.append( inPlug()->channelDataHash( channel, tileOrigin ) );
				}
			}
		}
	}

	// Hash parameters and plugin ID
	pluginIdPlug()->hash( h );
	for( const auto &child : parametersPlug()->children() )
	{
		if( auto *valuePlug = runTimeCast<const ValuePlug>( child.get() ) )
		{
			valuePlug->hash( h );
		}
	}
}

IECore::ConstCompoundObjectPtr OFXImageNode::computeOfxRenderBuffer( const Gaffer::Context *context ) const
{
	std::lock_guard<std::mutex> lock( m_renderMutex );
	CompoundObjectPtr result = new CompoundObject();

	if( !m_instance )
	{
		return result;
	}

	// Get input image data
	ImagePlug::GlobalScope globalScope( context );

	GafferOFX::ClipInstance* sourceClip = dynamic_cast<GafferOFX::ClipInstance*>( m_instance->getClip( "Source" ) );
	GafferOFX::ClipInstance* outputClip = dynamic_cast<GafferOFX::ClipInstance*>( m_instance->getClip( "Output" ) );

	OfxTime frame = context->getFrame();
	OfxPointD renderScale;
	renderScale.x = renderScale.y = 1.0;

	Box2i dataWindow;
	Format format;
	int width = 0, height = 0;
	auto frameBuffer = std::unique_ptr<OfxRGBAColourF[]>();

	const bool hasInput = inPlug()->getInput() != nullptr;

	if( sourceClip )
	{
		sourceClip->setConnected( hasInput );
	}

	if( hasInput && sourceClip )
	{
		// Filter: use input image data
		format = inPlug()->formatPlug()->getValue();
		dataWindow = inPlug()->dataWindowPlug()->getValue();
		ConstStringVectorDataPtr channelNamesData = inPlug()->channelNamesPlug()->getValue();

		width = dataWindow.size().x;
		height = dataWindow.size().y;

		if( width <= 0 || height <= 0 )
		{
			dataWindow = Box2i( V2i( 0, 0 ), V2i( (int)format.width(), (int)format.height() ) );
			width = dataWindow.size().x;
			height = dataWindow.size().y;
		}

		const vector<string> &channelNames = channelNamesData->readable();
		bool hasR = find( channelNames.begin(), channelNames.end(), "R" ) != channelNames.end();
		bool hasG = find( channelNames.begin(), channelNames.end(), "G" ) != channelNames.end();
		bool hasB = find( channelNames.begin(), channelNames.end(), "B" ) != channelNames.end();
		bool hasA = find( channelNames.begin(), channelNames.end(), "A" ) != channelNames.end();

		frameBuffer = std::make_unique<OfxRGBAColourF[]>( width * height );

		for( int i = 0; i < width * height; ++i )
		{
			frameBuffer[i].r = 0.0f;
			frameBuffer[i].g = 0.0f;
			frameBuffer[i].b = 0.0f;
			frameBuffer[i].a = hasA ? 0.0f : 1.0f;
		}

		if( hasR )
		{
			Sampler rSampler( inPlug(), "R", dataWindow );
			for( int y = dataWindow.min.y; y < dataWindow.max.y; ++y )
				for( int x = dataWindow.min.x; x < dataWindow.max.x; ++x )
				{
					int idx = ( y - dataWindow.min.y ) * width + ( x - dataWindow.min.x );
					frameBuffer[idx].r = rSampler.sample( x, y );
				}
		}
		if( hasG )
		{
			Sampler gSampler( inPlug(), "G", dataWindow );
			for( int y = dataWindow.min.y; y < dataWindow.max.y; ++y )
				for( int x = dataWindow.min.x; x < dataWindow.max.x; ++x )
				{
					int idx = ( y - dataWindow.min.y ) * width + ( x - dataWindow.min.x );
					frameBuffer[idx].g = gSampler.sample( x, y );
				}
		}
		if( hasB )
		{
			Sampler bSampler( inPlug(), "B", dataWindow );
			for( int y = dataWindow.min.y; y < dataWindow.max.y; ++y )
				for( int x = dataWindow.min.x; x < dataWindow.max.x; ++x )
				{
					int idx = ( y - dataWindow.min.y ) * width + ( x - dataWindow.min.x );
					frameBuffer[idx].b = bSampler.sample( x, y );
				}
		}
		if( hasA )
		{
			Sampler aSampler( inPlug(), "A", dataWindow );
			for( int y = dataWindow.min.y; y < dataWindow.max.y; ++y )
				for( int x = dataWindow.min.x; x < dataWindow.max.x; ++x )
				{
					int idx = ( y - dataWindow.min.y ) * width + ( x - dataWindow.min.x );
					frameBuffer[idx].a = aSampler.sample( x, y );
				}
		}

		sourceClip->setExternalBuffer( frameBuffer.get(), width, height );
	}
	else
	{
		// Generator: use OFX plugin's RoD to determine output size
		OfxRectD rod;
		if( outputClip )
		{
			m_instance->getRegionOfDefinitionAction( frame, renderScale, rod );
		}
		else
		{
			rod.x1 = rod.y1 = 0;
			rod.x2 = rod.y2 = 720;
		}
		dataWindow = Box2i(
			V2i( (int)rod.x1, (int)rod.y1 ),
			V2i( (int)rod.x2, (int)rod.y2 )
		);
		width = dataWindow.size().x;
		height = dataWindow.size().y;
		if( width <= 0 || height <= 0 )
		{
			dataWindow = Box2i( V2i( 0, 0 ), V2i( 1920, 1080 ) );
			width = 1920;
			height = 1080;
		}
		format = Format( width, height );
	}

	// Set the render window on clips so getRegionOfDefinition returns correct bounds
	OfxRectD renderWindowD;
	renderWindowD.x1 = dataWindow.min.x;
	renderWindowD.y1 = dataWindow.min.y;
	renderWindowD.x2 = dataWindow.max.x;
	renderWindowD.y2 = dataWindow.max.y;

	if( hasInput && sourceClip )
	{
		sourceClip->setRenderWindow( renderWindowD );
	}
	if( outputClip )
	{
		outputClip->setRenderWindow( renderWindowD );
	}

	// Set up render parameters
	OfxRectI renderWindow;
	renderWindow.x1 = dataWindow.min.x;
	renderWindow.y1 = dataWindow.min.y;
	renderWindow.x2 = dataWindow.max.x;
	renderWindow.y2 = dataWindow.max.y;

	// Get region of interest and render
	OfxRectD regionOfInterest;
	regionOfInterest.x1 = dataWindow.min.x;
	regionOfInterest.y1 = dataWindow.min.y;
	regionOfInterest.x2 = dataWindow.max.x;
	regionOfInterest.y2 = dataWindow.max.y;

	std::map<OFX::Host::ImageEffect::ClipInstance *, OfxRectD> rois;
	m_instance->getRegionOfInterestAction( frame, renderScale, regionOfInterest, rois );

		// Initialize output clip properties before render
		if( outputClip )
		{
			outputClip->getProps().setStringProperty( kOfxImageEffectPropPixelDepth, kOfxBitDepthFloat );
			outputClip->getProps().setStringProperty( kOfxImageEffectPropComponents, kOfxImageComponentRGBA );
		}
		if( sourceClip )
		{
			sourceClip->getProps().setStringProperty( kOfxImageEffectPropPixelDepth, kOfxBitDepthFloat );
			sourceClip->getProps().setStringProperty( kOfxImageEffectPropComponents, kOfxImageComponentRGBA );
		}

		// Let the plugin customize clip preferences
		m_instance->getClipPreferences();

		// Pre-allocate the output image
		if( outputClip )
		{
			outputClip->getImage( frame, nullptr );
		}

		m_instance->beginRenderAction( frame, frame, 1.0, false, renderScale, true, false );
		OfxStatus r = m_instance->renderAction( frame, kOfxImageFieldBoth, renderWindow, renderScale, true, false, false );
		m_instance->endRenderAction( frame, frame, 1.0, false, renderScale, true, false );
		std::cerr << "DEBUG renderResult=" << r << std::endl;

	if( outputClip )
	{
		GafferOFX::Image* outputImage = outputClip->getOutputImage();
		if( outputImage )
		{
			OfxRectI outputBounds = outputImage->getBounds();
			int outWidth = outputBounds.x2 - outputBounds.x1;
			int outHeight = outputBounds.y2 - outputBounds.y1;

			// De-interleave output buffer into separate channel FloatVectorData
			FloatVectorDataPtr rData = new FloatVectorData();
			FloatVectorDataPtr gData = new FloatVectorData();
			FloatVectorDataPtr bData = new FloatVectorData();
			FloatVectorDataPtr aData = new FloatVectorData();
			vector<float> &rVec = rData->writable();
			vector<float> &gVec = gData->writable();
			vector<float> &bVec = bData->writable();
			vector<float> &aVec = aData->writable();

			rVec.resize( outWidth * outHeight );
			gVec.resize( outWidth * outHeight );
			bVec.resize( outWidth * outHeight );
			aVec.resize( outWidth * outHeight );

			for( int y = outputBounds.y1; y < outputBounds.y2; ++y )
			{
				for( int x = outputBounds.x1; x < outputBounds.x2; ++x )
				{
					OfxRGBAColourF* pixel = outputImage->pixel( x, y );
					if( pixel )
					{
						int idx = ( y - outputBounds.y1 ) * outWidth + ( x - outputBounds.x1 );
						rVec[idx] = pixel->r;
						gVec[idx] = pixel->g;
						bVec[idx] = pixel->b;
						aVec[idx] = pixel->a;
					}
				}
			}

			result->members()["R"] = rData;
			result->members()["G"] = gData;
			result->members()["B"] = bData;
			result->members()["A"] = aData;
		}
	}

	// Store data window and pixel aspect
	Box2iDataPtr dataWindowData = new Box2iData( dataWindow );
	result->members()["dataWindow"] = dataWindowData;

	FloatDataPtr parData = new FloatData( format.getPixelAspect() );
	result->members()["pixelAspect"] = parData;

	return result;
}

const GafferOFX::EffectImageInstance* OFXImageNode::effectInstance() const
{
	return m_instance.get();
}
