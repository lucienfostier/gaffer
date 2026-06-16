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

#include "GafferOFX/Host.h"
#include "GafferOFX/OFXImageNode.h"
#include "GafferOFX/OFXInteractInstance.h"

#include "IECorePython/RunTimeTypedBinding.h"

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

struct PairToTuple
{
    static PyObject* convert(const std::pair<double, double>& p)
	{
        return boost::python::incref(
            boost::python::make_tuple(p.first, p.second).ptr()
        );
    }
};

bool createPluginInstanceWrapper( OFXImageNode& node )
{
	IECorePython::ScopedGILRelease gilRelease;
	return node.createPluginInstance();

}

std::pair<double, double> effectInstanceProjectSizeWrapper( OFXImageNode& node )
{
	node.createPluginInstance();
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

OfxStatus interactDrawAction( GafferOFXInteractInstance &self, double time, const OfxPointD &renderScale )
{
	return self.drawAction( time, renderScale );
}

OfxStatus interactPenMotionAction( GafferOFXInteractInstance &self, double time, const OfxPointD &renderScale, const OfxPointD &penPos, const OfxPointI &penPosViewport, double pressure )
{
	return self.penMotionAction( time, renderScale, penPos, penPosViewport, pressure );
}

OfxStatus interactPenDownAction( GafferOFXInteractInstance &self, double time, const OfxPointD &renderScale, const OfxPointD &penPos, const OfxPointI &penPosViewport, double pressure )
{
	return self.penDownAction( time, renderScale, penPos, penPosViewport, pressure );
}

OfxStatus interactPenUpAction( GafferOFXInteractInstance &self, double time, const OfxPointD &renderScale, const OfxPointD &penPos, const OfxPointI &penPosViewport, double pressure )
{
	return self.penUpAction( time, renderScale, penPos, penPosViewport, pressure );
}

OfxStatus interactKeyDownAction( GafferOFXInteractInstance &self, double time, const OfxPointD &renderScale, int key, std::string keyString )
{
	char *ks = const_cast<char*>( keyString.c_str() );
	return self.keyDownAction( time, renderScale, key, ks );
}

OfxStatus interactKeyUpAction( GafferOFXInteractInstance &self, double time, const OfxPointD &renderScale, int key, std::string keyString )
{
	char *ks = const_cast<char*>( keyString.c_str() );
	return self.keyUpAction( time, renderScale, key, ks );
}

OfxStatus interactGainFocusAction( GafferOFXInteractInstance &self, double time, const OfxPointD &renderScale )
{
	return self.gainFocusAction( time, renderScale );
}

OfxStatus interactLoseFocusAction( GafferOFXInteractInstance &self, double time, const OfxPointD &renderScale )
{
	return self.loseFocusAction( time, renderScale );
}

struct OfxPointI_to_tuple
{
	static PyObject* convert( const OfxPointI& p )
	{
		return incref( make_tuple( p.x, p.y ).ptr() );
	}
};

} // anonymous namespace

BOOST_PYTHON_MODULE( _GafferOFX )
{
	to_python_converter<std::pair<double, double>, PairToTuple>();
	to_python_converter<OfxPointD, OfxPointD_to_tuple>();
	to_python_converter<OfxPointI, OfxPointI_to_tuple>();

	class_<Host>("Host", no_init)
		.def("findOFXPlugins", &Host::findOFXPlugins)
		.staticmethod("findOFXPlugins")
		.def("pluginIDs", &pluginIDsWrapper)
		.staticmethod("pluginIDs")
	;

	class_<GafferOFXInteractInstance, boost::noncopyable>( "OFXInteractInstance", no_init )
		.def( "setViewportSize", &GafferOFXInteractInstance::setViewportSize )
		.def( "setTime", &GafferOFXInteractInstance::setTime )
		.def( "getTime", &GafferOFXInteractInstance::getTime )
		.def( "setupGLProjection", &GafferOFXInteractInstance::setupGLProjection )
		.def( "restoreGLProjection", &GafferOFXInteractInstance::restoreGLProjection )
		.def( "drawAction", &interactDrawAction )
		.def( "penMotionAction", &interactPenMotionAction )
		.def( "penDownAction", &interactPenDownAction )
		.def( "penUpAction", &interactPenUpAction )
		.def( "keyDownAction", &interactKeyDownAction )
		.def( "keyUpAction", &interactKeyUpAction )
		.def( "gainFocusAction", &interactGainFocusAction )
		.def( "loseFocusAction", &interactLoseFocusAction )
	;

	DependencyNodeClass<OFXImageNode>()
		.def( "createPluginInstance", &createPluginInstanceWrapper )
		.def( "effectInstanceProjectSize", &effectInstanceProjectSizeWrapper )
		.def( "hasOverlay", &OFXImageNode::hasOverlay )
		.def( "getInteract", &OFXImageNode::getInteract, return_value_policy<reference_existing_object>() )
		.def( "destroyInteract", &OFXImageNode::destroyInteract )
	;
}
