//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2026, Lucien Fostier. All rights reserved.
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
//  NONINFRINGEMENT) OR OTHERWISE ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////
#include "GafferOFX/EffectImageInstance.h"

#include "GafferOFX/ParamAlgo.h"
#include "GafferOFX/ClipInstance.h"
#include "GafferOFX/Host.h"
#include "GafferOFX/ParamInstance.h"
#include "GafferOFX/OFXImageNode.h"

#include "Gaffer/Context.h"
#include "Gaffer/Metadata.h"
#include "Gaffer/Plug.h"
#include "Gaffer/Process.h"

#include "GafferImage/ImagePlug.h"
#include "GafferImage/Sampler.h"
#include "GafferImage/FormatPlug.h"

#include "IECore/Exception.h"
#include "IECore/MessageHandler.h"
#include "IECore/SimpleTypedData.h"
#include "IECore/VectorTypedData.h"

#include "HostSupport/ofxhPluginCache.h"
#include "HostSupport/ofxhImageEffectAPI.h"

#include "ofxInteract.h"

#include "tbb/task_arena.h"

#include <cassert>
#include <iostream>

namespace
{

// Read RGBA channels from an ImagePlug into a flat interleaved OfxRGBAColourF
// buffer covering the given data window.  If the input lacks an Alpha channel,
// alpha=1.0 is injected.
void readPlugToRGBA( const GafferImage::ImagePlug *plug, OfxRGBAColourF *buffer, const Imath::Box2i &dataWindow, int width )
{
	IECore::ConstStringVectorDataPtr channelNamesData = plug->channelNamesPlug()->getValue();
	bool hasR = false, hasG = false, hasB = false, hasAlpha = false;
	for( const auto &ch : channelNamesData->readable() )
	{
		if( ch == "R" ) hasR = true;
		else if( ch == "G" ) hasG = true;
		else if( ch == "B" ) hasB = true;
		else if( ch == "A" ) hasAlpha = true;
	}

	std::unique_ptr<GafferImage::Sampler> rSampler, gSampler, bSampler, aSampler;
	if( hasR ) rSampler = std::make_unique<GafferImage::Sampler>( plug, "R", dataWindow );
	if( hasG ) gSampler = std::make_unique<GafferImage::Sampler>( plug, "G", dataWindow );
	if( hasB ) bSampler = std::make_unique<GafferImage::Sampler>( plug, "B", dataWindow );
	if( hasAlpha ) aSampler = std::make_unique<GafferImage::Sampler>( plug, "A", dataWindow );

	for( int y = dataWindow.min.y; y < dataWindow.max.y; ++y )
	{
		for( int x = dataWindow.min.x; x < dataWindow.max.x; ++x )
		{
			int idx = ( y - dataWindow.min.y ) * width + ( x - dataWindow.min.x );
			buffer[idx].r = rSampler ? rSampler->sample( x, y ) : 0.0f;
			buffer[idx].g = gSampler ? gSampler->sample( x, y ) : 0.0f;
			buffer[idx].b = bSampler ? bSampler->sample( x, y ) : 0.0f;
			buffer[idx].a = aSampler ? aSampler->sample( x, y ) : 1.0f;
		}
	}
}

// Read the alpha channel from an ImagePlug into a flat single-channel float
// buffer covering the given data window.  Used for Alpha (mask) clip images,
// whose component count is 1 - the plugin reads the first component as the
// mask value.  Masks follow Gaffer's own convention: coverage lives in A
// (Grade, ColorCorrect, etc. all sample the mask's alpha channel).
void readPlugToFloatA( const GafferImage::ImagePlug *plug, float *buffer, const Imath::Box2i &dataWindow, int width )
{
	GafferImage::Sampler aSampler( plug, "A", dataWindow );

	for( int y = dataWindow.min.y; y < dataWindow.max.y; ++y )
	{
		for( int x = dataWindow.min.x; x < dataWindow.max.x; ++x )
		{
			int idx = ( y - dataWindow.min.y ) * width + ( x - dataWindow.min.x );
			buffer[idx] = aSampler.sample( x, y );
		}
	}
}

} // anonymous namespace

using namespace GafferOFX;

