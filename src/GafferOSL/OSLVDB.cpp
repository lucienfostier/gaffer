/////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2013, John Haddon. All rights reserved.
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

#include "GafferOSL/OSLVDB.h"

#include "GafferOSL/ClosurePlug.h"
#include "GafferOSL/OSLCode.h"
#include "GafferOSL/ShadingEngine.h"

#include "GafferScene/ResamplePrimitiveVariables.h"

#include "Gaffer/Metadata.h"
#include "Gaffer/UndoScope.h"
#include "Gaffer/ScriptNode.h"
#include "Gaffer/NameValuePlug.h"

#include "IECoreScene/Primitive.h"
#include "IECoreImage/OpenImageIOAlgo.h"

#include "IECore/MessageHandler.h"

#include "boost/bind/bind.hpp"

using namespace boost::placeholders;
using namespace Imath;
using namespace IECore;
using namespace IECoreScene;
using namespace Gaffer;
using namespace GafferScene;
using namespace GafferOSL;

GAFFER_NODE_DEFINE_TYPE( OSLVDB );

size_t OSLVDB::g_firstPlugIndex;

namespace
{

CompoundDataPtr prepareShadingPoints( const Primitive *primitive, const ShadingEngine *shadingEngine, const CompoundObject *gafferAttributes = nullptr )
{
	CompoundDataPtr shadingPoints = new CompoundData;
	for( PrimitiveVariableMap::const_iterator it = primitive->variables.begin(), eIt = primitive->variables.end(); it != eIt; ++it )
	{
		// todo: consider passing something like IndexedView to the ShadingEngine to avoid the expansion of indexed data.
		if( shadingEngine->needsAttribute( it->first ) )
		{
			if( it->second.indices )
			{
				shadingPoints->writable()[it->first] = it->second.expandedData();
			}
			else
			{
				shadingPoints->writable()[it->first] = boost::const_pointer_cast<Data>( it->second.data );
			}
		}
	}

	if( gafferAttributes )
	{
		for( const auto &i : gafferAttributes->members() )
		{
			if( shadingEngine->needsAttribute( i.first ) )
			{
				if( shadingPoints->writable().find( i.first ) == shadingPoints->writable().end() )
				{
					const IECore::Data* data = IECore::runTimeCast< IECore::Data >( i.second.get() );

					// We currently don't support array attributes
					// ( because ShadingEngine assumes that all arrays contain per-shading-point
					// data of the appropriate length. )
					// Using OpenImageIOAlgo to check if it's an array feels a bit weird, but it
					// seems important to exactly match the logic of GafferOSL::ShadingEngine
					if( data && !IECoreImage::OpenImageIOAlgo::DataView( data ).type.arraylen )
					{
						const IECore::BoolData* boolData = IECore::runTimeCast< const IECore::BoolData >( data );
						if( boolData )
						{
							shadingPoints->writable()[i.first] = new IECore::IntData( boolData->readable() );
						}
						else
						{
							// Const cast is safe because the resulting dict is const
							shadingPoints->writable()[i.first] = const_cast< IECore::Data* >( data );
						}
					}
					else
					{
						// If we hit this branch, it means either that the shader needs to read an attribute
						// which is invalid, in which case it would be nice to throw an error ... or it means
						// that OSL couldn't determine which attributes the shader needs, and we're trying to
						// pass it everything.  Because we can't tell which case we're in here, we can't throw
						// an error, and we just silently don't pass this attribute
					}
				}
			}
		}
	}

	return shadingPoints;
}

} // namespace

OSLVDB::OSLVDB( const std::string &name )
	:	Deformer( name )
{
	storeIndexOfNextChild( g_firstPlugIndex );
	addChild( new GafferScene::ShaderPlug( "__shader", Plug::In, Plug::Default & ~Plug::Serialisable ) );
	addChild( new Plug( "grids", Plug::In, Plug::Default & ~Plug::AcceptsInputs ) );
	addChild( new OSLCode( "__oslCode" ) );
	shaderPlug()->setInput( oslCode()->outPlug() );

	gridsPlug()->childAddedSignal().connect( boost::bind( &OSLVDB::gridAdded, this, ::_1, ::_2 ) );
	gridsPlug()->childRemovedSignal().connect( boost::bind( &OSLVDB::gridRemoved, this, ::_1, ::_2 ) );
}

OSLVDB::~OSLVDB()
{
}

GafferScene::ShaderPlug *OSLVDB::shaderPlug()
{
	return getChild<GafferScene::ShaderPlug>( g_firstPlugIndex );
}

