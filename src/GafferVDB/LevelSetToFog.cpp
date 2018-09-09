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

#include "GafferVDB/LevelSetToFog.h"

#include "IECore/StringAlgo.h"

#include "IECoreScene/PointsPrimitive.h"

#include "IECoreVDB/VDBObject.h"

#include "Gaffer/StringPlug.h"

#include "openvdb/openvdb.h"
#include "openvdb/tools/LevelSetUtil.h"


using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreVDB;
using namespace Gaffer;
using namespace GafferVDB;

IE_CORE_DEFINERUNTIMETYPED( LevelSetToFog );

size_t LevelSetToFog::g_firstPlugIndex = 0;

LevelSetToFog::LevelSetToFog( const std::string &name )
		: SceneElementProcessor( name, IECore::PathMatcher::NoMatch )
{
	storeIndexOfNextChild(g_firstPlugIndex);

	addChild( new StringPlug( "grids", Plug::In, "surface" ) );
}

LevelSetToFog::~LevelSetToFog()
{
}


Gaffer::StringPlug *LevelSetToFog::gridsPlug()
{
	return  getChild<StringPlug>( g_firstPlugIndex );
}

const Gaffer::StringPlug *LevelSetToFog::gridsPlug() const
{
	return  getChild<StringPlug>( g_firstPlugIndex );
}



void LevelSetToFog::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	SceneElementProcessor::affects( input, outputs );

	if( input == gridsPlug() )
	{
		outputs.push_back( outPlug()->objectPlug() );
		outputs.push_back( outPlug()->boundPlug() );
	}
}

bool LevelSetToFog::processesObject() const
{
	return true;
}

void LevelSetToFog::hashProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	SceneElementProcessor::hashProcessedObject( path, context, h );

	h.append( gridsPlug()->hash() );
}

IECore::ConstObjectPtr LevelSetToFog::computeProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::ConstObjectPtr inputObject ) const
{
	const IECoreVDB::VDBObject *vdbObject = runTimeCast<const IECoreVDB::VDBObject>(inputObject.get());
	if( !vdbObject )
	{
		return inputObject;
	}

	std::vector<std::string> grids = vdbObject->gridNames();

	std::string gridsToProcess = gridsPlug()->getValue();


	IECoreVDB::VDBObjectPtr newVDBObject = vdbObject->copy();

	for (const auto &gridName : grids )
	{
		if (IECore::StringAlgo::matchMultiple(gridName, gridsToProcess))
		{
			openvdb::GridBase::ConstPtr grid = vdbObject->findGrid( gridName );
			openvdb::FloatGrid::ConstPtr floatGrid = openvdb::GridBase::constGrid<openvdb::FloatGrid>( grid );

			if ( floatGrid )
			{
				openvdb::FloatGrid::Ptr fogGrid = floatGrid->deepCopy();

				openvdb::tools::sdfToFogVolume( *fogGrid );

				newVDBObject->insertGrid( fogGrid );
			}
		}
	}

	return newVDBObject;
}

bool LevelSetToFog::processesBound() const
{
	return true;
}

void LevelSetToFog::hashProcessedBound( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	SceneElementProcessor::hashProcessedBound( path, context, h );

	hashProcessedObject( path, context, h);
}

Imath::Box3f LevelSetToFog::computeProcessedBound( const ScenePath &path, const Gaffer::Context *context, const Imath::Box3f &inputBound ) const
{
	// todo calculate bounds from vdb grids
	return inputBound;
}

