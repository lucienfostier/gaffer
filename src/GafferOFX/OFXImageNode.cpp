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
#include "GafferOFX/ParamAlgo.h"
#include "GafferOFX/ParamInstance.h"

#include "Gaffer/Context.h"
#include "Gaffer/Metadata.h"
#include "Gaffer/ArrayPlug.h"
#include "Gaffer/ScriptNode.h"
#include "Gaffer/UndoScope.h"
#include "Gaffer/Process.h"
// CompoundObjectPlug provided via OFXImageNode.h which includes Gaffer/TypedObjectPlug.h

#include "GafferImage/ImageAlgo.h"
#include "GafferImage/Sampler.h"

#include "IECore/BoxOps.h"
#include "IECore/NullObject.h"

#ifdef OFX_SUPPORTS_OPENGLRENDER
#include "ofxGPURender.h"
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <condition_variable>
#include <vector>

namespace
{

// Pixel counts flow into int-typed tile math downstream. A corrupt RoD
// must throw here rather than wrap into a small buffer on 32-bit int
// multiplication.
size_t checkedPixelCount( int w, int h )
{
	if( w <= 0 || h <= 0 )
	{
		throw IECore::Exception( "OFX invalid render window" );
	}
	const size_t n = (size_t)w * (size_t)h;
	if( n > (size_t)INT_MAX )
	{
		throw IECore::Exception( "OFX render window too large" );
	}
	return n;
}

// Copy an OFX output image region into planar channel vectors. The planar
// index is ( y - indexOrigin.y ) * stride + ( x - indexOrigin.x ); positions
// outside [ 0, count ) are skipped. Shared by the tiled path (region within
// a tile) and the full-frame path (region covering the output bounds).
inline void deinterleaveImageToPlanar(
	GafferOFX::Image *outputImg,
	const Imath::Box2i &region,
	const Imath::V2i &indexOrigin,
	int stride,
	size_t count,
	std::vector<float> &rVec,
	std::vector<float> &gVec,
	std::vector<float> &bVec,
	std::vector<float> &aVec
)
{
	for( int y = region.min.y; y < region.max.y; ++y )
	{
		for( int x = region.min.x; x < region.max.x; ++x )
		{
			if( auto *pixel = outputImg->pixel( x, y ) )
			{
				const int idx = ( y - indexOrigin.y ) * stride + ( x - indexOrigin.x );
				if( idx >= 0 && (size_t)idx < count )
				{
					rVec[idx] = pixel->r;
					gVec[idx] = pixel->g;
					bVec[idx] = pixel->b;
					aVec[idx] = pixel->a;
				}
			}
		}
	}
}

} // namespace

//////////////////////////////////////////////////////////////////////////
// Single-threaded executor for OFX render work.
//
// All renders run on one persistent background thread rather than on the
// calling thread, for three reasons.  First, GL contexts are bound to the
// thread that creates them: rendering on arbitrary compute threads would
// create one context (and one shader compilation) per thread instead of
// reusing a single context.  Second, it moves render cost (e.g. shader
// compilation) off the calling thread.  Third, it serialises concurrent
// render submissions instead of running them all at once.
//
// Callers submit a task and block on their own future; the worker runs
// queued tasks without holding the mutex, and each caller rethrows only
// its own exception.
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
		// One future per caller: concurrent submitters queue instead of
		// overwriting each other's task, and each rethrows only its own
		// exception. The worker runs tasks without holding the mutex.
		std::packaged_task<void()> task( std::forward<F>( func ) );
		std::future<void> done = task.get_future();
		{
			std::lock_guard<std::mutex> lock( m_mutex );
			m_queue.push_back( std::move( task ) );
		}
		m_cv.notify_one();
		done.get();
	}

	~OFXRenderWorker()
	{
		{
			std::lock_guard<std::mutex> lock( m_mutex );
			m_done = true;
		}
		m_cv.notify_all();
		if( m_thread.joinable() )
		{
			m_thread.join();
		}
	}

private:

	OFXRenderWorker()
	{
		m_thread = std::thread( [this]() { workerLoop(); } );
	}

	OFXRenderWorker( const OFXRenderWorker & ) = delete;
	OFXRenderWorker &operator=( const OFXRenderWorker & ) = delete;

	void workerLoop()
	{
		while( true )
		{
			std::packaged_task<void()> task;
			{
				std::unique_lock<std::mutex> lock( m_mutex );
				m_cv.wait( lock, [this]() { return m_done || !m_queue.empty(); } );
				if( m_queue.empty() )
				{
					// m_done with nothing left to run.
					break;
				}
				task = std::move( m_queue.front() );
				m_queue.pop_front();
			}
			// packaged_task captures any exception into the caller's future.
			task();
		}
	}

	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::deque<std::packaged_task<void()>> m_queue;
	bool m_done = false;
};

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace GafferImage;
using namespace Gaffer;
using namespace GafferOFX;

// RAII guard that increments m_rendering and decrements on scope exit.
// Concurrent tiles each hold their own increment, so m_rendering > 0
// while any tile is rendering. destroyInstance waits on the drain.
struct RenderingCounter
{
	const OFXImageNode *node;
	RenderingCounter( const OFXImageNode *n ) : node( n ) { node->renderStarted(); }
	~RenderingCounter() { node->renderFinished(); }
};

void OFXImageNode::renderStarted() const
{
	++m_rendering;
}

void OFXImageNode::renderFinished() const
{
	if( --m_rendering == 0 )
	{
		std::lock_guard<std::mutex> lock( m_renderDrainedMutex );
		m_renderDrained.notify_all();
	}
}

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
	IntPlug *GLRenderMode = new IntPlug( "GLRenderMode", Plug::In, 0, 0, 2 );
	addChild( GLRenderMode );
	addChild( new CompoundObjectPlug( "__ofxRenderBuffer", Plug::Out, new CompoundObject, Plug::Default & ~Plug::Serialisable ) );
	addChild( new ObjectPlug( "__ofxTileBuffer", Plug::Out, IECore::NullObject::defaultNullObject(), Plug::Default & ~Plug::Serialisable ) );
	plugSetSignal().connect( [this]( Gaffer::Plug *plug ) { plugSet( plug ); } );
	// Invalidate instance on structural changes under parametersPlug (invariant 3).
	parametersPlug()->childAddedSignal().connect( [this]( Gaffer::GraphComponent *p, Gaffer::GraphComponent *c ){ parametersPlugChildAdded( p, c ); } );
	parametersPlug()->childRemovedSignal().connect( [this]( Gaffer::GraphComponent *p, Gaffer::GraphComponent *c ){ parametersPlugChildRemoved( p, c ); } );
}

void OFXImageNode::destroyInstance()
{
	std::shared_ptr<GafferOFX::EffectImageInstance> doomed;
	{
		std::lock_guard<std::recursive_mutex> lock( m_instanceMutex );
		if( !m_instance )
		{
			return;
		}
		assert( Gaffer::Process::current() == nullptr && "destroyInstance must not be called inside compute" );
		doomed = std::move( m_instance );
		++m_instanceGeneration;
	}
	// Drain in-flight renders first: snapshots retain the wrapper, while
	// shutdown() releases plugin-side state those snapshots may reference.
	// The re-pull that follows this dirty terminates the renders being
	// waited on. This runs outside compute threads, so the wait does not
	// re-enter the render path.
	{
		std::unique_lock<std::mutex> lock( m_renderDrainedMutex );
		m_renderDrained.wait( lock, [this]() { return m_rendering.load() == 0; } );
	}
	// Teardown runs outside the lock; only the move above requires it.
	destroyInteract();
	if( m_glContextAttached )
	{
		// Dispatch detach through the worker, where the GL context is current.
		OFXRenderWorker::instance().execute( [doomed]() {
			GLContextManager::instance().makeCurrent();
			doomed->contextDetachedAction();
		} );
		m_glContextAttached = false;
	}
	doomed->shutdown();
}