const GafferScene::ShaderPlug *OSLVDB::shaderPlug() const
{
	return getChild<GafferScene::ShaderPlug>( g_firstPlugIndex );
}

Gaffer::Plug *OSLVDB::gridsPlug()
{
	return getChild<Gaffer::Plug>( g_firstPlugIndex + 6 );
}

const Gaffer::Plug *OSLVDB::gridsPlug() const
{
	return getChild<Gaffer::Plug>( g_firstPlugIndex + 6 );
}

GafferOSL::OSLCode *OSLVDB::oslCode()
{
	return getChild<GafferOSL::OSLCode>( g_firstPlugIndex + 7 );
}

const GafferOSL::OSLCode *OSLVDB::oslCode() const
{
	return getChild<GafferOSL::OSLCode>( g_firstPlugIndex + 7 );
}

bool OSLVDB::affectsProcessedObject( const Gaffer::Plug *input ) const
{
	return
		Deformer::affectsProcessedObject( input ) ||
		input == shaderPlug() ||
	;
}

void OSLVDB::hashProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ConstShadingEnginePtr shadingEngine = this->shadingEngine( context, gafferAttributes.get() );
	if( !shadingEngine )
	{
		h = inPlug()->objectPlug()->hash();
		return;
	}

	Deformer::hashProcessedObject( path, context, h );

	shadingEngine->hash( h );
}

static const IECore::InternedString g_world("world");

IECore::ConstObjectPtr OSLVDB::computeProcessedObject( const ScenePath &path, const Gaffer::Context *context, const IECore::Object *inputObject ) const
{
	const Primitive *inputPrimitive = runTimeCast<const Primitive>( inputObject );
	if( !inputPrimitive )
	{
		return inputObject;
	}

	ConstShadingEnginePtr shadingEngine = this->shadingEngine( context, gafferAttributes.get() );
	if( !shadingEngine )
	{
		return inputObject;
	}

	CompoundDataPtr shadingPoints = prepareShadingPoints( resampledObject.get(), shadingEngine.get() );

	PrimitivePtr outputPrimitive = inputPrimitive->copy();

	ShadingEngine::Transforms transforms;

    transforms[ g_world ] = ShadingEngine::Transform( Imath::M44f(), Imath::M44f() );

	CompoundDataPtr shadedPoints = shadingEngine->shade( shadingPoints.get(), transforms );
	for( CompoundDataMap::const_iterator it = shadedPoints->readable().begin(), eIt = shadedPoints->readable().end(); it != eIt; ++it )
	{

		// Ignore the output color closure as the debug closures are used to define what is 'exported' from the shader
		if( it->first != "Ci" )
		{
			outputPrimitive->variables[it->first] = PrimitiveVariable( interpolation, it->second );
		}
	}

	return outputPrimitive;
}

Gaffer::ValuePlug::CachePolicy OSLVDB::processedObjectComputeCachePolicy() const
{
	return ValuePlug::CachePolicy::TaskCollaboration;
}

bool OSLVDB::adjustBounds() const
{
	if( !Deformer::adjustBounds() )
	{
		return false;
	}

	// \todo - this is technically wrong, whether a deformation closure is output may depend
	// on the substitutions.
	//
	// To solve this properly, we could do something like: ShadingEngine declares a special
	// token STRING_PARAMETER_VALUE_UNKNOWN. Evaluating shade() on a ShadingEngine containing
	// these tokens throws an exception, but calls to hasDeformation or needsAttribute
	// would yield correct ( or at least conservative ) results, by using lockgeom=false.
	//
	// The getter for ShadingEngines inside OSLShader can then be modified to replace strings
	// that require substitutions with this token when a special flag is passed somehow.
	//
	// Uses of the shadingEngine in OSLVDB that don't use shade() and want global results
	// without considering the specific location ( adjustBounds and resampledNamesPlug )
	// would then need to pass this special flag.
	//
	// See conversation here https://github.com/GafferHQ/gaffer/pull/5039#discussion_r1063481066
	// where this approach was proposed, and we decided not to implement it yet.

	ConstShadingEnginePtr s = shadingEngine( Context::current(), nullptr );
	return s && s->hasDeformation();
}

ConstShadingEnginePtr OSLVDB::shadingEngine( const Gaffer::Context *context, const CompoundObject *substitutions ) const
{
	auto shader = runTimeCast<const OSLShader>( shaderPlug()->source()->node() );
	if( !shader )
	{
		return nullptr;
	}

	ScenePlug::GlobalScope globalScope( context );

	static ConstCompoundObjectPtr defaultSubstitutions = new CompoundObject();

	return shader->shadingEngine( substitutions ? substitutions : defaultSubstitutions.get() );
}