// Thread-local stack of active render invocations (stored as pointers so the
// original invocation object owns its outputImage - no double-free from copies).
// A stack (vs single slot) correctly handles nested/stolen renders when
// TBB steals a render task while the thread is blocked inside a Gaffer pull.
thread_local std::vector<RenderInvocation*> g_renderStack;

RenderInvocationGuard::RenderInvocationGuard( RenderInvocation &inv )
{
	g_renderStack.push_back( &inv );
}

RenderInvocationGuard::~RenderInvocationGuard()
{
	if( m_active )
	{
		g_renderStack.pop_back();
	}
}

const RenderInvocation &RenderInvocationGuard::invocation() const
{
	return *g_renderStack.back();
}

RenderInvocation *EffectImageInstance::currentInvocation()
{
	if( g_renderStack.empty() )
	{
		return nullptr;
	}
	return g_renderStack.back();
}

// Thread-local stack of dispatching instances. Pushed in mainEntry (the
// funnel for every plugin dispatch) so host-level suites can route to
// the originating node - e.g. persistent messages.
thread_local std::vector<EffectImageInstance*> g_instanceStack;

EffectImageInstance *EffectImageInstance::currentInstance()
{
	if( g_instanceStack.empty() )
	{
		return nullptr;
	}
	return g_instanceStack.back();
}

void EffectImageInstance::setPersistentMessage( const std::string &message )
{
	std::lock_guard<std::mutex> lock( m_messageMutex );
	m_persistentMessage = message;
}

std::string EffectImageInstance::persistentMessage() const
{
	std::lock_guard<std::mutex> lock( m_messageMutex );
	return m_persistentMessage;
}

// Thread-local stack of in-flight OFX actions per thread.
// Render actions run concurrently on worker threads, so this must
// be thread-local, never a plain member.
thread_local std::vector<std::string> g_actionStack;

std::string EffectImageInstance::currentAction()
{
	if( g_actionStack.empty() )
	{
		return "";
	}
	return g_actionStack.back();
}

void EffectImageInstance::pushAction( const std::string &action )
{
	g_actionStack.push_back( action );
}

void EffectImageInstance::popAction()
{
	if( !g_actionStack.empty() )
	{
		g_actionStack.pop_back();
	}
}

