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
#include <GL/gl.h>
#include <GL/glext.h>

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
// All renders are dispatched to a single persistent background thread:
//  - Keeps the main thread responsive (no freeze)
//  - GL plugins reuse their context (no per-tile shader recompilation)
//  - Avoids CPU saturation from concurrent renders
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
		// Re-entrancy guard: if the worker thread itself calls execute()
		// (e.g. via a Gaffer pull inside renderAction), run inline to
		// avoid self-deadlock on m_mutex.
		if( std::this_thread::get_id() == m_thread.get_id() )
		{
			func();
			return;
		}
		std::unique_lock<std::mutex> lock( m_mutex );
		m_task = std::forward<F>( func );
		m_ready = true;
		m_cv.notify_one();
		m_cv.wait( lock, [this]() { return !m_ready; } );
		if( m_exception )
		{
			auto ex = std::move( m_exception );
			std::rethrow_exception( ex );
		}
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
			try
			{
				m_task();
			}
			catch( ... )
			{
				m_exception = std::current_exception();
			}
			m_ready = false;
			m_cv.notify_one();
		}
	}

	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::function<void()> m_task;
	std::exception_ptr m_exception;
	bool m_ready = false;
	bool m_done = false;
};

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace GafferImage;
using namespace Gaffer;
using namespace GafferOFX;

// RAII guard that sets m_rendering = true and resets on scope exit.
struct RenderingGuard
{
	std::atomic<bool> &flag;
	RenderingGuard( std::atomic<bool> &f ) : flag( f ) { flag = true; }
	~RenderingGuard() { flag = false; }
};

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
			// Marshal detach through worker so the GL context is current.
			// Only dispatch if the old effect instance has GL support.
			OFXRenderWorker::instance().execute( [this]() {
				GLContextManager::instance().makeCurrent();
				m_instance->contextDetachedAction();
			} );
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
		// Params can legitimately change clip prefs; re-evaluate.
		m_instance->getClipPreferences();
	}
}

OFXImageNode::~OFXImageNode()
{
}

