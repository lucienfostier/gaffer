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

#include "boost/python.hpp"

#include "GafferBindings/DependencyNodeBinding.h"

#include "GafferOFX/GLContextManager.h"
#include "GafferOFX/Host.h"
#include "GafferOFX/OFXImageNode.h"
#include "GafferOFX/OFXInteractInstance.h"

#include "IECorePython/RunTimeTypedBinding.h"
#include "IECorePython/ScopedGILRelease.h"

using namespace boost::python;
using namespace GafferBindings;
using namespace GafferOFX;

namespace
{

boost::python::list pluginIDsWrapper()
{
	boost::python::list result;
	for( const auto &id : Host::pluginIDs() )
	{
		result.append( id );
	}
	return result;
}

boost::python::dict pluginBundlesWrapper()
{
	boost::python::dict result;
	for( const auto &[id, bundle] : Host::pluginBundles() )
	{
		result[id] = bundle;
	}
	return result;
}

bool createPluginInstanceWrapper( OFXImageNode& node )
{
	IECorePython::ScopedGILRelease gilRelease;
	return node.createPluginInstance();

}

bool loadPluginWrapper( OFXImageNode& node, const std::string &pluginId, bool keepExistingValues )
{
	IECorePython::ScopedGILRelease gilRelease;
	return node.loadPlugin( pluginId, keepExistingValues );
}

// Test-only hooks for the action-gate unit test. Underscore-prefixed
// by convention; not public API. Used by testActionGate to drive
// EffectImageInstance::pushAction/popAction/allowParamSet without a
// live plugin dispatch.
void pushTestActionWrapper( const std::string &action )
{
	EffectImageInstance::pushAction( action );
}

void popTestActionWrapper()
{
	EffectImageInstance::popAction();
}

std::string currentTestActionWrapper()
{
	return EffectImageInstance::currentAction();
}

bool testAllowParamSetWrapper( OFXImageNode &node, const std::string &paramName )
{
	const EffectImageInstance *instance = node.effectInstance();
	if( !instance )
	{
		return false;
	}
	return const_cast<EffectImageInstance*>( instance )->allowParamSet( paramName );
}

OfxStatus callVMessage( const char *type, const char *id, const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	OfxStatus s = Host::instance().vmessage( type, id, fmt, args );
	va_end( args );
	return s;
}

OfxStatus callPersistentMessage( const char *type, const char *id, const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	OfxStatus s = Host::instance().setPersistentMessage( type, id, fmt, args );
	va_end( args );
	return s;
}

OfxStatus hostMessageWrapper( const std::string &type, const std::string &id, const std::string &message )
{
	return callVMessage( type.c_str(), id.c_str(), "%s", message.c_str() );
}

OfxStatus hostMessageFormattedWrapper( const std::string &type, const std::string &id, const std::string &fmt, const boost::python::object &arg )
{
	// Simple formatting test helper - supports one %s/%d substitution via python formatting already done,
	// so just forward fmt as message with arg string.
	// For true printf formatting, we test via hostMessage with pre-formatted python string.
	// This wrapper formats as "%s" + arg to verify va_list handling.
	std::string m = boost::python::extract<std::string>( boost::python::str( arg ) );
	return callVMessage( type.c_str(), id.c_str(), fmt.c_str(), m.c_str() );
}

OfxStatus hostPersistentMessageWrapper( const std::string &type, const std::string &id, const std::string &message )
{
	return callPersistentMessage( type.c_str(), id.c_str(), "%s", message.c_str() );
}

OfxStatus hostClearPersistentMessageWrapper()
{
	return Host::instance().clearPersistentMessage();
}

std::string hostPersistentMessageGetter()
{
	return Host::instance().persistentMessage();
}

std::pair<double, double> effectInstanceProjectSizeWrapper( OFXImageNode& node )
{
	if( !createPluginInstanceWrapper( node ) )
	{
		throw IECore::Exception( "Failed to create OFX plugin instance" );
	}
	double xsize, ysize;
	node.effectInstance()->getProjectSize(xsize, ysize);
	return std::make_pair(xsize, ysize);
}

// OFXInteractInstance wrappers

struct OfxPointD_to_tuple
{
	static PyObject* convert( const OfxPointD& p )
	{
		return incref( make_tuple( p.x, p.y ).ptr() );
	}
};

struct OfxPointI_to_tuple
{
	static PyObject* convert( const OfxPointI& p )
	{
		return incref( make_tuple( p.x, p.y ).ptr() );
	}
};

struct PairToTuple
{
	static PyObject* convert( const std::pair<double, double>& p )
	{
		return incref( make_tuple( p.first, p.second ).ptr() );
	}
};

namespace
{

OfxPointD pointDFromObject( const boost::python::object &o )
{
	OfxPointD r;
	r.x = boost::python::extract<double>( o[0] );
	r.y = boost::python::extract<double>( o[1] );
	return r;
}

OfxPointI pointIFromObject( const boost::python::object &o )
{
	OfxPointI r;
	r.x = boost::python::extract<int>( o[0] );
	r.y = boost::python::extract<int>( o[1] );
	return r;
}

}

OfxStatus interactDrawAction( GafferOFXInteractInstance &self, double time, const boost::python::object &renderScaleObj )
{
	OfxPointD renderScale = pointDFromObject( renderScaleObj );
	IECorePython::ScopedGILRelease gilRelease;
	return self.drawAction( time, renderScale );
}

void interactRenderOverlay( GafferOFXInteractInstance &self, double time, double renderScaleX, double renderScaleY, double pixelAspect, int imageWidth = 0, int imageHeight = 0 )
{
	IECorePython::ScopedGILRelease gilRelease;
	self.renderOverlay( time, renderScaleX, renderScaleY, pixelAspect, imageWidth, imageHeight );
}

OfxStatus interactPenMotionAction( GafferOFXInteractInstance &self, double time, const boost::python::object &renderScaleObj, const boost::python::object &penPosObj, const boost::python::object &penPosViewportObj, double pressure )
{
	OfxPointD renderScale = pointDFromObject( renderScaleObj );
	OfxPointD penPos = pointDFromObject( penPosObj );
	OfxPointI penPosViewport = pointIFromObject( penPosViewportObj );
	IECorePython::ScopedGILRelease gilRelease;
	return self.penMotionAction( time, renderScale, penPos, penPosViewport, pressure );
}

OfxStatus interactPenDownAction( GafferOFXInteractInstance &self, double time, const boost::python::object &renderScaleObj, const boost::python::object &penPosObj, const boost::python::object &penPosViewportObj, double pressure )
{
	OfxPointD renderScale = pointDFromObject( renderScaleObj );
	OfxPointD penPos = pointDFromObject( penPosObj );
	OfxPointI penPosViewport = pointIFromObject( penPosViewportObj );
	IECorePython::ScopedGILRelease gilRelease;
	return self.penDownAction( time, renderScale, penPos, penPosViewport, pressure );
}

OfxStatus interactPenUpAction( GafferOFXInteractInstance &self, double time, const boost::python::object &renderScaleObj, const boost::python::object &penPosObj, const boost::python::object &penPosViewportObj, double pressure )
{
	OfxPointD renderScale = pointDFromObject( renderScaleObj );
	OfxPointD penPos = pointDFromObject( penPosObj );
	OfxPointI penPosViewport = pointIFromObject( penPosViewportObj );
	IECorePython::ScopedGILRelease gilRelease;
	return self.penUpAction( time, renderScale, penPos, penPosViewport, pressure );
}

OfxStatus interactKeyDownAction( GafferOFXInteractInstance &self, double time, const boost::python::object &renderScaleObj, int key, std::string keyString )
{
	OfxPointD renderScale = pointDFromObject( renderScaleObj );
	char *ks = const_cast<char*>( keyString.c_str() );
	IECorePython::ScopedGILRelease gilRelease;
	return self.keyDownAction( time, renderScale, key, ks );
}

OfxStatus interactKeyUpAction( GafferOFXInteractInstance &self, double time, const boost::python::object &renderScaleObj, int key, std::string keyString )
{
	OfxPointD renderScale = pointDFromObject( renderScaleObj );
	char *ks = const_cast<char*>( keyString.c_str() );
	IECorePython::ScopedGILRelease gilRelease;
	return self.keyUpAction( time, renderScale, key, ks );
}

OfxStatus interactGainFocusAction( GafferOFXInteractInstance &self, double time, const boost::python::object &renderScaleObj )
{
	OfxPointD renderScale = pointDFromObject( renderScaleObj );
	IECorePython::ScopedGILRelease gilRelease;
	return self.gainFocusAction( time, renderScale );
}

OfxStatus interactLoseFocusAction( GafferOFXInteractInstance &self, double time, const boost::python::object &renderScaleObj )
{
	OfxPointD renderScale = pointDFromObject( renderScaleObj );
	IECorePython::ScopedGILRelease gilRelease;
	return self.loseFocusAction( time, renderScale );
}

void interactNotifyPluginEdited( GafferOFXInteractInstance &self )
{
	// Release GIL before calling into plugin code — some plugins
	// (e.g. Sapphire) spin on GL or spawn threads that may need
	// the GIL internally.  Holding the GIL during native plugin
	// dispatch can deadlock or crash the Python UI thread.
	IECorePython::ScopedGILRelease gilRelease;
	self.notifyPluginEdited();
}

} // anonymous namespace