EffectImageInstance::ParamSetPolicy EffectImageInstance::paramSetPolicy( const std::string &paramName ) const
{
	// Render-family writes cannot touch the graph (see header), but the
	// plugin-declared illegal notify pattern (e.g. Shadertoy's end of
	// render toggle) throws on failure. Swallow: report success without
	// writing. HostSupport still delivers instanceChanged(pluginEdited).
	//
	// Known limitation: plugins whose multi-render parameter synchronisation
	// needs real render-time writes (e.g. Shadertoy populating its
	// paramValue* uniform slots via updateExtra) will not see those plugs
	// update - the values have nowhere safe to land mid-compute. Supporting
	// that needs a shadow map (pending values readable via get(), mixed into
	// the render hash, flushed to plugs at safe points such as
	// serialise()). Not implemented.
	const std::string swallowAction = currentAction();
	if( swallowAction == kOfxImageEffectActionRender ||
	swallowAction == kOfxImageEffectActionBeginSequenceRender ||
	swallowAction == kOfxImageEffectActionEndSequenceRender )
	{
		std::lock_guard<std::mutex> lock( m_warnMutex );
		if( m_warnedSetValue.insert( std::string( "swallow:" ) + paramName ).second )
		{
			IECore::msg( IECore::Msg::Info, "GafferOFX",
				"paramSetValue for \"" + paramName + "\" swallowed during action \"" +
				swallowAction + "\" - returning kOfxStatOK without writing" );
		}
		return ParamSetPolicy::Swallow;
	}

	// Belt-and-braces: never write during a Gaffer compute, whatever
	// the claimed action.
	if( Gaffer::Process::current() != nullptr )
	{
		std::lock_guard<std::mutex> lock( m_warnMutex );
		if( m_warnedSetValue.insert( paramName ).second )
		{
			IECore::msg( IECore::Msg::Warning, "GafferOFX",
				"paramSetValue for \"" + paramName + "\" blocked during compute (action \"" +
				currentAction() + "\") - returning kOfxStatFailed" );
		}
		return ParamSetPolicy::Deny;
	}

	const std::string action = currentAction();

	// Legal writers: create, instanceChanged family, syncPrivateData,
	// interact actions. Empty action means direct host/UI call outside
	// dispatch (e.g. notifyPluginEdited path) - allow.
	if( action.empty() )
	{
		return ParamSetPolicy::Write;
	}
	if( action == kOfxActionCreateInstance ||
	action == kOfxActionBeginInstanceChanged ||
	action == kOfxActionInstanceChanged ||
	action == kOfxActionEndInstanceChanged ||
	action == kOfxActionSyncPrivateData )
	{
		// Spec-legal but Gaffer-illegal if lazy creation runs inside
		// compute - Process check above already dropped those. Log
		// create-time writes once for evidence gathering.
		if( action == kOfxActionCreateInstance )
		{
			std::lock_guard<std::mutex> lock( m_warnMutex );
			if( m_warnedSetValue.insert( std::string( "create:" ) + paramName ).second )
			{
				IECore::msg( IECore::Msg::Info, "GafferOFX",
					"paramSetValue for \"" + paramName + "\" during createInstance" );
			}
		}
		return ParamSetPolicy::Write;
	}
	// Interact actions (pen/motion/up/down, key, focus) run on main
	// thread outside computes - allow. Match by prefix to cover
	// OfxInteractAction* and gain/lose focus variants.
	if( action.find( "OfxInteractAction" ) == 0 ||
	action.find( "OfxActionGainInstanceFocus" ) == 0 ||
	action.find( "OfxActionLoseInstanceFocus" ) == 0 ||
	action == kOfxInteractActionPenDown ||
	action == kOfxInteractActionPenMotion ||
	action == kOfxInteractActionPenUp )
	{
		return ParamSetPolicy::Write;
	}

	// Anything else is illegal.
	std::lock_guard<std::mutex> lock( m_warnMutex );
	if( m_warnedSetValue.insert( paramName ).second )
	{
		IECore::msg( IECore::Msg::Warning, "GafferOFX",
			"paramSetValue for \"" + paramName + "\" blocked during action \"" +
			action + "\" - returning kOfxStatFailed" );
	}
	return ParamSetPolicy::Deny;
}

// Instance registration functions from libOfxGafferHost.so
// Used to track valid Param::Instance* pointers for safe handle validation
// without touching the handle's memory (avoids __dynamic_cast SIGSEGV).
namespace OFX { namespace Host { namespace Param {
	void registerInstance(Instance* inst, const void* descriptorHandle);
	void unregisterInstance(Instance* inst);
} } }


EffectImageInstance::EffectImageInstance( OFX::Host::ImageEffect::ImageEffectPlugin* plugin, OFX::Host::ImageEffect::Descriptor& desc, const std::string& context): OFX::Host::ImageEffect::Instance(plugin,desc,context,false)
{
}

EffectImageInstance::~EffectImageInstance()
{
	// Teardown must have been done via shutdown() (dispatch destroy
	// while registered, then unregister). Assert and fallback.
	if( !m_shutdown )
	{
		assert( false && "EffectImageInstance destroyed without shutdown()" );
		IECore::msg( IECore::Msg::Error, "GafferOFX::EffectImageInstance", "Destroyed without shutdown() - possible leak/corruption (destroyInstance() not called on main thread outside compute)" );
		shutdown();
	}
}

OFX::Host::ImageEffect::ClipInstance* EffectImageInstance::newClipInstance(OFX::Host::ImageEffect::Instance* plugin, OFX::Host::ImageEffect::ClipDescriptor* descriptor, int index)
{
	return new ClipInstance(this,descriptor);
}


const std::string &EffectImageInstance::getDefaultOutputFielding() const
{
	static const std::string v(kOfxImageFieldNone);
	return v;
}

OfxStatus EffectImageInstance::vmessage(const char* type, const char* id, const char* format, va_list args)
{
	// The message suite routes plugin messages here (not to Host).
	// Forward so logging, the UI hook and per-instance routing apply.
	// Host::vmessage reads `args` once (with an internal copy for
	// sizing), so forwarding is safe.
	return Host::instance().vmessage(type, id, format, args);
}

