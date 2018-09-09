//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2018, Image Engine. All rights reserved.
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
//      * Neither the name of Image Engine nor the names of
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

#include "GafferVDB/LevelSetMeasure.h"

#include "IECore/StringAlgo.h"

#include "IECoreScene/PointsPrimitive.h"

#include "IECoreVDB/VDBObject.h"

#include "Gaffer/StringPlug.h"
#include "GafferScene/ScenePlug.h"

#include "openvdb/openvdb.h"
#include "openvdb/tools/LevelSetMeasure.h"


using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreVDB;
using namespace Gaffer;
using namespace GafferScene;
using namespace GafferVDB;

IE_CORE_DEFINERUNTIMETYPED( LevelSetMeasure );

size_t LevelSetMeasure::g_firstPlugIndex = 0;

LevelSetMeasure::LevelSetMeasure( const std::string &name )
		: SceneElementProcessor( name, IECore::PathMatcher::NoMatch )
{
	storeIndexOfNextChild( g_firstPlugIndex );

	addChild ( new StringPlug( "grids", Plug::In, "*" ) );

	outPlug()->boundPlug()->setInput( inPlug()->boundPlug() );
	outPlug()->objectPlug()->setInput( inPlug()->objectPlug() );
}

LevelSetMeasure::~LevelSetMeasure()
{
}

Gaffer::StringPlug *LevelSetMeasure::gridsPlug()
{
	return getChild<StringPlug>( g_firstPlugIndex );
}

const Gaffer::StringPlug *LevelSetMeasure::gridsPlug() const
{
	return getChild<const StringPlug>( g_firstPlugIndex );
}

void LevelSetMeasure::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	SceneElementProcessor::affects( input, outputs );

	if ( input == gridsPlug()  )
	{
		outputs.push_back( outPlug()->attributesPlug() );
	}
}

bool LevelSetMeasure::processesAttributes() const
{
	return true;
}

void LevelSetMeasure::hashProcessedAttributes( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	SceneElementProcessor::hashProcessedAttributes( path, context, h );

	h.append( inPlug()->objectHash( path ) );
	h.append( gridsPlug()->getValue() );
}

IECore::ConstCompoundObjectPtr LevelSetMeasure::computeProcessedAttributes( const ScenePath &path, const Gaffer::Context *context, IECore::ConstCompoundObjectPtr inputAttributes ) const
{

	auto vdbObject = IECore::runTimeCast<const IECoreVDB::VDBObject> ( inPlug()->object( path ) );

	if ( !vdbObject )
	{
		return inputAttributes;
	}

	std::vector<std::string> gridNames = vdbObject->gridNames();
	std::string grids = gridsPlug()->getValue();

	IECore::CompoundObjectPtr newAttributes = inputAttributes->copy();

	for (const auto &gridName : gridNames )
	{
		if (IECore::StringAlgo::matchMultiple(gridName, grids))
		{
			openvdb::GridBase::ConstPtr srcGrid = vdbObject->findGrid( gridName );

			openvdb::FloatGrid::ConstPtr grid = openvdb::GridBase::constGrid<openvdb::FloatGrid>( srcGrid );

			if ( !grid )
			{
				continue;
			}

			openvdb::tools::LevelSetMeasure<openvdb::FloatGrid> measure ( *grid );

			//void measure(Real& area, Real& volume, Real& avgMeanCurvature, bool useWorldUnits = true);
			openvdb::Real area, volume, averageMeanCurvature;
			measure.measure( area, volume, averageMeanCurvature);


			newAttributes->members().insert(std::make_pair( std::string("levelset:") + gridName + ":area", new IECore::FloatData( area) ) );
			newAttributes->members().insert(std::make_pair( std::string("levelset:") + gridName + ":volume", new IECore::FloatData( volume ) ) );
			newAttributes->members().insert(std::make_pair( std::string("levelset:") + gridName + ":averageMeanCurvature", new IECore::FloatData( averageMeanCurvature ) ) );

		}
	}

	return newAttributes;
}


