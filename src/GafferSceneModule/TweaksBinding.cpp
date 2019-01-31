//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2016, Image Engine Design Inc. All rights reserved.
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

#include "TweaksBinding.h"

#include "GafferScene/ShaderTweaks.h"
#include "GafferScene/CameraTweaks.h"
#include "GafferScene/TweakPlug.h"

#include "GafferBindings/DependencyNodeBinding.h"
#include "GafferBindings/PlugBinding.h"

using namespace boost::python;
using namespace Gaffer;
using namespace GafferBindings;
using namespace GafferScene;

namespace
{

TweakPlugPtr constructUsingData( const std::string &tweakName, IECore::ConstDataPtr tweakValue, TweakPlug::Mode mode, bool enabled )
{
	return new TweakPlug( tweakName, tweakValue.get(), mode, enabled );
}

void applyTweak( const TweakPlug &plug, IECore::CompoundData &parameters, bool requireExists )
{
	IECorePython::ScopedGILRelease gilRelease;
	plug.applyTweak( &parameters, requireExists );
}

void applyTweaks( const Plug &tweaksPlug, IECoreScene::ShaderNetwork &shaderNetwork )
{
	IECorePython::ScopedGILRelease gilRelease;
	TweakPlug::applyTweaks( &tweaksPlug, &shaderNetwork );
}

void applyTweaksToParameters( const TweaksPlug &tweaksPlug, IECore::CompoundData &parameters, bool requireExists )
{
	IECorePython::ScopedGILRelease gilRelease;
	tweaksPlug.applyTweaks( &parameters, requireExists );
}

} // namespace

void GafferSceneModule::bindTweaks()
{
	DependencyNodeClass<ShaderTweaks>();
	DependencyNodeClass<CameraTweaks>();

	PlugClass<TweakPlug> tweakPlugClass;

	{
		scope tweakPlugScope = tweakPlugClass;
		enum_<TweakPlug::Mode>( "Mode" )
			.value( "Replace", TweakPlug::Replace )
			.value( "Add", TweakPlug::Add )
			.value( "Subtract", TweakPlug::Subtract )
			.value( "Multiply", TweakPlug::Multiply )
			.value( "Remove", TweakPlug::Remove )
		;
	}

	tweakPlugClass
		.def(
			init<const char *, Plug::Direction, unsigned>(
				(
					boost::python::arg_( "name" )=GraphComponent::defaultName<TweakPlug>(),
					boost::python::arg_( "direction" )=Plug::In,
					boost::python::arg_( "flags" )=Plug::Default
				)
			)
		)
		.def(
			"__init__",
			make_constructor(
				constructUsingData,
				default_call_policies(),
				(
					boost::python::arg_( "tweakName" ),
					boost::python::arg_( "valuePlug" ),
					arg( "mode" ) = TweakPlug::Replace,
					boost::python::arg_( "enabled" )=true
				)
			)
		)
		.def(
			init<const std::string &, const ValuePlugPtr, TweakPlug::Mode, bool>(
				(
					boost::python::arg_( "tweakName" ),
					boost::python::arg_( "value" ),
					arg( "mode" ) = TweakPlug::Replace,
					boost::python::arg_( "enabled" )=true
				)
			)
		)
		.def( "applyTweak", &applyTweak, ( arg( "parameters" ), arg( "requireExists" ) = false ) )
		.def( "applyTweaks", &applyTweaks, ( arg( "shaderNetwork" ) ) )
		.staticmethod( "applyTweaks" )
	;

	PlugClass<TweaksPlug>()
		.def(
			init<const std::string &, Plug::Direction, unsigned>(
				(
					boost::python::arg_( "name" )=GraphComponent::defaultName<TweaksPlug>(),
					boost::python::arg_( "direction" )=Plug::In,
					boost::python::arg_( "flags" )=Plug::Default
				)
			)
		)
		.def( "applyTweaks", &applyTweaks, ( arg( "shaderNetwork" ) ) )
		.def( "applyTweaks", &applyTweaksToParameters, ( arg( "parameters" ), arg( "requireExists" ) = false ) )
	;

}