void OFXImageNode::parametersPlugChildAdded( Gaffer::GraphComponent *parent, Gaffer::GraphComponent *child )
{
	// Structural change under parametersPlug invalidates the instance (invariant 3).
	// Skip when there is no instance yet, or while createPluginInstance is
	// populating plugs (parameter constructors bind to plugs being added).
	if( m_creatingInstance.load() || !lockedInstance() )
	{
		return;
	}
	destroyInstance();
}

void OFXImageNode::parametersPlugChildRemoved( Gaffer::GraphComponent *parent, Gaffer::GraphComponent *child )
{
	if( m_creatingInstance.load() || !lockedInstance() )
	{
		return;
	}
	destroyInstance();
}

bool OFXImageNode::loadPlugin( const std::string &pluginId, bool keepExistingValues )
{
	if( pluginId.empty() )
	{
		Gaffer::UndoScope undo( ancestor<Gaffer::ScriptNode>() );
		pluginIdPlug()->setValue( "" );
		// The instance was already destroyed by plugSet.
		parametersPlug()->clearChildren();
		removeClipPlugs();
		return true;
	}

	Host &host = Host::instance();
	auto plugin = host.m_pluginCache.getPluginById( pluginId );
	if( !plugin )
	{
		return false;
	}

	// Find context descriptor (same priority as createPluginInstance)
	const std::set<std::string> &contexts = plugin->getContexts();
	std::vector<std::string> contextPriority;
	if( contexts.find( kOfxImageEffectContextFilter ) != contexts.end() ) contextPriority.push_back( kOfxImageEffectContextFilter );
	if( contexts.find( kOfxImageEffectContextGeneral ) != contexts.end() ) contextPriority.push_back( kOfxImageEffectContextGeneral );
	if( contexts.find( kOfxImageEffectContextGenerator ) != contexts.end() ) contextPriority.push_back( kOfxImageEffectContextGenerator );
	for( const auto &c : contexts )
	{
		if( c != kOfxImageEffectContextFilter && c != kOfxImageEffectContextGeneral && c != kOfxImageEffectContextGenerator )
		{
			contextPriority.push_back( c );
		}
	}
	OFX::Host::ImageEffect::Descriptor *descriptor = nullptr;
	for( const auto &ctx : contextPriority )
	{
		descriptor = plugin->getContext( ctx );
		if( descriptor ) break;
	}
	if( !descriptor )
	{
		return false;
	}

	Gaffer::UndoScope undo( ancestor<Gaffer::ScriptNode>() );

	if( pluginIdPlug()->getValue() != pluginId )
	{
		pluginIdPlug()->setValue( pluginId );
		// plugSet destroyed the previous instance.
	}

	{
		struct CreatingGuard
		{
			std::atomic<bool> &flag;
			CreatingGuard( std::atomic<bool> &f ) : flag( f ) { flag.store( true ); }
			~CreatingGuard() { flag.store( false ); }
		} guard( m_creatingInstance );

		// Descriptor-driven setup first (descriptor defaults), then
		// bind-only instance creation. setupPlugs creates/reconciles
		// plugs; createPluginInstance binds to them.
		ParamAlgo::setupPlugs( *descriptor, parametersPlug(), keepExistingValues );

		if( !createPluginInstance() )
		{
			return false;
		}
	}

	return true;
}

bool OFXImageNode::ensureInstance() const
{
	{
		std::lock_guard<std::recursive_mutex> lock( m_instanceMutex );
		if( m_instance )
		{
			return true;
		}
	}
	std::string pluginId = pluginIdPlug()->getValue();
	if( pluginId.empty() )
	{
		return false;
	}
	// Lazy creation binds to existing plugs. Setup preserves compatible
	// plugs, so binding resolves them without graph changes. The atomic
	// guard covers plug creation during populate. Lock order is creation
	// mutex before instance mutex.
	if( !const_cast<OFXImageNode*>( this )->createPluginInstance() )
	{
		return false;
	}
	std::lock_guard<std::recursive_mutex> lock( m_instanceMutex );
	return (bool)m_instance;
}

std::shared_ptr<GafferOFX::EffectImageInstance> OFXImageNode::snapshotInstance() const
{
	// Release the instance mutex before ensureInstance: creation waits on
	// the creation mutex, which a concurrent creator may hold while
	// publishing under this mutex. Holding both in opposite order would
	// deadlock.
	{
		std::lock_guard<std::recursive_mutex> lock( m_instanceMutex );
		if( m_instance )
		{
			return m_instance;
		}
	}
	ensureInstance();
	{
		std::lock_guard<std::recursive_mutex> lock( m_instanceMutex );
		return m_instance;
	}
}

std::shared_ptr<GafferOFX::EffectImageInstance> OFXImageNode::lockedInstance() const
{
	// Locked read without creation, for main-thread paths racing
	// compute-thread publication. Reading the shared_ptr unlocked
	// while another thread assigns it is undefined behaviour.
	std::lock_guard<std::recursive_mutex> lock( m_instanceMutex );
	return m_instance;
}

void OFXImageNode::plugSet( Gaffer::Plug *plug )
{
	if( plug == pluginIdPlug() )
	{
		// Eager recreation on the main thread: plugin initialisation
		// (including creation-time paramSetValue calls) and plug setup
		// run outside computes. Creation inside a compute binds to
		// existing plugs only; initialisation writes are deferred
		// through the action gate.
		destroyInstance();
		createPluginInstance();
	}
	else if( parametersPlug()->isAncestorOf( plug ) )
	{
		const std::shared_ptr<GafferOFX::EffectImageInstance> instance = lockedInstance();
		if( !instance )
		{
			return;
		}
	if( m_rendering > 0 )
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
		instance->beginInstanceChangedAction( kOfxChangeUserEdited );
		instance->paramInstanceChangedAction( plug->getName().string(), kOfxChangeUserEdited, time, renderScale );
		instance->endInstanceChangedAction( kOfxChangeUserEdited );
		// Clip preferences can depend on parameter values; re-evaluate.
		instance->getClipPreferences();
	}
}

OFXImageNode::~OFXImageNode()
{
	if( lockedInstance() )
	{
		destroyInstance();
	}
}