OfxStatus EffectImageInstance::setPersistentMessage(const char* type, const char* id, const char* format, va_list args)
{
	// Host routes to this instance via the dispatch stack.
	return Host::instance().setPersistentMessage(type, id, format, args);
}

OfxStatus EffectImageInstance::clearPersistentMessage()
{
	return Host::instance().clearPersistentMessage();
}

void EffectImageInstance::getProjectSize(double& xSize, double& ySize) const
{
	if( auto inv = currentInvocation() )
	{
		if( inv->projectWidth > 0 && inv->projectHeight > 0 )
		{
			xSize = inv->projectWidth;
			ySize = inv->projectHeight;
			return;
		}
	}

	if( auto ctx = Gaffer::Context::current() )
	{
		auto gafferFormat = GafferImage::FormatPlug::getDefaultFormat( ctx );
		xSize = gafferFormat.width();
		ySize = gafferFormat.height();
	}
	else
	{
		xSize = 1920;
		ySize = 1080;
	}
}

void EffectImageInstance::getProjectOffset(double& xOffset, double& yOffset) const
{
	xOffset = 0;
	yOffset = 0;
}

void EffectImageInstance::getProjectExtent(double& xSize, double& ySize) const
{
	if( auto ctx = Gaffer::Context::current() )
	{
		auto gafferFormat = GafferImage::FormatPlug::getDefaultFormat( ctx );
		xSize = gafferFormat.width();
		ySize = gafferFormat.height();
	}
	else
	{
		xSize = 1920;
		ySize = 1080;
	}
}

double EffectImageInstance::getProjectPixelAspectRatio() const
{
	if( auto ctx = Gaffer::Context::current() )
	{
		auto gafferFormat = GafferImage::FormatPlug::getDefaultFormat( ctx );
		return gafferFormat.getPixelAspect();
	}
	return 1.0;
}

double EffectImageInstance::getEffectDuration() const
{

	auto start = 1;
	auto end = 100;
	if( auto sn = scriptNode() )
	{
		start = sn->frameStartPlug()->getValue();
		end = sn->frameEndPlug()->getValue();
	}
	return end - start;
}

double EffectImageInstance::getFrameRate() const
{
	if( auto ctx = Gaffer::Context::current() )
	{
		return ctx->getFramesPerSecond();
	}
	return 24.0;
}

double EffectImageInstance::getFrameRecursive() const
{
	if( auto ctx = Gaffer::Context::current() )
	{
		return ctx->getFrame();
	}
	return 1.0;
}

void EffectImageInstance::getRenderScaleRecursive(double &x, double &y) const
{
	x = y = 1.0;
}

OFX::Host::Param::Instance* EffectImageInstance::newParam(const std::string& name, OFX::Host::Param::Descriptor& descriptor)
{
	OFX::Host::Param::Instance *result = nullptr;

	if(descriptor.getType()==kOfxParamTypeInteger)
	{
		result = new IntegerInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeDouble)
	{
		result = new DoubleInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeBoolean)
	{
		result = new BooleanInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeChoice)
	{
		result = new ChoiceInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeRGBA)
	{
		result = new RGBAInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeRGB)
	{
		result = new RGBInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeDouble2D)
	{
		result = new Double2DInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeInteger2D)
	{
		result = new Integer2DInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypePushButton)
	{
		result = new PushbuttonInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeString)
	{
		result = new StringInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeGroup)
	{
		result = new OFX::Host::Param::GroupInstance(descriptor,this);
	}
	else if(descriptor.getType()==kOfxParamTypePage)
	{
		result = new OFX::Host::Param::PageInstance(descriptor,this);
	}
	else if(descriptor.getType()==kOfxParamTypeCustom)
	{
		result = new StringInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeParametric)
	{
		result = new ParametricInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeDouble3D)
	{
		result = new Double3DInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeInteger3D)
	{
		result = new Integer3DInstance(this,name,descriptor);
	}
	else
	{
		IECore::msg( IECore::Msg::Level::Warning, "GafferOFX", "Unhandled OFX parameter type \"" + descriptor.getType() + "\" for \"" + name + "\"" );
	}
	if( result )
	{
		OFX::Host::Param::registerInstance(result, &descriptor);

		// HostSupport passes the param map key; it must equal the
		// descriptor name that setup used for the plug.
		assert( name == descriptor.getName() );
		auto *plug = static_cast<OFXImageNode*>( node() )->parametersPlug()->getChild<Gaffer::Plug>( ParamAlgo::plugName( descriptor ) );
		if( plug )
		{
			ParamAlgo::registerParameterMetadata( plug, descriptor, &getDescriptor() );
		}
	}

	return result;
}