void OSLVDB::updatePrimitiveVariables()
{
	// Disable undo for the actions we perform, because anything that can
	// trigger an update is undoable itself, and we will take care of everything as a whole
	// when we are undone.
	UndoScope undoDisabler( scriptNode(), UndoScope::Disabled );

	// Currently the OSLCode node will recompile every time an input is added.
	// We're hoping in the future to avoid doing this until the network is actually needed,
	// but in the meantime, we can save some time by emptying the code first, so that at least
	// all the redundant recompiles are of shorter code.
	oslCode()->codePlug()->setValue( "" );

	oslCode()->parametersPlug()->clearChildren();

	std::string code = "closure color out = 0;\n";

	for( NameValuePlug::Iterator inputPlug( gridsPlug() ); !inputPlug.done(); ++inputPlug )
	{
		std::string prefix = "";
		BoolPlug* enabledPlug = (*inputPlug)->enabledPlug();
		if( enabledPlug )
		{
			IntPlugPtr codeEnablePlug = new IntPlug( "enable" );
			oslCode()->parametersPlug()->addChild( codeEnablePlug );
			codeEnablePlug->setInput( enabledPlug );
			prefix = "if( " + codeEnablePlug->getName().string() + " ) ";
		}

		Plug *valuePlug = (*inputPlug)->valuePlug();

		if( valuePlug->typeId() == ClosurePlug::staticTypeId() )
		{
			// Closures are a special case that doesn't need a wrapper function
			ClosurePlugPtr codeClosurePlug = new ClosurePlug( "closureIn" );
			oslCode()->parametersPlug()->addChild( codeClosurePlug );
			codeClosurePlug->setInput( valuePlug );

			code += prefix + "out = out + " + codeClosurePlug->getName().string() + ";\n";
			continue;
		}

		std::string outFunction;
		PlugPtr codeValuePlug;
		const Gaffer::TypeId valueType = (Gaffer::TypeId)valuePlug->typeId();
		switch( (int)valueType )
		{
			case FloatPlugTypeId :
				codeValuePlug = new FloatPlug( "value" );
				outFunction = "outFloat";
				break;
			case IntPlugTypeId :
				codeValuePlug = new IntPlug( "value" );
				outFunction = "outInt";
				break;
			case Color3fPlugTypeId :
				codeValuePlug = new Color3fPlug( "value" );
				outFunction = "outColor";
				break;
			case V3fPlugTypeId :
				codeValuePlug = new V3fPlug( "value" );
				{
					V3fPlug *v3fPlug = runTimeCast<V3fPlug>( valuePlug );
					if( v3fPlug->interpretation() == GeometricData::Point )
					{
						outFunction = "outPoint";
					}
					else if( v3fPlug->interpretation() == GeometricData::Normal )
					{
						outFunction = "outNormal";
					}
					else if( v3fPlug->interpretation() == GeometricData::UV )
					{
						outFunction = "outUV";
					}
					else
					{
						outFunction = "outVector";
					}
				}
				break;
			case M44fPlugTypeId :
				codeValuePlug = new M44fPlug( "value" );
				outFunction = "outMatrix";
				break;
			case StringPlugTypeId :
				codeValuePlug = new StringPlug( "value" );
				outFunction = "outString";
				break;
		}

		if( codeValuePlug )
		{

			StringPlugPtr codeNamePlug = new StringPlug( "name" );
			oslCode()->parametersPlug()->addChild( codeNamePlug );
			codeNamePlug->setInput( (*inputPlug)->namePlug() );

			oslCode()->parametersPlug()->addChild( codeValuePlug );
			codeValuePlug->setInput( valuePlug );

			code += prefix + "out = out + " + outFunction + "( " + codeNamePlug->getName().string() + ", "
				+ codeValuePlug->getName().string() + ");\n";
			continue;
		}

		IECore::msg( IECore::Msg::Warning, "OSLVDB::updatePrimitiveVariables",
			"Could not create primitive variable from plug: " + (*inputPlug)->fullName()
		);
	}
	code += "Ci = out;\n";

	oslCode()->codePlug()->setValue( code );
}

void OSLVDB::gridAdded( const Gaffer::GraphComponent *parent, Gaffer::GraphComponent *child )
{
	updatePrimitiveVariables();
}

void OSLVDB::gridRemoved( const Gaffer::GraphComponent *parent, Gaffer::GraphComponent *child )
{
	updatePrimitiveVariables();
}
