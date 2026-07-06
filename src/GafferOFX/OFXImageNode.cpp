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

#ifdef OFX_SUPPORTS_OPENGLRENDER
// Include GLEW before any Gaffer/OFX headers to avoid X11 macro conflicts.
// GLEW includes GL/gl.h, which may pull in X11/Xlib.h.
#include <GL/glew.h>
// GLEW #undef's GLAPI at the end; OSMesa needs it for declarations.
// We define them AFTER including all Gaffer headers (which also use GLAPI).

#include <iostream>

#include "GafferOFX/GLContextManager.h"
#endif

#include "GafferOFX/OFXImageNode.h"
#include "GafferOFX/Host.h"
#include "GafferOFX/ClipInstance.h"
#include "GafferOFX/OFXInteractInstance.h"

#include "Gaffer/Context.h"
#include "Gaffer/Metadata.h"
#include "Gaffer/ArrayPlug.h"
// CompoundObjectPlug provided via OFXImageNode.h which includes Gaffer/TypedObjectPlug.h

#include "GafferImage/ImageAlgo.h"
#include "GafferImage/Sampler.h"

#include "IECore/BoxOps.h"

#ifdef OFX_SUPPORTS_OPENGLRENDER
#include "ofxGPURender.h"
// GLEW #undef's GLAPI at the end; Gaffer only uses GLuint types (from GL/gl.h),
// not the GLAPI macro. OSMesa/GLX need GLAPI for their function declarations.
#ifndef GLAPI
#define GLAPI extern
#endif
#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif
#include <GL/osmesa.h>
#include <GL/glx.h>
#endif

#include <algorithm>
#include <cctype>
#include <exception>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

//////////////////////////////////////////////////////////////////////////
// Dedicated render worker thread
//
// OFX plugins (e.g. Shadertoy) create OSMesa contexts during render.
// By dispatching all renders to a single background thread, we:
//  - Keep the main thread responsive (no freeze)
//  - Reuse the OSMesa context across renders (shader compilation
//    happens once instead of per-tile)
//  - Avoid CPU saturation from concurrent OSMesa renders
//////////////////////////////////////////////////////////////////////////

class OFXRenderWorker
{
public:

	static OFXRenderWorker &instance()
	{
		static OFXRenderWorker w;
		return w;
	}

	template<typename F>
	void execute( F &&func )
	{
		std::unique_lock<std::mutex> lock( m_mutex );
		m_task = std::forward<F>( func );
		m_ready = true;
		m_cv.notify_one();
		m_cv.wait( lock, [this]() { return !m_ready; } );
	}

	~OFXRenderWorker()
	{
		{
			std::lock_guard<std::mutex> lock( m_mutex );
			m_done = true;
			m_ready = true;
		}
		m_cv.notify_one();
		if( m_thread.joinable() )
			m_thread.join();
	}

private:

	OFXRenderWorker()
	{
		m_thread = std::thread( [this]() { workerLoop(); } );
	}

	void workerLoop()
	{
		std::unique_lock<std::mutex> lock( m_mutex );
		while( !m_done )
		{
			m_cv.wait( lock, [this]() { return m_ready; } );
			if( m_done ) break;
			m_task();
			m_ready = false;
			m_cv.notify_one();
		}
	}

	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::function<void()> m_task;
	bool m_ready = false;
	bool m_done = false;
};

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
		destroyInteract();
		if( m_glContextAttached )
		{
			m_instance->contextDetachedAction();
			m_glContextAttached = false;
		}
		m_instance.reset();
		createPluginInstance();
	}
	else if( m_instance && parametersPlug()->isAncestorOf( plug ) )
	{
		if( m_rendering )
		{
			return;
		}
		if( m_settingFromPlugin )
		{
			return;
		}
		OfxTime time = 0.0;
		if( const Gaffer::Context *ctx = Gaffer::Context::current() )
		{
			time = ctx->getFrame();
		}
		OfxPointD renderScale = { 1.0, 1.0 };
		m_instance->beginInstanceChangedAction( kOfxChangeUserEdited );
		m_instance->paramInstanceChangedAction( plug->getName().string(), kOfxChangeUserEdited, time, renderScale );
		m_instance->endInstanceChangedAction( kOfxChangeUserEdited );
	}
}