OfxStatus EffectImageInstance::editBegin(const std::string& name)
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus EffectImageInstance::editEnd()
{
	return kOfxStatErrMissingHostFeature;
}

void  EffectImageInstance::progressStart(const std::string &message, const std::string &messageid)
{
}

void  EffectImageInstance::progressEnd()
{
}

bool  EffectImageInstance::progressUpdate(double t)
{
	return true;
}


double  EffectImageInstance::timeLineGetTime()
{
	if( auto ctx = Gaffer::Context::current() )
	{
		return ctx->getFrame();
	}
	return 1.0;
}

void  EffectImageInstance::timeLineGotoTime(double t)
{
}

void  EffectImageInstance::timeLineGetBounds(double &t1, double &t2)
{

	t1 = 1;
	t2 = 100;
	if( auto sn = scriptNode() )
	{
		t1 = sn->frameStartPlug()->getValue();
		t2 = sn->frameEndPlug()->getValue();
	}
}

OFXImageNode* EffectImageInstance::node()
{
	return m_node;
}

const OFXImageNode* EffectImageInstance::node() const
{
	return m_node;
}

const Gaffer::ScriptNode* EffectImageInstance::scriptNode() const
{
	return m_node ? m_node->ancestor<Gaffer::ScriptNode>() : nullptr;
}

void EffectImageInstance::setNode(OFXImageNode* node)
{
	m_node = node;
	m_shutdown = false;
}

void EffectImageInstance::shutdown()
{
	if( m_shutdown || !m_node )
	{
		return;
	}
	m_shutdown = true;
	// Dispatch destroy while param instances are still registered
	// and back-pointer is valid (invariant 4). Prevent base
	// ~Instance (HostSupport/ofxhImageEffect.cpp:547, OFX_Release_1.5s)
	// from dispatching again (double free). Setting _created=false
	// before explicit mainEntry is safe: mainEntry does not guard
	// on _created, only the base dtor does. Regression: testAffects
	// -> testAlphaOnlyInput -> testBasicGainScale trio heap-corrupts
	// without this suppression (je_bitmap_set SIGSEGV).
	_created = false;
	try
	{
		// Base class dispatch will call the plugin's destroy action
		OFX::Host::ImageEffect::Instance::mainEntry( kOfxActionDestroyInstance, getHandle(), nullptr, nullptr );
	}
	catch( ... )
	{
	}
	// Unregister param instances before they are destroyed by ~SetInstance
	try
	{
		const auto &params = getParams();
		for( const auto &[name, inst] : params )
		{
			OFX::Host::Param::unregisterInstance( inst );
		}
	}
	catch( ... )
	{
	}
	m_node = nullptr;
}

void EffectImageInstance::markParamInteracted( const std::string &name )
{
	m_interactedParams.insert( name );
}

void EffectImageInstance::clearInteractedParams()
{
	m_interactedParams.clear();
}

const std::unordered_set<std::string> &EffectImageInstance::interactedParams() const
{
	return m_interactedParams;
}

OfxStatus EffectImageInstance::mainEntry(const char *action, const void *handle, OFX::Host::Property::Set *inArgs, OFX::Host::Property::Set *outArgs)
{
	typedef OFX::Host::ImageEffect::Instance BaseInstance;
	const std::string actionStr = action ? action : "";
	pushAction( actionStr );
	struct InstanceGuard
	{
		InstanceGuard( EffectImageInstance *i ) { g_instanceStack.push_back( i ); }
		~InstanceGuard() { if( !g_instanceStack.empty() ) { g_instanceStack.pop_back(); } }
	} instanceGuard( this );
	try
	{
		OfxStatus result = BaseInstance::mainEntry( action, handle, inArgs, outArgs );
		popAction();
		return result;
	}
	catch( const IECore::Exception &e )
	{
		popAction();
		IECore::msg( IECore::Msg::Error, "GafferOFX", std::string( "Action \"" ) + actionStr + "\" failed: " + e.what() );
		return kOfxStatFailed;
	}
	catch( const Gaffer::ProcessException &e )
	{
		popAction();
		IECore::msg( IECore::Msg::Error, "GafferOFX", std::string( "Action \"" ) + actionStr + "\" cancelled: " + e.what() );
		return kOfxStatFailed;
	}
	catch( const std::exception &e )
	{
		popAction();
		IECore::msg( IECore::Msg::Error, "GafferOFX", std::string( "Action \"" ) + actionStr + "\" failed: " + e.what() );
		return kOfxStatFailed;
	}
	catch( ... )
	{
		popAction();
		IECore::msg( IECore::Msg::Error, "GafferOFX", std::string( "Action \"" ) + actionStr + "\" failed with unknown exception" );
		return kOfxStatFailed;
	}
}