BOOST_PYTHON_MODULE( _GafferOFX )
{
	to_python_converter<OfxPointD, OfxPointD_to_tuple>();
	to_python_converter<OfxPointI, OfxPointI_to_tuple>();
	to_python_converter<std::pair<double, double>, PairToTuple>();

	class_<Host>("Host", no_init)
		.def("findOFXPlugins", &Host::findOFXPlugins)
		.staticmethod("findOFXPlugins")
		.def("pluginIDs", &pluginIDsWrapper)
		.staticmethod("pluginIDs")
		.def("pluginBundles", &pluginBundlesWrapper)
		.staticmethod("pluginBundles")
		.def("message", &hostMessageWrapper)
		.staticmethod("message")
		.def("messageFormatted", &hostMessageFormattedWrapper)
		.staticmethod("messageFormatted")
		.def("setPersistentMessage", &hostPersistentMessageWrapper)
		.staticmethod("setPersistentMessage")
		.def("clearPersistentMessage", &hostClearPersistentMessageWrapper)
		.staticmethod("clearPersistentMessage")
		.def("persistentMessage", &hostPersistentMessageGetter)
		.staticmethod("persistentMessage")
	;

	class_<GLContextManager, boost::noncopyable>( "GLContextManager", no_init )
		.def( "instance", &GLContextManager::instance, return_value_policy<reference_existing_object>() )
		.staticmethod( "instance" )
		.def( "backendName", &GLContextManager::backendName )
		.def( "rendererString", &GLContextManager::rendererString )
		.def( "hardwareAvailable", &GLContextManager::hardwareAvailable )
	;

	class_<GafferOFXInteractInstance, boost::noncopyable>( "OFXInteractInstance", no_init )
		.def( "setViewportSize", &GafferOFXInteractInstance::setViewportSize )
		.def( "setDisplayWindowOrigin", &GafferOFXInteractInstance::setDisplayWindowOrigin )
		.def( "setTime", &GafferOFXInteractInstance::setTime )
		.def( "getTime", &GafferOFXInteractInstance::getTime )
		.def( "renderOverlay", &interactRenderOverlay )
		.def( "debugDraw", &GafferOFXInteractInstance::debugDraw )
		.def( "drawAction", &interactDrawAction )
		.def( "penMotionAction", &interactPenMotionAction )
		.def( "penDownAction", &interactPenDownAction )
		.def( "penUpAction", &interactPenUpAction )
		.def( "keyDownAction", &interactKeyDownAction )
		.def( "keyUpAction", &interactKeyUpAction )
		.def( "gainFocusAction", &interactGainFocusAction )
		.def( "loseFocusAction", &interactLoseFocusAction )
		.def( "notifyPluginEdited", &interactNotifyPluginEdited )
	;

	DependencyNodeClass<OFXImageNode>()
		.def( "createPluginInstance", &createPluginInstanceWrapper )
		.def( "loadPlugin", &loadPluginWrapper, ( arg( "pluginId" ), arg( "keepExistingValues" ) = true ) )
		.def( "effectInstanceProjectSize", &effectInstanceProjectSizeWrapper )
		.def( "hasOverlay", &OFXImageNode::hasOverlay )
		.def( "getInteract", &OFXImageNode::getInteract, return_value_policy<reference_existing_object>() )
		.def( "destroyInteract", &OFXImageNode::destroyInteract )
		.def( "rendering", &OFXImageNode::rendering )
		// Test-only; underscore = not public API (see wrappers above).
		.def( "_testAllowParamSet", &testAllowParamSetWrapper )
	;

	def( "_pushTestAction", &pushTestActionWrapper );
	def( "_popTestAction", &popTestActionWrapper );
	def( "_currentTestAction", &currentTestActionWrapper );
}
