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

#include "IECoreVDB/VDBObject.h"
#include "IECoreImage/OpenImageIOAlgo.h"

#include "IECore/MessageHandler.h"

#include "boost/bind/bind.hpp"

using namespace boost::placeholders;
using namespace Imath;
using namespace IECore;
using namespace IECoreScene;
using namespace IECoreVDB;
using namespace Gaffer;
using namespace GafferScene;
using namespace GafferOSL;
using namespace std;

GAFFER_NODE_DEFINE_TYPE( OSLVDB );

size_t OSLVDB::g_firstPlugIndex;

namespace
{

void updateGridFromSparse(
    openvdb::FloatGrid::Ptr grid,
    ConstV3fVectorDataPtr &coordData,
    ConstFloatVectorDataPtr &valueData
)
{
    auto &coords = coordData->readable();
    auto &values = valueData->readable();

    if (coords.size() != values.size())
    {
        throw std::runtime_error("Coordinates and values size mismatch");
    }

    auto accessor = grid->getAccessor();

    for (size_t i = 0; i < coords.size(); ++i)
    {
        const Imath::V3f &v = coords[i];
        openvdb::Coord c(
            static_cast<int>(v.x),
            static_cast<int>(v.y),
            static_cast<int>(v.z)
        );
        accessor.setValueOn(c, values[i]); // activate voxel and set value
    }
}

CompoundDataPtr prepareShadingPoints( const VDBObject *vdb, const ShadingEngine *shadingEngine )
{
	CompoundDataPtr shadingPoints = new CompoundData;
	for( const auto& gridName : vdb->gridNames() )
	{
        std::cout << "grid name: " << gridName << std::endl;
		if( shadingEngine->needsAttribute( gridName ) )
		{
            FloatVectorDataPtr gridData = new FloatVectorData;
            vector<float> &gridWritable = gridData->writable();

            V3fVectorDataPtr coordData = new V3fVectorData();
            std::vector<Imath::V3f> &coords = coordData->writable();

            std::cout << "prepare shading points : " << gridName  << std::endl;

            const auto& base = vdb->findGrid( gridName );
            openvdb::FloatGrid::ConstPtr grid = openvdb::gridConstPtrCast<openvdb::FloatGrid>(base);

            size_t activeVoxelCount = grid->activeVoxelCount();

            gridWritable.reserve( activeVoxelCount );
            std::cout << "found grid: " << gridName << " ptr: " << grid << std::endl;
            for (auto iter = grid->cbeginValueOn(); iter.test(); ++iter)
            {
                auto c = iter.getCoord();
                //std::cout << "coord : " << c << std::endl;

                coords.emplace_back(
                    static_cast<float>(c.x()), 
                    static_cast<float>(c.y()), 
                    static_cast<float>(c.z())
                );

                float val = *iter;
                //std::cout << "value : " << val << std::endl;
                gridWritable.push_back( val );
            }
            shadingPoints->writable()[gridName] = gridData;
            shadingPoints->writable()[gridName + "_coord"] = coordData;
		}
	}

    // dummy P
    V3fVectorDataPtr pData = new V3fVectorData;
    vector<V3f> &pWritable = pData->writable();

    pWritable.assign( 10000, V3f() );

    shadingPoints->writable()["P"] = pData;
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
	return getChild<Gaffer::Plug>( g_firstPlugIndex + 1 );
}

const Gaffer::Plug *OSLVDB::gridsPlug() const
{
	return getChild<Gaffer::Plug>( g_firstPlugIndex + 1 );
}

GafferOSL::OSLCode *OSLVDB::oslCode()
{
	return getChild<GafferOSL::OSLCode>( g_firstPlugIndex + 2 );
}

const GafferOSL::OSLCode *OSLVDB::oslCode() const
{
	return getChild<GafferOSL::OSLCode>( g_firstPlugIndex + 2 );
}

bool OSLVDB::affectsProcessedObject( const Gaffer::Plug *input ) const
{
	return
		Deformer::affectsProcessedObject( input ) ||
		input == shaderPlug()
	;
}

void OSLVDB::hashProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ConstShadingEnginePtr shadingEngine;
	if( auto shader = runTimeCast<const OSLShader>( shaderPlug()->source()->node() ) )
	{
		ScenePlug::GlobalScope globalScope( context );
		shadingEngine = shader->shadingEngine();
	}

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
	const VDBObject *inputVDB = runTimeCast<const VDBObject>( inputObject );
	if( !inputVDB )
	{
		return inputObject;
	}

	ConstShadingEnginePtr shadingEngine;
	if( auto shader = runTimeCast<const OSLShader>( shaderPlug()->source()->node() ) )
	{
		ScenePlug::GlobalScope globalScope( context );
		shadingEngine = shader->shadingEngine();
	}

	if( !shadingEngine )
	{
		return inputObject;
	}

	CompoundDataPtr shadingPoints = prepareShadingPoints( inputVDB, shadingEngine.get() );

	VDBObjectPtr outputVDB = inputVDB->copy();

	ShadingEngine::Transforms transforms;

    transforms[ g_world ] = ShadingEngine::Transform( Imath::M44f(), Imath::M44f() );

	CompoundDataPtr shadedPoints = shadingEngine->shade( shadingPoints.get(), transforms );
    auto it = shadedPoints->readable().find("density_coord");
    if (it != shadedPoints->readable().end())
    {
        std::cout << it->second.get() << std::endl;
    }
	for( CompoundDataMap::const_iterator it = shadedPoints->readable().begin(), eIt = shadedPoints->readable().end(); it != eIt; ++it )
	{

		// Ignore the output color closure as the debug closures are used to define what is 'exported' from the shader
		if( it->first != "Ci" )
		{
            std::cout << "grid to transfer " << it->first << std::endl;
            std::cout << "grid ptr: " << it->second << " typename : " << it->second->typeName() << std::endl;
            if( auto floatData = runTimeCast<const FloatVectorData>( it->second ) )
            {
                openvdb::FloatGrid::Ptr newGrid;
                const auto& base = inputVDB->findGrid( it->first );
                if ( base )
                {
                    openvdb::FloatGrid::ConstPtr grid = openvdb::gridConstPtrCast<openvdb::FloatGrid>(base);
                    newGrid = grid->deepCopy();
                }
                else
                {
                    newGrid = openvdb::FloatGrid::create();
                }

                IECore::ConstV3fVectorDataPtr coordData = IECore::runTimeCast<const IECore::V3fVectorData>(shadedPoints->readable().at(it->first.string() + "_coord"));
                updateGridFromSparse( newGrid, coordData, floatData );
                for ( const auto& v : floatData->readable() )
                {
                    std::cout << "shaded value : " << v << std::endl;
                }
                outputVDB->removeGrid(it->first.string());
                outputVDB->insertGrid( newGrid );


                auto result = openvdb::tools::minMax(newGrid->tree());
                
                float minVal = result.min();
                float maxVal = result.max();
                
                std::cout << "Grid min: " << minVal << " max: " << maxVal << std::endl;
            }
		}
	}

	return outputVDB;
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