bool OFXImageNode::createPluginInstance()
{
	if( m_instance )
		return true;

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

		// Verify this plugin supports float pixel depth.  GafferOFX renders
		// everything in 32-bit float and does not perform byte conversion.
		// Query the plugin DESCRIPTOR's property set (not the instance's) —
		// the instance props don't reliably chain for string-property lookups.
		{
			bool supportsFloat = false;
			const auto &dp = m_instance->getPlugin()->getDescriptor().getProps();
			try
			{
				int n = dp.getDimension( kOfxImageEffectPropSupportedPixelDepths );
				for( int i = 0; i < n && !supportsFloat; ++i )
					supportsFloat = dp.getStringProperty( kOfxImageEffectPropSupportedPixelDepths, i ) == kOfxBitDepthFloat;
			}
			catch( const std::exception & ) {}
			if( !supportsFloat )
			{
				std::cerr << "GafferOFX: rejecting plugin \"" << m_instance->getPlugin()->getIdentifier()
				          << "\" — does not advertise kOfxBitDepthFloat in kOfxImageEffectPropSupportedPixelDepths"
				          << std::endl;
				m_instance.reset();
				return false;
			}
		}

		// Give plugins a chance to set up frame-dependent state once at
		// instantiation, not during hashing (which must be side-effect-free).
		m_instance->getClipPreferences();

		// Detect whether this plugin supports tiled rendering.
		// GL plugins always use full-frame (FBO/readback overhead).
		// CPU plugins with SupportsTiles=1 can render per-tile.
		// CImg plugins claim supportsTiles but assert srcRoD.x1 == dstRoD.x1,
		// making them incompatible with tile-sized render windows.
		m_tiledRenderSupported = false;
		{
			bool pluginSupportsGL = false;
			try
			{
				std::string val = m_instance->getPlugin()->getDescriptor().getProps().getStringProperty(
					kOfxImageEffectPropOpenGLRenderSupported
				);
				pluginSupportsGL = ( val == "true" || val == "needed" );
			}
			catch( const std::exception & ) {}
			if( !pluginSupportsGL && m_instance->supportsTiles() )
			{
				std::string pluginId = m_instance->getPlugin()->getIdentifier();
				bool isCImg = pluginId.find( "net.sf.cimg." ) == 0 || pluginId.find( "eu.cimg." ) == 0;
				if( !isCImg )
				{
					m_tiledRenderSupported = true;
				}
			}
		}

		// Dynamic getConnected() looks up plug input at call time

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

		// Store the plug name on the clip for dynamic getConnected() lookup
		clip->setPlugName( plugName );
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

	if( m_tiledRenderSupported )
	{
		// ---- Tiled path: per-tile channelData renders directly ----
		//
		// Input format/dataWindow/channelNames metadata affects the
		// render buffer (used for output format/dataWindow/channelNames).
		if( input == inPlug()->formatPlug() || input == inPlug()->dataWindowPlug() || input == inPlug()->channelNamesPlug() )
		{
			outputs.push_back( ofxRenderBufferPlug() );
		}

		// Input channel data directly affects output channel data
		// (bypassing __ofxRenderBuffer, enabling per-tile renders).
		if( input == inPlug()->channelDataPlug() )
		{
			outputs.push_back( outPlug()->channelDataPlug() );
		}

		// Dynamic clip plugs affect output channel data directly.
		for( const auto &name : m_clipPlugNames )
		{
			if( auto *imgPlug = getChild<GafferImage::ImagePlug>( name ) )
			{
				if( input == imgPlug->formatPlug() || input == imgPlug->dataWindowPlug() ||
				    input == imgPlug->channelNamesPlug() )
				{
					outputs.push_back( ofxRenderBufferPlug() );
				}
				if( input == imgPlug->channelDataPlug() )
				{
					outputs.push_back( outPlug()->channelDataPlug() );
				}
			}
		}

		// Parameters and plugin ID affect both the render buffer
		// (for metadata) and channel data (for per-tile renders).
		if( parametersPlug()->isAncestorOf( input ) )
		{
			outputs.push_back( ofxRenderBufferPlug() );
			outputs.push_back( outPlug()->channelDataPlug() );
		}
		if( input == pluginIdPlug() )
		{
			outputs.push_back( ofxRenderBufferPlug() );
			outputs.push_back( outPlug()->channelDataPlug() );
		}

		// Render buffer affects metadata outputs only (not channelData).
		if( input == ofxRenderBufferPlug() )
		{
			outputs.push_back( outPlug()->formatPlug() );
			outputs.push_back( outPlug()->dataWindowPlug() );
			outputs.push_back( outPlug()->channelNamesPlug() );
		}
	}
	else
	{
		// ---- Full-frame path: everything goes via render buffer ----
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

	if( m_tiledRenderSupported )
	{
		// Tiled path: output is always RGBA.
		vector<string> names = { "R", "G", "B", "A" };
		return new StringVectorData( names );
	}

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
		else if( m_tiledRenderSupported )
		{
			// Tiled path: each tile has its own cache key based on
			// params, input data, frame, tile origin, and channel.
			const Imath::V2i tileOrigin = context->get<V2i>( ImagePlug::tileOriginContextName );
			h.append( tileOrigin );
			h.append( context->get<std::string>( ImagePlug::channelNameContextName ) );

			pluginIdPlug()->hash( h );
			for( const auto &child : parametersPlug()->children() )
			{
				if( auto *valuePlug = runTimeCast<const ValuePlug>( child.get() ) )
				{
					valuePlug->hash( h );
				}
			}
			h.append( context->getFrame() );

			// Hash the full input data window to capture all pixels
			// the plugin might read for this tile.  This is conservative
			// (over-invalidates) but correct.  A future optimization
			// could call getRegionOfInterestAction to narrow the hash.
			Box2i dataWindow = inPlug()->dataWindowPlug()->getValue();
			if( dataWindow.size().x > 0 && dataWindow.size().y > 0 )
			{
				IECore::ConstStringVectorDataPtr channelNamesData = inPlug()->channelNamesPlug()->getValue();
				const auto &channels = channelNamesData->readable();
				for( int y = dataWindow.min.y; y < dataWindow.max.y; y += ImagePlug::tileSize() )
				{
					for( int x = dataWindow.min.x; x < dataWindow.max.x; x += ImagePlug::tileSize() )
					{
						V2i srcTileOrigin( x, y );
						for( const auto &ch : channels )
						{
							h.append( inPlug()->channelDataHash( ch, srcTileOrigin ) );
						}
					}
				}
			}

			// Hash connected clip plugs' data windows
			for( const auto &name : m_clipPlugNames )
			{
				if( auto *imgPlug = getChild<GafferImage::ImagePlug>( name ) )
				{
					if( !imgPlug->getInput() ) continue;
					Box2i clipDw = imgPlug->dataWindowPlug()->getValue();
					if( clipDw.size().x > 0 && clipDw.size().y > 0 )
					{
						IECore::ConstStringVectorDataPtr clipChannels = imgPlug->channelNamesPlug()->getValue();
						for( int yy = clipDw.min.y; yy < clipDw.max.y; yy += ImagePlug::tileSize() )
						{
							for( int xx = clipDw.min.x; xx < clipDw.max.x; xx += ImagePlug::tileSize() )
							{
								V2i clipTileOrigin( xx, yy );
								for( const auto &ch : clipChannels->readable() )
								{
									h.append( imgPlug->channelDataHash( ch, clipTileOrigin ) );
								}
							}
						}
					}
				}
			}
		}
		else
		{
			ofxRenderBufferPlug()->hash( h );
			h.append( context->get<V2i>( ImagePlug::tileOriginContextName ) );
			h.append( context->get<std::string>( ImagePlug::channelNameContextName ) );
		}
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

	if( m_tiledRenderSupported && inPlug()->getInput() )
	{
		return computeTiledChannelData( channelName, tileOrigin, context );
	}

	// ---- Full-frame path (non-tiled plugins) ----
	IECore::ConstCompoundObjectPtr renderBuffer = ofxRenderBufferPlug()->getValue();

	if( !renderBuffer )
	{
		return ImagePlug::emptyTile();
	}

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

// -----------------------------------------------------------------------
// readPlugToRGBA moved to EffectImageInstance.cpp
// -----------------------------------------------------------------------

IECore::ConstFloatVectorDataPtr OFXImageNode::computeTiledChannelData( const std::string &channelName, const Imath::V2i &tileOrigin, const Gaffer::Context *context ) const
{
	std::lock_guard<std::mutex> lock( m_renderMutex );

	OfxTime frame = context->getFrame();
	OfxPointD renderScale = { 1.0, 1.0 };

	int ts = ImagePlug::tileSize();
	Box2i dataWindow = inPlug()->dataWindowPlug()->getValue();
	Box2i tileBound( tileOrigin, tileOrigin + V2i( ts, ts ) );
	Box2i renderBox(
		V2i( std::max( tileBound.min.x, dataWindow.min.x ), std::max( tileBound.min.y, dataWindow.min.y ) ),
		V2i( std::min( tileBound.max.x, dataWindow.max.x ), std::min( tileBound.max.y, dataWindow.max.y ) )
	);
	if( renderBox.size().x <= 0 || renderBox.size().y <= 0 )
	{
		return new FloatVectorData();
	}
	OfxRectD renderWindowD = { (double)renderBox.min.x, (double)renderBox.min.y,
	                           (double)renderBox.max.x, (double)renderBox.max.y };
	OfxRectI renderWindowI = { renderBox.min.x, renderBox.min.y,
	                           renderBox.max.x, renderBox.max.y };

	// Get region of interest for this tile
	std::map<OFX::Host::ImageEffect::ClipInstance *, OfxRectD> rois;
	m_instance->getRegionOfInterestAction( frame, renderScale, renderWindowD, rois );

	// Convert RoIs to name-based map for the invocation
	std::map<std::string, OfxRectD> clipRoIs;
	for( auto &[clipPtr, roi] : rois )
	{
		if( clipPtr )
			clipRoIs[clipPtr->getName()] = roi;
	}

	// Set up output clip pixel depth
	{
		auto *outputClip = dynamic_cast<GafferOFX::ClipInstance*>( m_instance->getClip( "Output" ) );
		if( outputClip )
		{
			outputClip->getProps().setStringProperty( kOfxImageEffectPropPixelDepth, kOfxBitDepthFloat );
			outputClip->getProps().setStringProperty( kOfxImageEffectPropComponents, kOfxImageComponentRGBA );
		}
		auto *sourceClip = dynamic_cast<GafferOFX::ClipInstance*>( m_instance->getClip( "Source" ) );
		if( sourceClip )
		{
			sourceClip->getProps().setStringProperty( kOfxImageEffectPropPixelDepth, kOfxBitDepthFloat );
			sourceClip->getProps().setStringProperty( kOfxImageEffectPropComponents, kOfxImageComponentRGBA );
		}
	}

	// Build half-open clip properties set for extra clips
	for( const auto &plugName : m_clipPlugNames )
	{
		std::string ofxClipName = plugName;
		if( !ofxClipName.empty() && islower( ofxClipName[0] ) )
			ofxClipName[0] = toupper( ofxClipName[0] );

		auto *clip = dynamic_cast<GafferOFX::ClipInstance*>( m_instance->getClip( ofxClipName ) );
		if( clip )
		{
			clip->getProps().setStringProperty( kOfxImageEffectPropPixelDepth, kOfxBitDepthFloat );
			clip->getProps().setStringProperty( kOfxImageEffectPropComponents, kOfxImageComponentRGBA );
		}
	}

	FloatVectorDataPtr tileData = new FloatVectorData();
	vector<float> &tile = tileData->writable();
	tile.resize( ImagePlug::tilePixels(), 0.0f );

	// Render inline on the compute thread — CPU renders are serialized by
	// m_renderMutex and re-entrant via the TLS invocation stack.  GL
	// plugins never reach this path (gated by m_tiledRenderSupported).
	{
		RenderingGuard _rg( m_rendering );

		Gaffer::ConstContextPtr ctxCopy = new Gaffer::Context( *Gaffer::Context::current() );
		OfxRectI rwI = renderWindowI;
		OFX::Host::ImageEffect::ClipInstance *outputClip = dynamic_cast<GafferOFX::ClipInstance*>(
			m_instance->getClip( "Output" )
		);

		double pw = dataWindow.size().x;
		double ph = dataWindow.size().y;
		m_instance->setProjectFormat( pw, ph );

		RenderInvocation inv;
		inv.time = frame;
		inv.renderWindow = rwI;
		inv.renderScale.x = renderScale.x;
		inv.renderScale.y = renderScale.y;
		inv.context = ctxCopy;
		inv.clipRoIs = clipRoIs;

		OFX::Host::ImageEffect::Image *sharedOutput = nullptr;
		OfxStatus renderStatus = kOfxStatOK;

		{
			RenderInvocationGuard guard( inv );

			m_instance->beginRenderAction( frame, frame, 1.0, false, renderScale, true, true );
			renderStatus = m_instance->renderAction( frame, kOfxImageFieldNone, renderWindowI, renderScale, true, true, false );
			m_instance->endRenderAction( frame, frame, 1.0, false, renderScale, true, true );

			// Read output tile while invocation is still active
			if( outputClip )
			{
				sharedOutput = inv.outputImage;
				if( sharedOutput )
					sharedOutput->addReference();
			}
		}

		// Propagate cancellation / check render status before touching data
		if( inv.exception )
		{
			if( sharedOutput ) sharedOutput->releaseReference();
			std::rethrow_exception( inv.exception );
		}
		if( renderStatus != kOfxStatOK && renderStatus != kOfxStatReplyDefault )
		{
			if( sharedOutput ) sharedOutput->releaseReference();
			throw IECore::Exception( "OFX renderAction failed" );
		}

		// De-interleave output into tileData
		if( sharedOutput )
		{
			int comp = -1;
			if( channelName == "R" ) comp = 0;
			else if( channelName == "G" ) comp = 1;
			else if( channelName == "B" ) comp = 2;
			else if( channelName == "A" ) comp = 3;

			if( comp >= 0 )
			{
				for( int y = rwI.y1; y < rwI.y2; ++y )
				{
					int tileY = y - tileOrigin.y;
					if( tileY < 0 || tileY >= ts ) continue;
					for( int x = rwI.x1; x < rwI.x2; ++x )
					{
						int tileX = x - tileOrigin.x;
						if( tileX < 0 || tileX >= ts ) continue;
						OfxRGBAColourF *pixel = static_cast<GafferOFX::Image*>( sharedOutput )->pixel( x, y );
						if( pixel )
						{
							tile[tileY * ts + tileX] = (&pixel->r)[comp];
						}
					}
				}
			}
			sharedOutput->releaseReference();
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

	if( !m_tiledRenderSupported )
	{
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
	}
	else
	{
		// Tiled path: hash clip metadata only (format/dataWindow/channelNames),
		// not tile-level channel data.  Per-tile pixel hashing is done in
		// hashChannelData.
		for( const auto &name : m_clipPlugNames )
		{
			if( auto *imgPlug = getChild<GafferImage::ImagePlug>( name ) )
			{
				if( !imgPlug->getInput() ) continue;
				imgPlug->formatPlug()->hash( h );
				imgPlug->dataWindowPlug()->hash( h );
				imgPlug->channelNamesPlug()->hash( h );
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
	// the cache across frames.  getClipPreferences is called once during
	// createPluginInstance (not here — hashing must be side-effect-free).
	if( m_instance )
	{
		h.append( context->getFrame() );
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

	ImagePlug::GlobalScope globalScope( context );

	GafferOFX::ClipInstance* sourceClip = dynamic_cast<GafferOFX::ClipInstance*>( m_instance->getClip( "Source" ) );
	GafferOFX::ClipInstance* outputClip = dynamic_cast<GafferOFX::ClipInstance*>( m_instance->getClip( "Output" ) );

	OfxTime frame = context->getFrame();
	OfxPointD renderScale = { 1.0, 1.0 };

	Box2i dataWindow;
	Format format;
	const bool hasInput = inPlug()->getInput() != nullptr;

	if( hasInput && sourceClip )
	{
		format = inPlug()->formatPlug()->getValue();
		dataWindow = inPlug()->dataWindowPlug()->getValue();
		if( dataWindow.size().x <= 0 || dataWindow.size().y <= 0 )
		{
			dataWindow = Box2i( V2i( 0, 0 ), V2i( (int)format.width(), (int)format.height() ) );
		}
	}
	else
	{
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
		if( dataWindow.size().x <= 0 || dataWindow.size().y <= 0 )
		{
			dataWindow = Box2i( V2i( 0, 0 ), V2i( 1920, 1080 ) );
		}
		format = Format( dataWindow.size().x, dataWindow.size().y );
	}

	// Tiled path: just compute metadata (dataWindow + pixelAspect), no full-frame render.
	if( m_tiledRenderSupported && hasInput )
	{
		Format fmt = hasInput ? inPlug()->formatPlug()->getValue()
		          : Format( dataWindow.size().x, dataWindow.size().y );
		result->members()["dataWindow"] = new Box2iData( dataWindow );
		result->members()["pixelAspect"] = new FloatData( fmt.getPixelAspect() );
		return result;
	}

	OfxRectI renderWindow = {
		dataWindow.min.x, dataWindow.min.y,
		dataWindow.max.x, dataWindow.max.y
	};

	// Check if the effect is identity (e.g. FrameHold pass-through at a different frame)
	{
		OfxTime identityTime = frame;
		std::string identityClip;
		if( m_instance->isIdentityAction( identityTime, kOfxImageFieldNone, renderWindow, renderScale, identityClip ) == kOfxStatOK )
		{
			if( identityClip == "Source" && hasInput && sourceClip )
			{
				// Read source at identityTime and return as output via pull
				Gaffer::Context::EditableScope idEdit( context );
				idEdit.setFrame( identityTime );

				Format idFormat = inPlug()->formatPlug()->getValue();
				Box2i idDw = inPlug()->dataWindowPlug()->getValue();
				int idW = idDw.size().x;
				int idH = idDw.size().y;
				if( idW > 0 && idH > 0 )
				{
					FloatVectorDataPtr rData = new FloatVectorData();
					FloatVectorDataPtr gData = new FloatVectorData();
					FloatVectorDataPtr bData = new FloatVectorData();
					FloatVectorDataPtr aData = new FloatVectorData();
					rData->writable().resize( idW * idH );
					gData->writable().resize( idW * idH );
					bData->writable().resize( idW * idH );
					aData->writable().resize( idW * idH );

					GafferImage::Sampler rSamp( inPlug(), "R", idDw );
					GafferImage::Sampler gSamp( inPlug(), "G", idDw );
					GafferImage::Sampler bSamp( inPlug(), "B", idDw );
					IECore::ConstStringVectorDataPtr chNames = inPlug()->channelNamesPlug()->getValue();
					bool hasA = false;
					for( const auto &c : chNames->readable() ) { if( c == "A" ) { hasA = true; break; } }
					std::unique_ptr<GafferImage::Sampler> aSamp;
					if( hasA ) aSamp = std::make_unique<GafferImage::Sampler>( inPlug(), "A", idDw );

					vector<float> &rVec = rData->writable();
					vector<float> &gVec = gData->writable();
					vector<float> &bVec = bData->writable();
					vector<float> &aVec = aData->writable();

					for( int y = idDw.min.y; y < idDw.max.y; ++y )
					{
						int row = ( y - idDw.min.y ) * idW;
						for( int x = idDw.min.x; x < idDw.max.x; ++x )
						{
							int idx = row + ( x - idDw.min.x );
							rVec[idx] = rSamp.sample( x, y );
							gVec[idx] = gSamp.sample( x, y );
							bVec[idx] = bSamp.sample( x, y );
							aVec[idx] = aSamp ? aSamp->sample( x, y ) : 1.0f;
						}
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

	// ---- Clip property setup (no pre-fetch) ----
	{
		auto setClipProps = []( GafferOFX::ClipInstance *clip ) {
			if( clip )
			{
				clip->getProps().setStringProperty( kOfxImageEffectPropPixelDepth, kOfxBitDepthFloat );
				clip->getProps().setStringProperty( kOfxImageEffectPropComponents, kOfxImageComponentRGBA );
			}
		};
		setClipProps( outputClip );
		setClipProps( sourceClip );
		for( const auto &plugName : m_clipPlugNames )
		{
			std::string ofxClipName = plugName;
			if( !ofxClipName.empty() && islower( ofxClipName[0] ) )
				ofxClipName[0] = toupper( ofxClipName[0] );
			setClipProps( dynamic_cast<GafferOFX::ClipInstance*>( m_instance->getClip( ofxClipName ) ) );
		}
	}

	// Get RoI for each clip and build clipRoIs map
	std::map<OFX::Host::ImageEffect::ClipInstance *, OfxRectD> rois;
	OfxRectD regionOfInterest = {
		(double)dataWindow.min.x, (double)dataWindow.min.y,
		(double)dataWindow.max.x, (double)dataWindow.max.y
	};
	m_instance->getRegionOfInterestAction( frame, renderScale, regionOfInterest, rois );

	std::map<std::string, OfxRectD> clipRoIs;
	for( auto &[clipPtr, roi] : rois )
	{
		if( clipPtr )
			clipRoIs[clipPtr->getName()] = roi;
	}

	// Check if the plugin supports GL rendering.  GL-capable plugins
	// are dispatched to the dedicated OFXRenderWorker for context affinity;
	// CPU-only plugins render inline with zero GL interaction.
	bool pluginSupportsGL = false;
	bool glIsNeeded = false;
	try
	{
		std::string val = m_instance->getPlugin()->getDescriptor().getProps().getStringProperty(
			kOfxImageEffectPropOpenGLRenderSupported
		);
		pluginSupportsGL = ( val == "true" || val == "needed" );
		glIsNeeded = ( val == "needed" );
	}
	catch( const std::exception & ) {}

	int w = renderWindow.x2 - renderWindow.x1;
	int h = renderWindow.y2 - renderWindow.y1;

	RenderingGuard _rg( m_rendering );

	Gaffer::ConstContextPtr ctxCopy = new Gaffer::Context( *Gaffer::Context::current() );
	m_instance->setProjectFormat( (double)dataWindow.size().x, (double)dataWindow.size().y );

	RenderInvocation inv;
	inv.time = frame;
	inv.renderWindow = renderWindow;
	inv.renderScale.x = renderScale.x;
	inv.renderScale.y = renderScale.y;
	inv.context = ctxCopy;
	inv.clipRoIs = clipRoIs;

	// Shared render function used by both CPU (inline) and GL (worker) paths.
	OfxStatus renderStatus = kOfxStatOK;
	auto renderFunc = [&]()
	{
		RenderInvocationGuard guard( inv );
		m_instance->beginRenderAction( frame, frame, 1.0, false, renderScale, true, true );
		renderStatus = m_instance->renderAction( frame, kOfxImageFieldNone, renderWindow, renderScale, true, true, false );
		m_instance->endRenderAction( frame, frame, 1.0, false, renderScale, true, true );
	};

	if( pluginSupportsGL )
	{
		// GL path: dispatch to dedicated worker for context affinity.
		// Prefetch input images on the compute thread first — the worker
		// must never pull Gaffer directly, as that could deadlock when
		// another thread holds m_renderMutex for a different node.
		// Use clip->getImage() with the full RoI so the resolveFetchRegion
		// floor/ceil + RoD math is consistent between prefetch and render.
		{
			RenderInvocationGuard guard( inv );
			for( const auto &[clipName, roI] : clipRoIs )
			{
				if( clipName == "Output" )
					continue;
				GafferOFX::ClipInstance *clip = dynamic_cast<GafferOFX::ClipInstance*>(
					m_instance->getClip( clipName )
				);
				if( !clip || clip->plugName().empty() )
					continue;
				const GafferImage::ImagePlug *plug = m_instance->node()->getChild<GafferImage::ImagePlug>( clip->plugName() );
				if( !plug || !plug->getInput() )
					continue;
				OfxRectD roiD = roI;
				if( auto *img = static_cast<GafferOFX::Image*>( clip->getImage( frame, &roiD ) ) )
					inv.prefetched[clipName] = img;
			}
			if( inv.exception )
				std::rethrow_exception( inv.exception );
		}

		// The worker owns the EGL context; makeCurrent on any other
		// thread would steal it permanently.
		OFXRenderWorker::instance().execute( [&]()
		{
			GLContextManager &gl = GLContextManager::instance();
			bool useGL = gl.makeCurrent();

			// Require-GL plugins get a hard failure if no context.
			if( glIsNeeded && !useGL )
			{
				throw IECore::Exception(
					"OFX plugin requires OpenGL but no GL context is available"
				);
			}

			if( !m_glContextAttached && useGL && gl.pluginDispatchOK() )
			{
				m_glContextAttached = true;
				m_instance->getProps().setIntProperty( kOfxImageEffectPropOpenGLEnabled, 1 );
				m_instance->contextAttachedAction();
			}

			if( useGL )
			{
				bool pluginDispatchOK = gl.pluginDispatchOK();
				if( pluginDispatchOK )
				{
					unsigned int fbo = 0, tex = 0;
					if( w != (int)gl.outputTexWidth() || h != (int)gl.outputTexHeight() )
					{
						glGenTextures( 1, &tex );
						glBindTexture( GL_TEXTURE_2D, tex );
						glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr );
						glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
						glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
						glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
						glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

						GLContextManager::glGenFramebuffersF( 1, &fbo );
						GLContextManager::glBindFramebufferF( GL_FRAMEBUFFER, fbo );
						GLContextManager::glFramebufferTexture2DF( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0 );

						gl.setOutputFBO( fbo, tex, w, h );
					}
					else
					{
						fbo = gl.outputFBO();
						tex = gl.outputTexture();
						GLContextManager::glBindFramebufferF( GL_FRAMEBUFFER, fbo );
					}

					GLenum fbStatus = GLContextManager::glCheckFramebufferStatusF( GL_FRAMEBUFFER );
					if( fbStatus != GL_FRAMEBUFFER_COMPLETE ) {}

					glViewport( 0, 0, w, h );
					glClearColor( 0.25f, 0.5f, 0.75f, 1.0f );
					glClear( GL_COLOR_BUFFER_BIT );
				}
			}

			// Run render action (on the worker thread — GL context is valid here)
			renderFunc();

			// ---- GL readback (if plugin rendered into our FBO) ----
			if( useGL && pluginSupportsGL )
			{
				GLContextManager::glBindFramebufferF( GL_FRAMEBUFFER, gl.outputFBO() );

				// Sentinel: check if the clear color is still intact.
				// If the plugin rendered into our FBO, pixels should differ.
				float probe[4];
				glReadPixels( 0, 0, 1, 1, GL_RGBA, GL_FLOAT, probe );
				auto approxEq = []( float a, float b, float eps ) {
					return ( a - b ) < eps && ( b - a ) < eps;
				};
				if( !approxEq( probe[0], 0.25f, 0.001f ) ||
				    !approxEq( probe[1], 0.50f, 0.001f ) ||
				    !approxEq( probe[2], 0.75f, 0.001f ) )
				{
					// Plugin rendered to our FBO.  Read pixels directly
					// into result members (inv.outputImage may be NULL
					// if the plugin used loadTexture instead of getImage).
					// NOTE: do NOT also read from inv.outputImage here —
					// the sentinel intact means no GL render, and the CPU
					// de-interleave below handles the CPU-output path.
					glFinish();
					glPixelStorei( GL_PACK_ALIGNMENT, 1 );

					int numPixels = w * h;
					vector<float> pixelBuffer( numPixels * 4 );
					glReadPixels( 0, 0, w, h, GL_RGBA, GL_FLOAT, pixelBuffer.data() );

					FloatVectorDataPtr rData = new FloatVectorData();
					FloatVectorDataPtr gData = new FloatVectorData();
					FloatVectorDataPtr bData = new FloatVectorData();
					FloatVectorDataPtr aData = new FloatVectorData();
					rData->writable().resize( numPixels );
					gData->writable().resize( numPixels );
					bData->writable().resize( numPixels );
					aData->writable().resize( numPixels );

					for( int i = 0; i < numPixels; ++i )
					{
						rData->writable()[i] = pixelBuffer[i * 4];
						gData->writable()[i] = pixelBuffer[i * 4 + 1];
						bData->writable()[i] = pixelBuffer[i * 4 + 2];
						aData->writable()[i] = pixelBuffer[i * 4 + 3];
					}

					result->members()["R"] = rData;
					result->members()["G"] = gData;
					result->members()["B"] = bData;
					result->members()["A"] = aData;
				}
				// Sentinel intact && inv.outputImage means the plugin
				// wrote to getImage(Output) on the CPU — the de-interleave
				// below handles this case.  Do NOT glReadPixels into it.
			}
		} );

	}
	else
	{
		// CPU path: no GL interaction, render inline on the compute thread.
		// Serialized by m_renderMutex; re-entrant via TLS invocation stack.
		renderFunc();
	}

	// ---- Propagate cancellation / check render status ----
	// Order is critical: cancellation from a failed Gaffer pull must surface
	// as IECore::Cancelled (silent retry), not as a generic render failure
	// (red node + error dialog on every viewer pan).
	if( inv.exception )
		std::rethrow_exception( inv.exception );

	if( renderStatus != kOfxStatOK && renderStatus != kOfxStatReplyDefault )
		throw IECore::Exception( "OFX renderAction failed" );

	// ---- De-interleave output into result (compute thread, worker is done) ----
	// Skip if GL readback already populated result (sentinel-differed path).
	if( !result->member<FloatVectorData>( "R" ) )
	{
		auto *outputImg = inv.outputImage;
		if( outputImg )
		{
			OfxRectI ob = outputImg->getBounds();
			int ow = ob.x2 - ob.x1;
			int oh = ob.y2 - ob.y1;
			if( ow > 0 && oh > 0 )
			{
				FloatVectorDataPtr rData = new FloatVectorData();
				FloatVectorDataPtr gData = new FloatVectorData();
				FloatVectorDataPtr bData = new FloatVectorData();
				FloatVectorDataPtr aData = new FloatVectorData();
				rData->writable().resize( ow * oh );
				gData->writable().resize( ow * oh );
				bData->writable().resize( ow * oh );
				aData->writable().resize( ow * oh );

				vector<float> &rVec = rData->writable();
				vector<float> &gVec = gData->writable();
				vector<float> &bVec = bData->writable();
				vector<float> &aVec = aData->writable();

				for( int y = ob.y1; y < ob.y2; ++y )
				{
					int row = ( y - ob.y1 ) * ow;
					for( int x = ob.x1; x < ob.x2; ++x )
					{
						if( auto *pixel = static_cast<GafferOFX::Image*>( outputImg )->pixel( x, y ) )
						{
							int idx = row + ( x - ob.x1 );
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
	}

	result->members()["dataWindow"] = new Box2iData( dataWindow );
	result->members()["pixelAspect"] = new FloatData( format.getPixelAspect() );
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