OFX::Host::ImageEffect::Image *EffectImageInstance::fetchInputImage(
	const ClipInstance &clip, OfxTime time, const OfxRectI &region
) const
{
	const GafferImage::ImagePlug *plug = nullptr;
	const std::string &clipName = clip.getName();

	if( clipName == "Source" )
	{
		plug = static_cast<const OFXImageNode*>( m_node )->inPlug();
	}
	else if( clipName != "Output" )
	{
		const std::string &plugName = clip.plugName();
		if( !plugName.empty() )
		{
			plug = m_node->getChild<GafferImage::ImagePlug>( plugName );
		}
	}

	if( !plug || !plug->getInput() )
	{
		return nullptr;
	}

	int width = region.x2 - region.x1;
	int height = region.y2 - region.y1;
	if( width <= 0 || height <= 0 )
	{
		return nullptr;
	}

	// Create output Image - allocates its own buffer
	OfxRectI bufBounds = { region.x1, region.y1, region.x2, region.y2 };
	Image *image = new Image( const_cast<ClipInstance&>( clip ), time, 0, &bufBounds );

	// Fill the image's pixel data by pulling from Gaffer under isolate
	// (prevents TBB task stealing during the pull).
	const bool isAlpha = ( image->getStringProperty( kOfxImageEffectPropComponents ) == kOfxImageComponentAlpha );
	OfxRGBAColourF *pixelData = isAlpha ? nullptr : image->pixel( region.x1, region.y1 );
	float *alphaData = isAlpha ? image->alphaPixel( region.x1, region.y1 ) : nullptr;

	auto *inv = currentInvocation();
	const Gaffer::Context *baseCtx = inv ? inv->context.get() : Gaffer::Context::current();

	if( baseCtx && ( pixelData || alphaData ) )
	{
		if( time == baseCtx->getFrame() )
		{
			Gaffer::Context::Scope scope( baseCtx );
			try
			{
				tbb::this_task_arena::isolate( [&]() {
					Imath::Box2i imgRegion(
						Imath::V2i( region.x1, region.y1 ),
						Imath::V2i( region.x2, region.y2 )
					);
					if( isAlpha )
					{
						readPlugToFloatA( plug, alphaData, imgRegion, width );
					}
					else
					{
						readPlugToRGBA( plug, pixelData, imgRegion, width );
					}
				} );
			}
			catch( ... )
			{
				if( auto *activeInv = currentInvocation() )
				{
					activeInv->exception = std::current_exception();
				}
				image->releaseReference();
				return nullptr;
			}
		}
		else
		{
			// Temporal access: override frame (EditableScope alone copies and scopes)
			Gaffer::Context::EditableScope edit( baseCtx );
			edit.setFrame( time );
			try
			{
				tbb::this_task_arena::isolate( [&]() {
					Imath::Box2i imgRegion(
						Imath::V2i( region.x1, region.y1 ),
						Imath::V2i( region.x2, region.y2 )
					);
					if( isAlpha )
					{
						readPlugToFloatA( plug, alphaData, imgRegion, width );
					}
					else
					{
						readPlugToRGBA( plug, pixelData, imgRegion, width );
					}
				} );
			}
			catch( ... )
			{
				if( auto *activeInv = currentInvocation() )
				{
					activeInv->exception = std::current_exception();
				}
				image->releaseReference();
				return nullptr;
			}
		}
	}

	return image;
}