bool OFXImageNode::createPluginInstance()
{
	// Serialise concurrent builds. The mutex is recursive because
	// setupPlugs signals re-enter plugSet (destroy/create) on the same
	// thread. It is released before any call that acquires the instance
	// mutex (see ensureInstance).
	std::lock_guard<std::recursive_mutex> createLock( m_createMutex );
	{
		std::lock_guard<std::recursive_mutex> lock( m_instanceMutex );
		if( m_instance )
		{
			return true;
		}
	}

	struct CreatingGuard
	{
		std::atomic<bool> &flag;
		CreatingGuard( std::atomic<bool> &f ) : flag( f ) { flag.store( true ); }
		~CreatingGuard() { flag.store( false ); }
	} creatingGuard( m_creatingInstance );

	// Plug setup and clip plug creation run outside computes. Inside a
	// compute (lazy ensureInstance path) this binds to existing plugs
	// only; missing plugs report a "reload the plugin" warning below
	// rather than blocking the task.
	const bool inCompute = Gaffer::Process::current() != nullptr;

	Host& host = Host::instance();
	std::string pluginId = pluginIdPlug()->getValue();
	auto plugin = host.m_pluginCache.getPluginById(pluginId);
	if( plugin )
	{
		if( !inCompute )
		{
			// Remove clip plugs from any previous instance
			removeClipPlugs();
		}

		// Use the first available context supported by the plugin
		const std::set<std::string> &contexts = plugin->getContexts();
		std::vector<std::string> contextPriority;
		if( contexts.find( kOfxImageEffectContextFilter ) != contexts.end() )
		{
			contextPriority.push_back( kOfxImageEffectContextFilter );
		}
		if( contexts.find( kOfxImageEffectContextGeneral ) != contexts.end() )
		{
			contextPriority.push_back( kOfxImageEffectContextGeneral );
		}
		if( contexts.find( kOfxImageEffectContextGenerator ) != contexts.end() )
		{
			contextPriority.push_back( kOfxImageEffectContextGenerator );
		}
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
		{
			return false;
		}

		// Descriptor-driven setup before instance creation (outside
		// computes; see inCompute above). This provides plugs with
		// descriptor defaults so parameter constructors bind rather
		// than create. Existing compatible values are preserved.
		if( !inCompute )
		{
			for( const auto &context : contextPriority )
			{
				if( auto *descriptor = plugin->getContext( context ) )
				{
					try
					{
						ParamAlgo::setupPlugs( *descriptor, parametersPlug(), true );
					}
					catch( const std::exception &e )
					{
						IECore::msg( IECore::Msg::Warning, "GafferOFX::OFXImageNode",
							std::string( "Param setup failed: " ) + e.what() );
					}
					break;
				}
			}
		}

		OFX::Host::ImageEffect::Instance *instance = nullptr;
		for( const auto &context : contextPriority )
		{
			try
			{
				instance = plugin->createInstance( context, this );
			}
			catch( const StalePlugsException &e )
			{
				IECore::msg( IECore::Msg::Warning, "GafferOFX::OFXImageNode",
					std::string( "Parameters out of date - reload the plugin: " ) + e.what() );
				return false;
			}
			catch( const std::exception &e )
			{
				IECore::msg( IECore::Msg::Warning, "GafferOFX::OFXImageNode",
					std::string( "Failed to create instance: " ) + e.what() );
				return false;
			}
			if( instance )
			{
				break;
			}
		}

		if( !instance )
		{
			return false;
		}

		// Build locally and publish once initialised, so snapshots of
		// m_instance observe either the previous instance or the new
		// one, and rejected plugins are not published.
		std::shared_ptr<EffectImageInstance> built( static_cast<EffectImageInstance*>( instance ) );

		built->createInstanceAction();

		// GafferOFX renders in 32-bit float without byte conversion, so
		// verify the plugin advertises float pixel depth. Query the
		// plugin descriptor property set rather than the instance
		// properties, where string-property lookup chaining is unreliable.
		{
			bool supportsFloat = false;
			const auto &dp = built->getPlugin()->getDescriptor().getProps();
			try
			{
				int n = dp.getDimension( kOfxImageEffectPropSupportedPixelDepths );
				for( int i = 0; i < n && !supportsFloat; ++i )
				{
					supportsFloat = dp.getStringProperty( kOfxImageEffectPropSupportedPixelDepths, i ) == kOfxBitDepthFloat;
				}
			}
			catch( const std::exception & ) {}
			if( !supportsFloat )
			{
				IECore::msg( IECore::Msg::Warning, "GafferOFX::OFXImageNode",
					"GafferOFX: rejecting plugin \"" + built->getPlugin()->getIdentifier() +
					"\" - does not advertise kOfxBitDepthFloat in kOfxImageEffectPropSupportedPixelDepths" );
				// Shut down before dropping: createInstanceAction has already
				// run, so the destructor would otherwise report the
				// instance as leaked.
				built->shutdown();
				return false;
			}
		}

		// Initialise frame-dependent state here; hashing remains
		// side-effect-free.
		built->getClipPreferences();

		// GL plugins render full-frame (FBO/readback path). CPU plugins
		// with SupportsTiles report per-tile rendering. CImg-based
		// plugins report supportsTiles but require srcRoD.x1 == dstRoD.x1,
		// which tile-sized render windows do not satisfy.
		m_tiledRenderSupported = false;
		{
			bool pluginSupportsGL = false;
			try
			{
				std::string val = built->getPlugin()->getDescriptor().getProps().getStringProperty(
					kOfxImageEffectPropOpenGLRenderSupported
				);
				pluginSupportsGL = ( val == "true" || val == "needed" );
			}
			catch( const std::exception & ) {}
			std::string pluginId = built->getPlugin()->getIdentifier();
			bool isCImg = pluginId.find( "net.sf.cimg." ) == 0 || pluginId.find( "eu.cimg." ) == 0;
			bool supportsTiles = false;
			try { supportsTiles = built->supportsTiles(); } catch( const std::exception & ) {}
			if( !pluginSupportsGL && supportsTiles && !isCImg )
			{
				m_tiledRenderSupported = true;
			}
			IECore::msg( IECore::Msg::Debug, "GafferOFX::OFXImageNode",
				"OFXImageNode: tiled=" + std::to_string( m_tiledRenderSupported ) +
				" for \"" + pluginId + "\" gl=" + std::to_string( pluginSupportsGL ) +
				" tiles=" + std::to_string( supportsTiles ) + " cimg=" + std::to_string( isCImg ) );
		}

		// Determine render thread safety level for tiled rendering.
		m_renderThreadSafety = RenderSafety::InstanceSafe;  // safe default
		if( m_tiledRenderSupported )
		{
			std::string val;
			try
			{
				val = built->getPlugin()->getDescriptor().getProps().getStringProperty(
					kOfxImageEffectPluginRenderThreadSafety
				);
			}
			catch( const std::exception & )
			{
				val = "(no property)";
			}

			if( val == kOfxImageEffectRenderFullySafe || val == "fully" )
			{
				m_renderThreadSafety = RenderSafety::FullySafe;
			}
			else if( val == kOfxImageEffectRenderUnsafe || val == "unsafe" )
			{
				m_renderThreadSafety = RenderSafety::Unsafe;
			}
			else
			{
				m_renderThreadSafety = RenderSafety::InstanceSafe;
			}

			IECore::msg( IECore::Msg::Debug, "GafferOFX::OFXImageNode",
				std::string( "OFXImageNode: tile render thread safety for \"" ) +
				built->getPlugin()->getIdentifier() + "\" = " + val + " -> " +
				( m_renderThreadSafety == RenderSafety::FullySafe ? "FullySafe" :
				m_renderThreadSafety == RenderSafety::Unsafe ? "Unsafe" : "InstanceSafe" ) );
		}

		// getConnected() resolves the plug input at call time.

		// Create a Gaffer plug for each non-Output, non-Source clip.
		// This runs outside computes; addChild is not permitted during
		// compute.
		if( !inCompute )
		{
			createClipPlugs( built.get() );
		}

		{
			std::lock_guard<std::recursive_mutex> lock( m_instanceMutex );
			m_instance = built;
		}
		return true;
	}
	return false;
}

std::vector<std::string> OFXImageNode::clipPlugNames() const
{
	std::lock_guard<std::mutex> lock( m_clipPlugNamesMutex );
	return m_clipPlugNames;
}

void OFXImageNode::removeClipPlugs()
{
	// Copy the names under lock, then remove without holding it:
	// removeChild() propagates dirtiness synchronously and re-enters
	// affects(), which would deadlock on the non-recursive mutex if
	// still held.
	for( const auto &name : clipPlugNames() )
	{
		if( auto *plug = getChild<GafferImage::ImagePlug>( name ) )
		{
			removeChild( plug );
		}
	}
	{
		std::lock_guard<std::mutex> lock( m_clipPlugNamesMutex );
		m_clipPlugNames.clear();
	}
}