OFXImageNode::~OFXImageNode()
{
}

bool OFXImageNode::createPluginInstance()
{
	if( m_instance )
		return true;

	m_clipPreferencesFetched = false;

	Host& host = Host::instance();
	std::string pluginId = pluginIdPlug()->getValue();
	auto plugin = host.m_pluginCache.getPluginById(pluginId);
	if( plugin )
	{
		// Remove clip plugs from any previous instance
		removeClipPlugs();

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

		// Create Gaffer plug for each non-Output, non-Source clip
		createClipPlugs();

		return true;
	}
	return false;
}

void OFXImageNode::removeClipPlugs()
{
	for( const auto &name : m_clipPlugNames )
	{
		if( auto *plug = getChild<GafferImage::ImagePlug>( name ) )
		{
			removeChild( plug );
		}
	}
	m_clipPlugNames.clear();
}

void OFXImageNode::createClipPlugs()
{
	if( !m_instance )
	{
		return;
	}

	for( int i = 0; i < m_instance->getNClips(); ++i )
	{
		auto *clip = dynamic_cast<GafferOFX::ClipInstance*>( m_instance->getNthClip( i ) );
		if( !clip )
		{
			continue;
		}

		const std::string &clipName = clip->getName();
		if( clipName == "Output" || clipName == "Source" )
		{
			continue;
		}

		// Derive Gaffer plug name from clip name (lowercase first letter)
		std::string plugName = clipName;
		if( !plugName.empty() && isupper( plugName[0] ) )
		{
			plugName[0] = tolower( plugName[0] );
		}

		// Reuse existing plug if present (e.g. after scene load or re-creation)
		auto *plug = getChild<GafferImage::ImagePlug>( plugName );
		if( !plug )
		{
			plug = new GafferImage::ImagePlug( plugName, Plug::In );
			addChild( plug );
		}
		m_clipPlugNames.push_back( plugName );

		// Reflect whether the plug already has a connection
		clip->setConnected( plug->getInput() != nullptr );
	}
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

	// Dynamic clip plugs (Mask, UV, etc.) affect the render buffer
	for( const auto &name : m_clipPlugNames )
	{
		if( auto *imgPlug = getChild<GafferImage::ImagePlug>( name ) )
		{
			if( input == imgPlug->formatPlug() || input == imgPlug->dataWindowPlug() ||
			    input == imgPlug->channelNamesPlug() || input == imgPlug->channelDataPlug() )
			{
				outputs.push_back( ofxRenderBufferPlug() );
			}
		}
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

	// Hash additional connected clip plugs
	for( const auto &name : m_clipPlugNames )
	{
		if( auto *imgPlug = getChild<GafferImage::ImagePlug>( name ) )
		{
			if( !imgPlug->getInput() )
			{
				continue;
			}
			imgPlug->formatPlug()->hash( h );
			imgPlug->dataWindowPlug()->hash( h );
			imgPlug->channelNamesPlug()->hash( h );
			Box2i clipDw = imgPlug->dataWindowPlug()->getValue();
			if( clipDw.size().x > 0 && clipDw.size().y > 0 )
			{
				IECore::ConstStringVectorDataPtr clipChannels = imgPlug->channelNamesPlug()->getValue();
				for( int yy = clipDw.min.y; yy < clipDw.max.y; yy += ImagePlug::tileSize() )
				{
					for( int xx = clipDw.min.x; xx < clipDw.max.x; xx += ImagePlug::tileSize() )
					{
						V2i tileOrigin( xx, yy );
						for( const auto &ch : clipChannels->readable() )
						{
							h.append( imgPlug->channelDataHash( ch, tileOrigin ) );
						}
					}
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

	// Include the frame in the hash so that time-varying OFX plugins
	// (whether they declare _frameVarying or not) correctly invalidate
	// the cache across frames.  Call getClipPreferences here to give
	// plugins a chance to set up frame-dependent state.
	if( m_instance )
	{
		if( !m_clipPreferencesFetched )
		{
			m_instance->getClipPreferences();
			m_clipPreferencesFetched = true;
		}
		h.append( context->getFrame() );
	}
}

namespace
{

/// Read RGBA channel data from an ImagePlug into an interleaved RGBA buffer.
void readPlugToRGBA( const GafferImage::ImagePlug *plug, OfxRGBAColourF *buffer, const Box2i &dataWindow, int width )
{
	GafferImage::Sampler rSampler( plug, "R", dataWindow );
	GafferImage::Sampler gSampler( plug, "G", dataWindow );
	GafferImage::Sampler bSampler( plug, "B", dataWindow );

	ConstStringVectorDataPtr channelNamesData = plug->channelNamesPlug()->getValue();
	bool hasAlpha = false;
	for( const auto &ch : channelNamesData->readable() )
	{
		if( ch == "A" )
		{
			hasAlpha = true;
			break;
		}
	}

	std::unique_ptr<GafferImage::Sampler> aSampler;
	if( hasAlpha )
	{
		aSampler = std::make_unique<GafferImage::Sampler>( plug, "A", dataWindow );
	}

	for( int y = dataWindow.min.y; y < dataWindow.max.y; ++y )
	{
		for( int x = dataWindow.min.x; x < dataWindow.max.x; ++x )
		{
			int idx = ( y - dataWindow.min.y ) * width + ( x - dataWindow.min.x );
			buffer[idx].r = rSampler.sample( x, y );
			buffer[idx].g = gSampler.sample( x, y );
			buffer[idx].b = bSampler.sample( x, y );
			buffer[idx].a = aSampler ? aSampler->sample( x, y ) : 1.0f;
		}
	}
}

} // namespace

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

		frameBuffer = std::make_unique<OfxRGBAColourF[]>( width * height );

		for( int i = 0; i < width * height; ++i )
		{
			frameBuffer[i].r = 0.0f;
			frameBuffer[i].g = 0.0f;
			frameBuffer[i].b = 0.0f;
			frameBuffer[i].a = 1.0f;
		}

		readPlugToRGBA( inPlug(), frameBuffer.get(), dataWindow, width );

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

	// ----- Feed additional input clips (Mask, UV, etc.) -----
	// Keep a vector of all per-clip frame buffers so they stay alive
	// throughout the render.
	struct ClipBuffer
	{
		GafferOFX::ClipInstance *clip;
		std::unique_ptr<OfxRGBAColourF[]> buffer;
	};
	std::vector<ClipBuffer> extraClipBuffers;

	for( const auto &plugName : m_clipPlugNames )
	{
		// Capitalise first letter to match OFX clip name
		std::string ofxClipName = plugName;
		if( !ofxClipName.empty() && islower( ofxClipName[0] ) )
		{
			ofxClipName[0] = toupper( ofxClipName[0] );
		}

		auto *clip = dynamic_cast<GafferOFX::ClipInstance*>( m_instance->getClip( ofxClipName ) );
		if( !clip )
		{
			continue;
		}

		auto *plug = getChild<GafferImage::ImagePlug>( plugName );
		if( !plug || !plug->getInput() )
		{
			clip->setConnected( false );
			continue;
		}

		clip->setConnected( true );

		Box2i clipDw = plug->dataWindowPlug()->getValue();
		if( clipDw.size().x <= 0 || clipDw.size().y <= 0 )
		{
			continue;
		}

		int clipWidth = clipDw.size().x;
		int clipHeight = clipDw.size().y;

		auto buf = std::make_unique<OfxRGBAColourF[]>( clipWidth * clipHeight );
		readPlugToRGBA( plug, buf.get(), clipDw, clipWidth );

		clip->setExternalBuffer( buf.get(), clipWidth, clipHeight );
		OfxRectD clipRod;
		clipRod.x1 = clipDw.min.x;
		clipRod.y1 = clipDw.min.y;
		clipRod.x2 = clipDw.max.x;
		clipRod.y2 = clipDw.max.y;
		clip->setRenderWindow( clipRod );

		extraClipBuffers.push_back( { clip, std::move( buf ) } );
	}

	// Set up render parameters
	OfxRectI renderWindow;
	renderWindow.x1 = dataWindow.min.x;
	renderWindow.y1 = dataWindow.min.y;
	renderWindow.x2 = dataWindow.max.x;
	renderWindow.y2 = dataWindow.max.y;

	// Check if the effect is identity (e.g. FrameHold pass-through at a different frame)
	{
		OfxTime identityTime = frame;
		std::string identityClip;
		if( m_instance->isIdentityAction( identityTime, kOfxImageFieldNone, renderWindow, renderScale, identityClip ) == kOfxStatOK )
		{
			if( identityClip == "Source" && hasInput && sourceClip )
			{
				// Read source at identityTime and return as output
				Gaffer::ContextPtr idContext = new Gaffer::Context( *context );
				idContext->setFrame( identityTime );
				Gaffer::Context::Scope idScope( idContext.get() );

				GafferImage::Format idFormat = inPlug()->formatPlug()->getValue();
				Box2i idDw = inPlug()->dataWindowPlug()->getValue();
				int idWidth = idDw.size().x;
				int idHeight = idDw.size().y;
				if( idWidth > 0 && idHeight > 0 )
				{
					auto idBuf = std::make_unique<OfxRGBAColourF[]>( idWidth * idHeight );
					readPlugToRGBA( inPlug(), idBuf.get(), idDw, idWidth );

					FloatVectorDataPtr rData = new FloatVectorData();
					FloatVectorDataPtr gData = new FloatVectorData();
					FloatVectorDataPtr bData = new FloatVectorData();
					FloatVectorDataPtr aData = new FloatVectorData();
					vector<float> &rVec = rData->writable();
					vector<float> &gVec = gData->writable();
					vector<float> &bVec = bData->writable();
					vector<float> &aVec = aData->writable();
					rVec.resize( idWidth * idHeight );
					gVec.resize( idWidth * idHeight );
					bVec.resize( idWidth * idHeight );
					aVec.resize( idWidth * idHeight );
					for( int i = 0; i < idWidth * idHeight; ++i )
					{
						rVec[i] = idBuf[i].r;
						gVec[i] = idBuf[i].g;
						bVec[i] = idBuf[i].b;
						aVec[i] = idBuf[i].a;
					}
					result->members()["R"] = rData;
					result->members()["G"] = gData;
					result->members()["B"] = bData;
					result->members()["A"] = aData;
				result->members()["dataWindow"] = new Box2iData( idDw );
				result->members()["pixelAspect"] = new FloatData( idFormat.getPixelAspect() );
				}
				return result;
			}
		}
	}

	// ---- Temporal clip access: pre-fetch frames needed by the plugin ----
	if( hasInput && sourceClip && m_instance->temporalAccess() )
	{
		OFX::Host::ImageEffect::RangeMap rangeMap;
		if( m_instance->getFrameNeededAction( frame, rangeMap ) == kOfxStatOK )
		{
			auto srcIt = rangeMap.find( sourceClip );
			if( srcIt != rangeMap.end() && !srcIt->second.empty() )
			{
				std::map<OfxTime, std::unique_ptr<OfxRGBAColourF[]>> cache;
				int cacheWidth = dataWindow.size().x;
				int cacheHeight = dataWindow.size().y;

				for( const auto &range : srcIt->second )
				{
					for( OfxTime t = range.min; t <= range.max; t += 1.0 )
					{
						if( cache.find( t ) != cache.end() )
							continue;

						// Current frame is already in external buffer, don't re-fetch
						if( t == frame )
							continue;

						auto buf = std::make_unique<OfxRGBAColourF[]>( cacheWidth * cacheHeight );
						// Fill with opaque black as fallback (matching current frame initialization)
						for( int i = 0; i < cacheWidth * cacheHeight; ++i )
						{
							buf[i].r = 0.0f;
							buf[i].g = 0.0f;
							buf[i].b = 0.0f;
							buf[i].a = 1.0f;
						}

						// Read the input plug at the requested time
						Gaffer::ContextPtr tContext = new Gaffer::Context( *context );
						tContext->setFrame( t );
						Gaffer::Context::Scope tScope( tContext.get() );

						readPlugToRGBA( inPlug(), buf.get(), dataWindow, cacheWidth );
						cache[t] = std::move( buf );
					}
				}

				if( !cache.empty() )
				{
					sourceClip->setFrameCache( std::move( cache ), cacheWidth, cacheHeight, dataWindow );
				}
			}
		}
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

		// Dispatch the render to our dedicated worker thread.
		// This keeps renders off the main thread (no UI freeze).
		//
		// We make the EGL/GLX/OSMesa context current on the worker thread
		// (for future GL rendering support) but do NOT call
		// contextAttachedAction — plugins that create their own GL context
		// (e.g. Shadertoy with OSMesa) do so internally during renderAction,
		// and CPU-only plugins render via clipGetImage into the pre-allocated
		// CPU buffer.
		OFXRenderWorker::instance().execute( [this, frame, renderScale, &renderWindow, outputClip, sourceClip]() {

			GLContextManager::instance().makeCurrent();

			// ---- Plugin render ----
			m_rendering = true;
			m_instance->beginRenderAction( frame, frame, 1.0, false, renderScale, true, true );
			m_instance->renderAction( frame, kOfxImageFieldNone, renderWindow, renderScale, true, true, false );
			m_instance->endRenderAction( frame, frame, 1.0, false, renderScale, true, true );
			m_rendering = false;
		} );

		// Clear frame cache after render
		if( sourceClip )
		{
			sourceClip->clearFrameCache();
		}

		if( outputClip )
		{
				GafferOFX::Image* outputImage = outputClip->getOutputImage();
				if( outputImage )
				{
					OfxRectI outputBounds = outputImage->getBounds();
					int cpuOutWidth = outputBounds.x2 - outputBounds.x1;
					int cpuOutHeight = outputBounds.y2 - outputBounds.y1;

					// De-interleave output buffer into separate channel FloatVectorData
					FloatVectorDataPtr rData = new FloatVectorData();
					FloatVectorDataPtr gData = new FloatVectorData();
					FloatVectorDataPtr bData = new FloatVectorData();
					FloatVectorDataPtr aData = new FloatVectorData();
					vector<float> &rVec = rData->writable();
					vector<float> &gVec = gData->writable();
					vector<float> &bVec = bData->writable();
					vector<float> &aVec = aData->writable();

					rVec.resize( cpuOutWidth * cpuOutHeight );
					gVec.resize( cpuOutWidth * cpuOutHeight );
					bVec.resize( cpuOutWidth * cpuOutHeight );
					aVec.resize( cpuOutWidth * cpuOutHeight );

					for( int y = outputBounds.y1; y < outputBounds.y2; ++y )
					{
						for( int x = outputBounds.x1; x < outputBounds.x2; ++x )
						{
							OfxRGBAColourF* pixel = outputImage->pixel( x, y );
							if( pixel )
							{
								int idx = ( y - outputBounds.y1 ) * cpuOutWidth + ( x - outputBounds.x1 );
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

bool OFXImageNode::hasOverlay() const
{
	if( !m_instance )
		return false;
	// Calling getOverlayDescriptor() triggers kOfxActionDescribe on the
	// overlay interact via the Context descriptor, which reads the
	// overlay interact main entry from the Context descriptor's
	// properties (set during DescribeInContext by the plugin's
	// setOverlayInteractDescriptor() call).
	OFX::Host::Interact::Descriptor &desc = m_instance->getOverlayDescriptor();
	OFX::Host::Interact::State state = desc.getState();
	return state == OFX::Host::Interact::eDescribed
		|| state == OFX::Host::Interact::eCreated;
}

GafferOFXInteractInstance* OFXImageNode::getInteract()
{
	if( !m_interactInstance && m_instance && hasOverlay() )
	{
		// 32-bit float, hasAlpha=true — matches the float RGBA clips used throughout.
		m_interactInstance = std::make_unique<GafferOFXInteractInstance>( *m_instance, 32, true );
		m_interactInstance->createInstance();
	}
	return m_interactInstance.get();
}

void OFXImageNode::destroyInteract()
{
	m_interactInstance.reset();
}
