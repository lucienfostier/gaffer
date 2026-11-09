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

#pragma once

#include "GafferOFX/EffectImageInstance.h"
#include "GafferOFX/Export.h"
#include "GafferOFX/TypeIds.h"

#include "Gaffer/StringPlug.h"
#include "Gaffer/TypedObjectPlug.h"

#include "GafferImage/ImageProcessor.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace GafferOFX
{

class GafferOFXInteractInstance;

class GAFFEROFX_API OFXImageNode : public GafferImage::ImageProcessor
{

	public :

		explicit OFXImageNode( const std::string &name=defaultName<OFXImageNode>() );
		~OFXImageNode() override;

		GAFFER_NODE_DECLARE_TYPE( GafferOFX::OFXImageNode, OFXImageNodeTypeId, GafferImage::ImageProcessor );

		bool createPluginInstance();

		/// Descriptor-driven plug setup. Walks the context descriptor for
		/// `pluginId`, calls `ParamAlgo::setupPlugs`, and creates or
		/// recreates the instance. Runs in a single UndoScope and must be
		/// called on the main thread.
		bool loadPlugin( const std::string &pluginId, bool keepExistingValues = true );

		Gaffer::StringPlug* pluginIdPlug();
		const Gaffer::StringPlug* pluginIdPlug() const;


		Gaffer::Plug *parametersPlug();
		const Gaffer::Plug *parametersPlug() const;

		/// Renderer preference for GL-capable plugins: Auto (0), CPU (1)
		/// or GPU (2). CPU renders through the software EGL device and GPU
		/// forces the hardware device.
		Gaffer::IntPlug *GLRenderModePlug();
		const Gaffer::IntPlug *GLRenderModePlug() const;

		/// Returns true when the current plugin advertises OpenGL
		/// rendering ("true" or "needed"). Controls the visibility of the
		/// `GLRenderMode` plug.
		bool supportsGL() const;


		void affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const override;

		void hash( const Gaffer::ValuePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		void compute( Gaffer::ValuePlug *output, const Gaffer::Context *context ) const override;

		const GafferOFX::EffectImageInstance* effectInstance() const;

		/// Returns the last persistent message set by the plugin on this
		/// node's instance. Empty when cleared or never set.
		std::string persistentMessage() const;

		Gaffer::CompoundObjectPlug *ofxRenderBufferPlug();
		const Gaffer::CompoundObjectPlug *ofxRenderBufferPlug() const;

		Gaffer::ObjectPlug *tileBufferPlug();
		const Gaffer::ObjectPlug *tileBufferPlug() const;

	protected :

		void hashFormat( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		void hashDataWindow( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		void hashSampleOffsets( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		void hashChannelNames( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		void hashChannelData( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;

		void hashOfxRenderBuffer( const Gaffer::Context *context, IECore::MurmurHash &h ) const;
		void hashTileBuffer( const Gaffer::Context *context, IECore::MurmurHash &h ) const;

		Gaffer::ValuePlug::CachePolicy computeCachePolicy( const Gaffer::ValuePlug *output ) const override;

		GafferImage::Format computeFormat( const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		Imath::Box2i computeDataWindow( const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		IECore::ConstCompoundDataPtr computeMetadata( const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		bool computeDeep( const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		IECore::ConstIntVectorDataPtr computeSampleOffsets( const Imath::V2i &tileOrigin, const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		IECore::ConstStringVectorDataPtr computeChannelNames( const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		IECore::ConstFloatVectorDataPtr computeChannelData( const std::string &channelName, const Imath::V2i &tileOrigin, const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;

		IECore::ConstCompoundObjectPtr computeOfxRenderBuffer( const Gaffer::Context *context ) const;
		IECore::ConstCompoundObjectPtr computeTileBuffer( const Gaffer::Context *context ) const;


	public :

		bool hasOverlay() const;
		class GafferOFXInteractInstance* getInteract();
		void destroyInteract();

		int rendering() const { return m_rendering; }

		void renderStarted() const;
		void renderFinished() const;

		/// Incremented every time the effect instance is destroyed, whether
		/// by a plugin change, reload or structural invalidation. Lets UI
		/// caches that hold raw interact pointers detect replacement.
		unsigned long long instanceGeneration() const { return m_instanceGeneration.load(); }
		bool settingFromPlugin() const { return m_settingFromPlugin; }
		void setSettingFromPlugin( bool v ) const { m_settingFromPlugin = v; }

		/// Render thread safety level read from the plugin descriptor when
		/// the instance is created.
		enum class RenderSafety { Unknown, FullySafe, InstanceSafe, Unsafe };

	private :

		void plugSet( Gaffer::Plug *plug );
		void destroyInstance();
		void parametersPlugChildAdded( Gaffer::GraphComponent *parent, Gaffer::GraphComponent *child );
		void parametersPlugChildRemoved( Gaffer::GraphComponent *parent, Gaffer::GraphComponent *child );
		void removeClipPlugs();
		void createClipPlugs( GafferOFX::EffectImageInstance *instance );

		/// Returns a locked copy of the dynamic clip plug names for compute
		/// threads. Iterating the vector without the lock races main-thread
		/// calls to `push_back` and `clear`.
		std::vector<std::string> clipPlugNames() const;

		/// Acquires the instance, creating it on demand for deserialized
		/// scripts. Called from const hash and compute methods. Creation
		/// binds to existing plugs without repeating descriptor plug setup.
		/// Returns a reference-counted handle, so a concurrent
		/// `destroyInstance` cannot free the instance mid-compute. Null when
		/// no plugin is set or creation fails.
		std::shared_ptr<GafferOFX::EffectImageInstance> acquireInstance() const;

		/// Returns a locked read without creating an instance. Used by
		/// main-thread paths that race compute-thread publication.
		std::shared_ptr<GafferOFX::EffectImageInstance> lockedInstance() const;

		static size_t g_firstPlugIndex;
		mutable std::shared_ptr<GafferOFX::EffectImageInstance> m_instance;
		// Guards publication, replacement and locked reads of `m_instance`.
		// Held only for short sections, never across plugin callbacks:
		// create-time plug writes cancel and wait on tasks that may
		// themselves hold instance references. Recursive, because plug
		// signals during setup re-enter `plugSet` and destroy on the same
		// thread.
		mutable std::recursive_mutex m_instanceMutex;
		// Serialises concurrent instance builds. Held across plugin
		// callbacks, so tasks a builder waits for must never wait on it.
		// `m_instanceMutex` is different: locked readers take it.
		mutable std::recursive_mutex m_createMutex;
		// Guards plug creation during instance setup, where parameter
		// constructors bind to plugs. Read on the main thread by the
		// structural handlers and written by `createPluginInstance` on any
		// thread, so it must be atomic. Creation inside a compute is
		// bind-only: `setupPlugs` and clip plug creation are skipped while a
		// Process is active, and constructors only bind. The graph is
		// therefore never mutated from a compute thread.
		mutable std::atomic<bool> m_creatingInstance{ false };
		mutable std::atomic<unsigned long long> m_instanceGeneration{ 0 };
		mutable std::atomic<bool> m_glContextAttached = false;
		/// `GLContextManager::contextGeneration()` at the last
		/// `contextAttached`. A mismatch means the shared context was
		/// rebuilt onto another device since this node attached, so the
		/// plugin's GL state is no longer valid.
		mutable std::atomic<int> m_glContextGeneration = -1;
		mutable std::atomic<int> m_rendering = 0;
		// destroyInstance waits on this until in-flight renders drain.
		mutable std::mutex m_renderDrainedMutex;
		mutable std::condition_variable m_renderDrained;
		mutable bool m_settingFromPlugin = false;
		// Read from compute threads, written by main-thread creation.
		mutable std::atomic<bool> m_tiledRenderSupported = false;
		mutable std::atomic<RenderSafety> m_renderThreadSafety = RenderSafety::Unknown;
		mutable std::mutex m_renderMutex;
		// Global across all nodes: Unsafe plugins must not render
		// concurrently anywhere, not just within one node.
		static std::mutex g_globalRenderMutex;
		// Guards m_clipPlugNames: mutated on the main thread by
		// createClipPlugs()/removeClipPlugs(), read from any thread by
		// affects().
		mutable std::mutex m_clipPlugNamesMutex;
		std::vector<std::string> m_clipPlugNames;
		std::unique_ptr<GafferOFXInteractInstance> m_interactInstance;

};

IE_CORE_DECLAREPTR( OFXImageNode )

} // namespace GafferOFX