void OFXImageNode::createClipPlugs( GafferOFX::EffectImageInstance *instance )
{
	if( !instance )
	{
		return;
	}

	for( int i = 0; i < instance->getNClips(); ++i )
	{
		auto *clip = dynamic_cast<GafferOFX::ClipInstance*>( instance->getNthClip( i ) );
		if( !clip )
		{
			continue;
		}

		const std::string &clipName = clip->getName();
		if( clipName == "Output" || clipName == "Source" )
		{
			continue;
		}

		// Derive the Gaffer plug name from the clip name (lowercase first
		// letter, then ParamAlgo sanitisation).
		std::string plugName = clipName;
		if( !plugName.empty() && isupper( plugName[0] ) )
		{
			plugName[0] = tolower( plugName[0] );
		}
		plugName = ParamAlgo::plugName( plugName );

		// Reuse an existing plug where present (for example after scene
		// load or re-creation). A child of a different type under the
		// same name is replaced before addChild, which does not permit
		// duplicate names.
		auto *plug = getChild<GafferImage::ImagePlug>( plugName );
		if( !plug )
		{
			if( auto *stale = getChild<Gaffer::Plug>( plugName ) )
			{
				removeChild( stale );
			}
			plug = new GafferImage::ImagePlug( plugName, Plug::In );
			addChild( plug );
		}
		{
			std::lock_guard<std::mutex> lock( m_clipPlugNamesMutex );
			m_clipPlugNames.push_back( plugName );
		}

		// Store the plug name on the clip for getConnected() lookup.
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

Gaffer::IntPlug *OFXImageNode::GLRenderModePlug()
{
	return getChild<IntPlug>( g_firstPlugIndex + 2 );
}

const Gaffer::IntPlug *OFXImageNode::GLRenderModePlug() const
{
	return getChild<IntPlug>( g_firstPlugIndex + 2 );
}

CompoundObjectPlug *OFXImageNode::ofxRenderBufferPlug()
{
	return getChild<CompoundObjectPlug>( g_firstPlugIndex + 3 );
}

const CompoundObjectPlug *OFXImageNode::ofxRenderBufferPlug() const
{
	return getChild<CompoundObjectPlug>( g_firstPlugIndex + 3 );
}

ObjectPlug *OFXImageNode::tileBufferPlug()
{
	return getChild<ObjectPlug>( g_firstPlugIndex + 4 );
}

const ObjectPlug *OFXImageNode::tileBufferPlug() const
{
	return getChild<ObjectPlug>( g_firstPlugIndex + 4 );
}

void OFXImageNode::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	ImageProcessor::affects( input, outputs );

	// Guard against uninitialized inPlug when minInputs=0
	if( !inPlug() )
	{
		return;
	}

	// Wiring is the union of the tiled (__ofxTileBuffer) and full-frame
	// (__ofxRenderBuffer) paths. It must not depend on mutable render
	// state such as m_tiledRenderSupported: affects() has to be a pure
	// function of the graph. Over-dirtying only costs recomputation,
	// while under-dirtying leaves stale cache entries after a plugin
	// switch.
	if( input == GLRenderModePlug() )
	{
		outputs.push_back( ofxRenderBufferPlug() );
		outputs.push_back( tileBufferPlug() );
	}

	// Input image metadata affects the render buffer (output
	// format/dataWindow/channelNames).
	if( input == inPlug()->formatPlug() || input == inPlug()->dataWindowPlug() || input == inPlug()->channelNamesPlug() )
	{
		outputs.push_back( ofxRenderBufferPlug() );
	}

	// Input channel data drives both buffers (full-frame render reads
	// every tile; tiled path reads the intersecting ones).
	if( input == inPlug()->channelDataPlug() )
	{
		outputs.push_back( ofxRenderBufferPlug() );
		outputs.push_back( tileBufferPlug() );
	}

	if( input == inPlug()->dataWindowPlug() )
	{
		outputs.push_back( tileBufferPlug() );
	}

	// Dynamic clip plugs (Mask, UV, etc.).
	{
		std::lock_guard<std::mutex> lock( m_clipPlugNamesMutex );
		for( const auto &name : m_clipPlugNames )
		{
			if( auto *imgPlug = getChild<GafferImage::ImagePlug>( name ) )
			{
				if( input == imgPlug->formatPlug() || input == imgPlug->dataWindowPlug() ||
				input == imgPlug->channelNamesPlug() )
				{
					outputs.push_back( ofxRenderBufferPlug() );
				}
				if( input == imgPlug->channelDataPlug() || input == imgPlug->dataWindowPlug() )
				{
					outputs.push_back( ofxRenderBufferPlug() );
					outputs.push_back( tileBufferPlug() );
				}
			}
		}
	}

	// Parameters and plugin ID drive both buffers.
	if( input == pluginIdPlug() || parametersPlug()->isAncestorOf( input ) )
	{
		outputs.push_back( ofxRenderBufferPlug() );
		outputs.push_back( tileBufferPlug() );
	}

	// Render buffer affects all output image properties.
	if( input == ofxRenderBufferPlug() )
	{
		outputs.push_back( outPlug()->formatPlug() );
		outputs.push_back( outPlug()->dataWindowPlug() );
		outputs.push_back( outPlug()->channelNamesPlug() );
		outputs.push_back( outPlug()->channelDataPlug() );
	}

	// Tile buffer drives per-tile channel data.
	if( input == tileBufferPlug() )
	{
		outputs.push_back( outPlug()->channelDataPlug() );
	}
}

void OFXImageNode::hash( const Gaffer::ValuePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	if( output == ofxRenderBufferPlug() )
	{
		hashOfxRenderBuffer( context, h );
	}
	else if( output == tileBufferPlug() )
	{
		hashTileBuffer( context, h );
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
	else if( output == tileBufferPlug() )
	{
		IECore::ConstObjectPtr tileBuf = computeTileBuffer( context );
		if( !tileBuf )
		{
			tileBuf = IECore::NullObject::defaultNullObject();
		}
		static_cast<Gaffer::ObjectPlug *>( output )->setValue( tileBuf );
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

	if( m_tiledRenderSupported && inPlug()->getInput() )
	{
		// Tiled path: output channels are RGBA.
		vector<string> names = { "R", "G", "B", "A" };
		return new StringVectorData( names );
	}

	IECore::ConstCompoundObjectPtr renderBuffer = ofxRenderBufferPlug()->getValue();
	if( !renderBuffer )
	{
		return inPlug()->channelNamesPlug()->getValue();
	}
	vector<string> names;
	if( renderBuffer->member<Data>( "R" ) )
	{
		names.push_back( "R" );
	}
	if( renderBuffer->member<Data>( "G" ) )
	{
		names.push_back( "G" );
	}
	if( renderBuffer->member<Data>( "B" ) )
	{
		names.push_back( "B" );
	}
	if( renderBuffer->member<Data>( "A" ) )
	{
		names.push_back( "A" );
	}
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
		const std::shared_ptr<GafferOFX::EffectImageInstance> instance = snapshotInstance();
		if( !instance )
		{
			const std::string channelName = context->get<std::string>( ImagePlug::channelNameContextName );
			const Imath::V2i tileOrigin = context->get<V2i>( ImagePlug::tileOriginContextName );
			h.append( inPlug()->channelDataHash( channelName, tileOrigin ) );
			return;
		}
		else if( m_tiledRenderSupported && inPlug()->getInput() )
		{
			// Strip channelName before hashing tileBufferPlug so the
			// tile buffer hash (and all its upstream deps) is computed
			// identically for all four channels at this tile origin.
			// The channelName is appended afterwards for cache-key
			// uniqueness per channel.
			Context::EditableScope tileScope( context );
			tileScope.remove( ImagePlug::channelNameContextName );
			tileBufferPlug()->hash( h );
			h.append( context->get<std::string>( ImagePlug::channelNameContextName ) );
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
	// Read the full cached render buffer in global scope.
	ImagePlug::GlobalScope globalScope( context );

	const std::shared_ptr<GafferOFX::EffectImageInstance> instance = snapshotInstance();
	if( !instance )
	{
		return inPlug()->channelData( channelName, tileOrigin );
	}

	if( m_tiledRenderSupported && inPlug()->getInput() )
	{
		// Tiled path: pull __ofxTileBuffer (keyed by tileOrigin, without
		// channelName) so all four channels compute once per tile.
		// Remove channelName from the context before pulling; retaining
		// it would produce one cache entry per channel.
		Context::EditableScope tileScope( context );
		tileScope.remove( ImagePlug::channelNameContextName );

		IECore::ConstCompoundObjectPtr tileBuf = IECore::runTimeCast<const IECore::CompoundObject>( tileBufferPlug()->getValue() );

		if( !tileBuf )
		{
			return ImagePlug::emptyTile();
		}

		auto *chData = tileBuf->member<FloatVectorData>( channelName );
		if( !chData )
		{
			return ImagePlug::emptyTile();
		}

		// Return shared data. The tile buffer is immutable once computed,
		// following the ImagePlug::emptyTile() convention.
		return IECore::ConstFloatVectorDataPtr( chData );
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

	// Extract the tile region
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


void OFXImageNode::hashTileBuffer( const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImagePlug::GlobalScope globalScope( context );

	// Increment before snapshotting: if destroy runs between the snapshot
	// and the increment, it would observe zero in-flight renders and shut
	// down while the render below is still using the instance.
	RenderingCounter _rc( this );
	const std::shared_ptr<GafferOFX::EffectImageInstance> instance = snapshotInstance();
	if( !instance )
	{
		pluginIdPlug()->hash( h );
		inPlug()->formatPlug()->hash( h );
		return;
	}

	const Imath::V2i tileOrigin = context->get<V2i>( ImagePlug::tileOriginContextName );
	const OfxTime frame = context->getFrame();
	const OfxPointD renderScale = { 1.0, 1.0 };

	// Hash identity: tile origin, frame, plugin ID, params.
	h.append( tileOrigin );
	h.append( frame );
	pluginIdPlug()->hash( h );
	GLRenderModePlug()->hash( h );
	for( const auto &child : parametersPlug()->children() )
	{
		if( auto *valuePlug = runTimeCast<const ValuePlug>( child.get() ) )
		{
			valuePlug->hash( h );
		}
	}

	// Data window affects the render box: a window shift inside a tile
	// produces a different pixel boundary even if input pixels are unchanged.
	inPlug()->dataWindowPlug()->hash( h );

	// Build render box for this tile to determine which input tiles to hash.
	int ts = ImagePlug::tileSize();
	Box2i dataWindow = inPlug()->dataWindowPlug()->getValue();
	Box2i tileBound( tileOrigin, tileOrigin + V2i( ts, ts ) );
	Box2i renderBox(
		V2i( std::max( tileBound.min.x, dataWindow.min.x ), std::max( tileBound.min.y, dataWindow.min.y ) ),
		V2i( std::min( tileBound.max.x, dataWindow.max.x ), std::min( tileBound.max.y, dataWindow.max.y ) )
	);
	if( renderBox.size().x <= 0 || renderBox.size().y <= 0 )
	{
		return;
	}

	OfxRectD renderWindowD = { (double)renderBox.min.x, (double)renderBox.min.y,
				(double)renderBox.max.x, (double)renderBox.max.y };

	// Get region of interest for this tile to narrow input hashing.
	std::map<OFX::Host::ImageEffect::ClipInstance *, OfxRectD> rois;
	instance->getRegionOfInterestAction( frame, renderScale, renderWindowD, rois );

	// Hash input channel data for tiles intersecting each clip's RoI.
	// The inPlug (Source) is the primary input.
	auto hashInputRegion = [&]( const GafferImage::ImagePlug *plug, const OfxRectD &roiD )
	{
		if( !plug || !plug->getInput() )
		{
			return;
		}
		Box2i roiI(
			V2i( (int)std::floor( roiD.x1 ), (int)std::floor( roiD.y1 ) ),
			V2i( (int)std::ceil(  roiD.x2 ), (int)std::ceil(  roiD.y2 ) )
		);
		// Clamp to the plug's data window, and include the window in the
		// hash so a shift inside the RoI invalidates the cache.
		Box2i clipDw = plug->dataWindowPlug()->getValue();
		plug->dataWindowPlug()->hash( h );
		roiI = Box2i(
			V2i( std::max( roiI.min.x, clipDw.min.x ), std::max( roiI.min.y, clipDw.min.y ) ),
			V2i( std::min( roiI.max.x, clipDw.max.x ), std::min( roiI.max.y, clipDw.max.y ) )
		);
		if( roiI.size().x <= 0 || roiI.size().y <= 0 )
		{
			return;
		}

		IECore::ConstStringVectorDataPtr channelNamesData = plug->channelNamesPlug()->getValue();
		const auto &channels = channelNamesData->readable();
		for( int yy = roiI.min.y; yy < roiI.max.y; yy += ImagePlug::tileSize() )
		{
			for( int xx = roiI.min.x; xx < roiI.max.x; xx += ImagePlug::tileSize() )
			{
				V2i srcTileOrigin( xx, yy );
				for( const auto &ch : channels )
				{
					h.append( plug->channelDataHash( ch, srcTileOrigin ) );
				}
			}
		}
	};

	// Source clip
	auto it = rois.find( instance->getClip( "Source" ) );
	if( it != rois.end() )
	{
		hashInputRegion( inPlug(), it->second );
	}

	// Extra clip plugs
	for( const auto &plugName : clipPlugNames() )
	{
		if( auto *imgPlug = getChild<GafferImage::ImagePlug>( plugName ) )
		{
			std::string ofxClipName = plugName;
			if( !ofxClipName.empty() && islower( ofxClipName[0] ) )
			{
				ofxClipName[0] = toupper( ofxClipName[0] );
			}
			auto ci = rois.find( instance->getClip( ofxClipName ) );
			if( ci != rois.end() )
			{
				hashInputRegion( imgPlug, ci->second );
			}
		}
	}
}

void OFXImageNode::hashOfxRenderBuffer( const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImagePlug::GlobalScope globalScope( context );

	const std::shared_ptr<GafferOFX::EffectImageInstance> instance = snapshotInstance();
	if( !instance )
	{
		// No valid OFX instance: pass through input data. Hash input plugs
		// to invalidate the cache when inputs change.
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

	// Renderer preference changes the render path (GL device / CPU).
	GLRenderModePlug()->hash( h );

	if( !m_tiledRenderSupported || !inPlug()->getInput() )
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
		for( const auto &name : clipPlugNames() )
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
		// Tiled path: hash clip metadata only
		// (format/dataWindow/channelNames), not tile-level channel data.
		// Per-tile pixel hashing is in hashChannelData.
		for( const auto &name : clipPlugNames() )
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

	// Include the frame in the hash so time-varying OFX plugins invalidate
	// the cache across frames, whether or not they declare _frameVarying.
	// getClipPreferences runs once during createPluginInstance; hashing
	// remains side-effect-free.
	if( instance )
	{
		h.append( context->getFrame() );
	}
}

Gaffer::ValuePlug::CachePolicy OFXImageNode::computeCachePolicy( const Gaffer::ValuePlug *output ) const
{
	if( output == tileBufferPlug() || output == ofxRenderBufferPlug() )
	{
		return ValuePlug::CachePolicy::TaskCollaboration;
	}
	return ImageProcessor::computeCachePolicy( output );
}

IECore::ConstCompoundObjectPtr OFXImageNode::computeTileBuffer( const Gaffer::Context *context ) const
{
	// Count before snapshotting (see hashTileBuffer).
	RenderingCounter _rcTile( this );
	const std::shared_ptr<GafferOFX::EffectImageInstance> instance = snapshotInstance();
	if( !instance )
	{
		return nullptr;
	}
	// Copy the context before GlobalScope strips tile-level entries.
	// The render invocation needs tileOrigin in its context so that
	// fetchInputImage pulls the correct upstream tiles.
	Gaffer::ConstContextPtr ctxCopy = new Gaffer::Context( *context );

	const Imath::V2i tileOrigin = context->get<V2i>( ImagePlug::tileOriginContextName );

	// Enter GlobalScope so metadata pulls (dataWindow) are not
	// fragmented by tileOrigin.
	ImagePlug::GlobalScope globalScope( context );

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
		// setValue(nullptr) is not permitted; the dispatch site substitutes
		// NullObject, and returning null here allows early exit.
		return nullptr;
	}

	// Build result container with empty tile-sized buffers.
	CompoundObjectPtr result = new CompoundObject();
	int numPixels = ts * ts;
	FloatVectorDataPtr rData = new FloatVectorData();
	FloatVectorDataPtr gData = new FloatVectorData();
	FloatVectorDataPtr bData = new FloatVectorData();
	FloatVectorDataPtr aData = new FloatVectorData();
	rData->writable().resize( numPixels, 0.0f );
	gData->writable().resize( numPixels, 0.0f );
	bData->writable().resize( numPixels, 0.0f );
	aData->writable().resize( numPixels, 0.0f );
	result->members()["R"] = rData;
	result->members()["G"] = gData;
	result->members()["B"] = bData;
	result->members()["A"] = aData;
	result->members()["renderBox"] = new Box2iData( renderBox );

	OfxRectD renderWindowD = { (double)renderBox.min.x, (double)renderBox.min.y,
				(double)renderBox.max.x, (double)renderBox.max.y };
	OfxRectI renderWindowI = { renderBox.min.x, renderBox.min.y,
				renderBox.max.x, renderBox.max.y };

	// Identity check (for example FrameHold pass-through at a different
	// frame). Identity effects do not render; the host copies the
	// identity clip at identityTime into the output.
	{
		OfxTime identityTime = frame;
		std::string identityClip;
		if( instance->isIdentityAction( identityTime, kOfxImageFieldNone, renderWindowI, renderScale, identityClip ) == kOfxStatOK )
		{
			if( identityClip == "Source" && inPlug()->getInput() )
			{
				// Pull the source at identityTime (whole-tile pulls, so the
				// input cache handles reuse) and copy the renderBox region
				// into the tile. Pull only channels the input provides:
				// readers (for example OIIO) report missing channels as
				// errors. Inject alpha=1.0 for RGB-only inputs, matching
				// the readPlugToRGBA convention in the render path.
				Gaffer::Context::EditableScope idEdit( context );
				idEdit.setFrame( identityTime );

				IECore::ConstStringVectorDataPtr chNamesData = inPlug()->channelNamesPlug()->getValue();
				const auto &chNames = chNamesData->readable();
				const bool hasA = std::find( chNames.begin(), chNames.end(), "A" ) != chNames.end();

				std::map<std::string, IECore::ConstFloatVectorDataPtr> tiles;
				for( const char *ch : { "R", "G", "B", "A" } )
				{
					if( ch[0] == 'A' && !hasA )
					{
						continue;
					}
					IECore::ConstFloatVectorDataPtr td = inPlug()->channelData( ch, tileOrigin );
					if( !td )
					{
						throw IECore::Exception( "OFX identity: failed to pull input channel " + std::string( ch ) );
					}
					tiles[ch] = td;
				}
				{
					vector<float> &rVec = rData->writable();
					vector<float> &gVec = gData->writable();
					vector<float> &bVec = bData->writable();
					vector<float> &aVec = aData->writable();
					const vector<float> &rSrc = tiles["R"]->readable();
					const vector<float> &gSrc = tiles["G"]->readable();
					const vector<float> &bSrc = tiles["B"]->readable();
					const vector<float> *aSrc = hasA ? &tiles["A"]->readable() : nullptr;
					for( int y = renderBox.min.y; y < renderBox.max.y; ++y )
					{
						int tileRow = ( y - tileOrigin.y ) * ts;
						for( int x = renderBox.min.x; x < renderBox.max.x; ++x )
						{
							int tileIdx = tileRow + ( x - tileOrigin.x );
							rVec[tileIdx] = rSrc[tileIdx];
							gVec[tileIdx] = gSrc[tileIdx];
							bVec[tileIdx] = bSrc[tileIdx];
							aVec[tileIdx] = aSrc ? (*aSrc)[tileIdx] : 1.0f;
						}
					}
					return result;
				}
			}
		}
	}

	// RoI and isIdentity actions run outside the render lock. For the
	// bundled plugins these perform parameter arithmetic only.
	std::map<OFX::Host::ImageEffect::ClipInstance *, OfxRectD> rois;
	instance->getRegionOfInterestAction( frame, renderScale, renderWindowD, rois );

	std::map<std::string, OfxRectD> clipRoIs;
	for( auto &[clipPtr, roi] : rois )
	{
		if( clipPtr )
		{
			clipRoIs[clipPtr->getName()] = roi;
		}
	}

	// All metadata pulls and the RoI action are done. The section below
	// (beginRender/render/endRender + output extraction) must not run
	// concurrently for InstanceSafe and Unsafe plugins: per-instance
	// mutex for InstanceSafe, global mutex for Unsafe, no lock for
	// FullySafe.
	std::unique_lock<std::mutex> renderLock( m_renderMutex, std::defer_lock );
	std::unique_lock<std::mutex> globalLock( m_globalRenderMutex, std::defer_lock );
	if( m_renderThreadSafety == RenderSafety::InstanceSafe )
	{
		renderLock.lock();
	}
	else if( m_renderThreadSafety == RenderSafety::Unsafe )
	{
		globalLock.lock();
	}

	RenderInvocation inv;
	inv.time = frame;
	inv.renderWindow = renderWindowI;
	inv.renderScale.x = renderScale.x;
	inv.renderScale.y = renderScale.y;
	inv.projectWidth = dataWindow.size().x;
	inv.projectHeight = dataWindow.size().y;
	inv.context = ctxCopy;
	inv.clipRoIs = clipRoIs;

	OfxStatus renderStatus = kOfxStatOK;
	{
		RenderInvocationGuard guard( inv );
		instance->beginRenderAction( frame, frame, 1.0, false, renderScale, true, true );
		renderStatus = instance->renderAction( frame, kOfxImageFieldNone, renderWindowI, renderScale, true, true, false );
		instance->endRenderAction( frame, frame, 1.0, false, renderScale, true, true );
	}

	// Propagation order: exception first, then status.
	if( inv.exception )
	{
		std::rethrow_exception( inv.exception );
	}
	if( renderStatus != kOfxStatOK && renderStatus != kOfxStatReplyDefault )
	{
		GLContextManager &gl = GLContextManager::instance();
		IECore::msg( IECore::Msg::Error, "GafferOFX::OFXImageNode",
			"OFX renderAction failed for \"" + instance->getPlugin()->getIdentifier() +
			"\" (tile path backend=" + gl.backendName() +
			" renderer=" + gl.rendererString() +
			" status=" + std::to_string( renderStatus ) + ")" );
		throw IECore::Exception( "OFX renderAction failed" );
	}

	// De-interleave output into result
	auto *outputImg = inv.outputImage;
	if( outputImg )
	{
		OfxRectI ob = outputImg->getBounds();
		if( ob.x2 > ob.x1 && ob.y2 > ob.y1 )
		{
			vector<float> &rVec = rData->writable();
			vector<float> &gVec = gData->writable();
			vector<float> &bVec = bData->writable();
			vector<float> &aVec = aData->writable();

			deinterleaveImageToPlanar(
				static_cast<GafferOFX::Image*>( outputImg ),
				renderBox, tileOrigin, ts, (size_t)numPixels,
				rVec, gVec, bVec, aVec
			);
		}
	}

	return result;
}

IECore::ConstCompoundObjectPtr OFXImageNode::computeOfxRenderBuffer( const Gaffer::Context *context ) const
{
	std::lock_guard<std::mutex> lock( m_renderMutex );
	CompoundObjectPtr result = new CompoundObject();

	// Count before snapshotting (see hashTileBuffer).
	RenderingCounter _rcBuffer( this );
	const std::shared_ptr<GafferOFX::EffectImageInstance> instance = snapshotInstance();
	if( !instance )
	{
		return result;
	}

	ImagePlug::GlobalScope globalScope( context );

	GafferOFX::ClipInstance* sourceClip = dynamic_cast<GafferOFX::ClipInstance*>( instance->getClip( "Source" ) );
	GafferOFX::ClipInstance* outputClip = dynamic_cast<GafferOFX::ClipInstance*>( instance->getClip( "Output" ) );

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
			instance->getRegionOfDefinitionAction( frame, renderScale, rod );
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

	// Tiled path: compute metadata (dataWindow + pixelAspect) without a
	// full-frame render.
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

	// Identity check (for example FrameHold pass-through at a different frame).
	{
		OfxTime identityTime = frame;
		std::string identityClip;
		if( instance->isIdentityAction( identityTime, kOfxImageFieldNone, renderWindow, renderScale, identityClip ) == kOfxStatOK )
		{
			if( identityClip == "Source" && hasInput && sourceClip )
			{
				// Read the source at identityTime and return it as output.
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
					const size_t idPixels = checkedPixelCount( idW, idH );
					rData->writable().resize( idPixels );
					gData->writable().resize( idPixels );
					bData->writable().resize( idPixels );
					aData->writable().resize( idPixels );

					GafferImage::Sampler rSamp( inPlug(), "R", idDw );
					GafferImage::Sampler gSamp( inPlug(), "G", idDw );
					GafferImage::Sampler bSamp( inPlug(), "B", idDw );
					IECore::ConstStringVectorDataPtr chNames = inPlug()->channelNamesPlug()->getValue();
					bool hasA = false;
					for( const auto &c : chNames->readable() ) { if( c == "A" ) { hasA = true; break; } }
					std::unique_ptr<GafferImage::Sampler> aSamp;
					if( hasA )
					{
						aSamp = std::make_unique<GafferImage::Sampler>( inPlug(), "A", idDw );
					}

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

	// Get RoI for each clip and build clipRoIs map
	std::map<OFX::Host::ImageEffect::ClipInstance *, OfxRectD> rois;
	OfxRectD regionOfInterest = {
		(double)dataWindow.min.x, (double)dataWindow.min.y,
		(double)dataWindow.max.x, (double)dataWindow.max.y
	};
	instance->getRegionOfInterestAction( frame, renderScale, regionOfInterest, rois );

	std::map<std::string, OfxRectD> clipRoIs;
	for( auto &[clipPtr, roi] : rois )
	{
		if( clipPtr )
		{
			clipRoIs[clipPtr->getName()] = roi;
		}
	}

	// Query GL support. GL-capable plugins render on the OFXRenderWorker
	// for context affinity; remaining plugins render inline without GL.
	bool pluginSupportsGL = false;
	bool glIsNeeded = false;
	try
	{
		std::string val = instance->getPlugin()->getDescriptor().getProps().getStringProperty(
			kOfxImageEffectPropOpenGLRenderSupported
		);
		pluginSupportsGL = ( val == "true" || val == "needed" );
		glIsNeeded = ( val == "needed" );
	}
	catch( const std::exception & ) {}

	int w = renderWindow.x2 - renderWindow.x1;
	int h = renderWindow.y2 - renderWindow.y1;

	Gaffer::ConstContextPtr ctxCopy = new Gaffer::Context( *Gaffer::Context::current() );

	RenderInvocation inv;
	inv.time = frame;
	inv.projectWidth = dataWindow.size().x;
	inv.projectHeight = dataWindow.size().y;
	inv.renderWindow = renderWindow;
	inv.renderScale.x = renderScale.x;
	inv.renderScale.y = renderScale.y;
	inv.context = ctxCopy;
	inv.clipRoIs = clipRoIs;

	// Shared render function used by both CPU (inline) and GL (worker) paths.
	OfxStatus renderStatus = kOfxStatOK;
	bool useGL = false;
	auto renderFunc = [&]()
	{
		RenderInvocationGuard guard( inv );
		instance->beginRenderAction( frame, frame, 1.0, false, renderScale, true, true );
		renderStatus = instance->renderAction( frame, kOfxImageFieldNone, renderWindow, renderScale, true, true, false );
		instance->endRenderAction( frame, frame, 1.0, false, renderScale, true, true );
	};

	// Apply the renderer preference to the shared GL context. CPU and GPU
	// select the software and hardware device classes respectively; the
	// most recent render determines the current device. GL-capable
	// plugins render through GL (their non-GL path reports failure), so
	// the CPU path below applies to plugins without GL support.
	GLContextManager::RenderMode renderMode = GLContextManager::RenderMode::Auto;
	switch( GLRenderModePlug()->getValue() )
	{
		case 1 :
			renderMode = GLContextManager::RenderMode::CPU;
			break;
		case 2 :
			renderMode = GLContextManager::RenderMode::GPU;
			break;
		default :
			break;
	}

	GLContextManager::instance().setRenderMode( (int)renderMode );

	if( pluginSupportsGL )
	{
		// GL path: dispatch to the worker for context affinity. Prefetch
		// input images on the compute thread first; the worker does not
		// pull from Gaffer directly, since that would wait on
		// m_renderMutex when another thread holds it for a different
		// node. Fetch with the full RoI so the resolveFetchRegion
		// floor/ceil and RoD handling match between prefetch and render.
		{
			RenderInvocationGuard guard( inv );
			for( const auto &[clipName, roI] : clipRoIs )
			{
				if( clipName == "Output" )
				{
					continue;
				}
				GafferOFX::ClipInstance *clip = dynamic_cast<GafferOFX::ClipInstance*>(
					instance->getClip( clipName )
				);
				if( !clip || clipName == "Output" )
				{
					continue;
				}
				const GafferImage::ImagePlug *plug = nullptr;
				if( clipName == "Source" )
				{
					plug = inPlug();
				}
				else if( !clip->plugName().empty() )
				{
					plug = instance->node()->getChild<GafferImage::ImagePlug>( clip->plugName() );
				}
				if( !plug || !plug->getInput() )
				{
					continue;
				}
				OfxRectD roiD = roI;
				if( auto *img = static_cast<GafferOFX::Image*>( clip->getImage( frame, &roiD ) ) )
				{
					inv.prefetched[clipName] = img;
				}
			}
			if( inv.exception )
			{
				std::rethrow_exception( inv.exception );
			}
		}

		// The worker holds the current EGL context; calling makeCurrent on
		// another thread would move current-context status away from it.
		OFXRenderWorker::instance().execute( [&]()
		{
			GLContextManager &gl = GLContextManager::instance();
			useGL = gl.makeCurrent();

			// Plugins that require GL report a render failure when no
			// context is available.
			if( glIsNeeded && !useGL )
			{
				throw IECore::Exception(
					"OFX plugin requires OpenGL but no GL context is available"
				);
			}

			if( useGL )
			{
				// A context rebuild (device change via GLRenderMode)
				// invalidates GL objects the plugin cached on the previous
				// context. The worker makes the new context current before
				// this point, so run the detach/attach cycle for the plugin
				// to release prior state and initialise on the new device.
				if( m_glContextAttached && m_glContextGeneration != gl.contextGeneration() )
				{
					instance->contextDetachedAction();
					m_glContextAttached = false;
				}

				if( !m_glContextAttached )
				{
					m_glContextAttached = true;
					m_glContextGeneration = gl.contextGeneration();
					instance->getProps().setIntProperty( kOfxImageEffectPropOpenGLEnabled, 1 );
					instance->contextAttachedAction();
				}
			}

			if( useGL )
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

				if( GLContextManager::glCheckFramebufferStatusF( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
				{
					throw IECore::Exception( "OFX GL render target is not framebuffer-complete" );
				}

				glViewport( 0, 0, w, h );
				glClearColor( 0.25f, 0.5f, 0.75f, 1.0f );
				glClear( GL_COLOR_BUFFER_BIT );
			}

			// Run the render action on the worker thread, where the GL
			// context is current.
			renderFunc();

			// ---- GL readback (if plugin rendered into our FBO) ----
			if( useGL && pluginSupportsGL )
			{
				GLContextManager::glBindFramebufferF( GL_FRAMEBUFFER, gl.outputFBO() );

				// Sentinel: compare the clear colour at several spread pixels.
				// A single-pixel probe reports incorrectly for plugins that
				// leave the origin untouched or output the clear colour there.
				float probe[3][4];
				glReadPixels( 0, 0, 1, 1, GL_RGBA, GL_FLOAT, probe[0] );
				glReadPixels( w / 2, h / 2, 1, 1, GL_RGBA, GL_FLOAT, probe[1] );
				glReadPixels( w - 1, h - 1, 1, 1, GL_RGBA, GL_FLOAT, probe[2] );
				auto approxEq = []( float a, float b, float eps ) {
					return ( a - b ) < eps && ( b - a ) < eps;
				};
				auto isClear = [&]( int i ) {
					return approxEq( probe[i][0], 0.25f, 0.001f ) &&
					approxEq( probe[i][1], 0.50f, 0.001f ) &&
					approxEq( probe[i][2], 0.75f, 0.001f );
				};
				if( !isClear( 0 ) || !isClear( 1 ) || !isClear( 2 ) )
				{
					// Plugin rendered to the FBO. Read pixels directly into
					// result members (inv.outputImage can be null when the
					// plugin used loadTexture instead of getImage).
					// The FBO read and the CPU path below are mutually
					// exclusive by sentinel state: when the sentinel
					// differs the FBO holds the render, and when intact
					// the CPU de-interleave below handles the output.
					glFinish();
					glPixelStorei( GL_PACK_ALIGNMENT, 1 );

					const size_t numPixels = checkedPixelCount( w, h );
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

					for( size_t i = 0; i < numPixels; ++i )
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
				// Sentinel intact with inv.outputImage set means the plugin
				// wrote to getImage(Output) on the CPU; the de-interleave
				// below is the only reader for that case.
			}
		} );

	}
	else
	{
		// CPU path: no GL interaction; render inline on the compute thread,
		// serialised by m_renderMutex and re-entered through the TLS
		// invocation stack. Reset OpenGLEnabled so plugins with GL support
		// do not attempt GL work with no context current on this thread.
		instance->getProps().setIntProperty( kOfxImageEffectPropOpenGLEnabled, 0 );
		renderFunc();
	}

	// ---- Propagate cancellation / check render status ----
	// A failed Gaffer pull surfaces as IECore::Cancelled first so the
	// request retries silently; only other statuses fail the render,
	// which would otherwise mark the node on every viewer pan.
	if( inv.exception )
	{
		std::rethrow_exception( inv.exception );
	}

	if( renderStatus != kOfxStatOK && renderStatus != kOfxStatReplyDefault )
	{
		// Log plugin, GL path, backend state and context generation with
		// the failure for post-mortem diagnosis of renders that fail
		// inside plugin code.
		GLContextManager &gl = GLContextManager::instance();
		IECore::msg( IECore::Msg::Error, "GafferOFX::OFXImageNode",
			"OFX renderAction failed for \"" + instance->getPlugin()->getIdentifier() +
			"\" (useGL=" + std::to_string( useGL ) +
			" backend=" + gl.backendName() +
			" renderer=" + gl.rendererString() +
			" contextGeneration=" + std::to_string( gl.contextGeneration() ) +
			" attachedGeneration=" + std::to_string( m_glContextGeneration.load() ) +
			" status=" + std::to_string( renderStatus ) + ")" );
		throw IECore::Exception( "OFX renderAction failed" );
	}

	// ---- De-interleave output into result (compute thread, worker is done) ----
	// Skip when GL readback already populated the result (sentinel
	// indicated a GPU render).
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
				const size_t outPixels = checkedPixelCount( ow, oh );
				rData->writable().resize( outPixels );
				gData->writable().resize( outPixels );
				bData->writable().resize( outPixels );
				aData->writable().resize( outPixels );

				vector<float> &rVec = rData->writable();
				vector<float> &gVec = gData->writable();
				vector<float> &bVec = bData->writable();
				vector<float> &aVec = aData->writable();

				deinterleaveImageToPlanar(
					static_cast<GafferOFX::Image*>( outputImg ),
					Box2i( V2i( ob.x1, ob.y1 ), V2i( ob.x2, ob.y2 ) ),
					V2i( ob.x1, ob.y1 ), ow, outPixels,
					rVec, gVec, bVec, aVec
				);

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
	// Callers (bindings, UI) run on the main thread, as does
	// destroyInstance, so the raw pointer remains valid for the
	// duration of these calls. The lock only guards against
	// compute-thread publication races.
	std::lock_guard<std::recursive_mutex> lock( m_instanceMutex );
	return m_instance.get();
}

std::string OFXImageNode::persistentMessage() const
{
	const std::shared_ptr<GafferOFX::EffectImageInstance> instance = lockedInstance();
	if( !instance )
	{
		return "";
	}
	return instance->persistentMessage();
}

bool OFXImageNode::supportsGL() const
{
	const std::shared_ptr<GafferOFX::EffectImageInstance> instance = lockedInstance();
	if( !instance )
	{
		return false;
	}
	try
	{
		const std::string val = instance->getPlugin()->getDescriptor().getProps().getStringProperty(
			kOfxImageEffectPropOpenGLRenderSupported
		);
		return val == "true" || val == "needed";
	}
	catch( const std::exception & )
	{
		return false;
	}
}

bool OFXImageNode::hasOverlay() const
{
	const std::shared_ptr<GafferOFX::EffectImageInstance> instance = lockedInstance();
	if( !instance )
	{
		return false;
	}
	// Calling getOverlayDescriptor() triggers kOfxActionDescribe on the
	// overlay interact via the Context descriptor, which reads the
	// overlay interact main entry from the Context descriptor's
	// properties (set during DescribeInContext by the plugin's
	// setOverlayInteractDescriptor() call).
	OFX::Host::Interact::Descriptor &desc = instance->getOverlayDescriptor();
	OFX::Host::Interact::State state = desc.getState();
	return state == OFX::Host::Interact::eDescribed
		|| state == OFX::Host::Interact::eCreated;
}

GafferOFXInteractInstance* OFXImageNode::getInteract()
{
	const std::shared_ptr<GafferOFX::EffectImageInstance> instance = lockedInstance();
	if( !m_interactInstance && instance && hasOverlay() )
	{
		// 32-bit float with hasAlpha=true, matching the float RGBA clips
		// used throughout.
		m_interactInstance = std::make_unique<GafferOFXInteractInstance>( *instance, 32, true );
		m_interactInstance->createInstance();
	}
	return m_interactInstance.get();
}

void OFXImageNode::destroyInteract()
{
	m_interactInstance.reset();
}
