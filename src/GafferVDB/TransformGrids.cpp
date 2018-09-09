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

#include "GafferVDB/TransformGrids.h"

#include "IECore/StringAlgo.h"

#include "IECoreScene/PointsPrimitive.h"

#include "IECoreVDB/VDBObject.h"

#include "Gaffer/CompoundNumericPlug.h"
#include "Gaffer/StringPlug.h"

#include "openvdb/openvdb.h"
#include "openvdb/tools/GridTransformer.h"

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreVDB;
using namespace Gaffer;
using namespace GafferVDB;
using namespace GafferScene;

IE_CORE_DEFINERUNTIMETYPED( TransformGrids );

size_t TransformGrids::g_firstPlugIndex = 0;

TransformGrids::TransformGrids( const std::string &name )
		: SceneElementProcessor( name, IECore::PathMatcher::NoMatch )
{
	storeIndexOfNextChild(g_firstPlugIndex);

	addChild( new StringPlug( "grids", Plug::In, "*" ) );
	addChild( new TransformPlug( "transform", Plug::In, 0.0 ) );
}

TransformGrids::~TransformGrids()
{
}

Gaffer::StringPlug *TransformGrids::gridsPlug()
{
	return  getChild<StringPlug>( g_firstPlugIndex );
}

const Gaffer::StringPlug *TransformGrids::gridsPlug() const
{
	return  getChild<StringPlug>( g_firstPlugIndex  );
}

Gaffer::TransformPlug *TransformGrids::transformPlug()
{
	return  getChild<TransformPlug>( g_firstPlugIndex + 1 );
}

const Gaffer::TransformPlug *TransformGrids::transformPlug() const
{
	return  getChild<TransformPlug>( g_firstPlugIndex + 1 );
}

void TransformGrids::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	SceneElementProcessor::affects( input, outputs );

	if( input == gridsPlug() ||  input == transformPlug() || input->parent() == transformPlug()  )
	{
		outputs.push_back( outPlug()->objectPlug() );
		outputs.push_back( outPlug()->boundPlug() );
	}
}

bool TransformGrids::processesObject() const
{
	return true;
}

void TransformGrids::hashProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	SceneElementProcessor::hashProcessedObject( path, context, h );

	transformPlug()->hash( h );
	gridsPlug()->hash( h );
}

IECore::ConstObjectPtr TransformGrids::computeProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::ConstObjectPtr inputObject ) const
{
	const VDBObject *vdbObject = runTimeCast<const VDBObject>(inputObject.get());

	if( !vdbObject )
	{
		return inputObject;
	}

	std::string grids = gridsPlug()->getValue();

	VDBObjectPtr newVDBObject = runTimeCast<VDBObject>(vdbObject->copy());
	std::vector<std::string> gridNames = newVDBObject->gridNames();

	auto convert = [](Imath::V3f v) -> openvdb::Vec3R
	{
		return openvdb::Vec3R(v[0], v[1],v[2]);
	};

	openvdb::tools::GridTransformer transfomer (
			convert( transformPlug()->pivotPlug()->getValue() ),
			convert( transformPlug()->scalePlug()->getValue() ),
			convert( transformPlug()->rotatePlug()->getValue() ),
			convert( transformPlug()->translatePlug()->getValue() )
			);

	for (const auto &gridName : gridNames )
	{
		if (IECore::StringAlgo::matchMultiple(gridName, grids))
		{
			openvdb::GridBase::ConstPtr srcGrid = newVDBObject->findGrid( gridName );

			auto srcFloatGrid = openvdb::GridBase::constGrid<openvdb::FloatGrid>( srcGrid );

			openvdb::FloatGrid::Ptr newGrid ( new openvdb::FloatGrid() );

			auto outFloatGrid = srcFloatGrid->deepCopy();

			transfomer.transformGrid<openvdb::tools::PointSampler>( *srcFloatGrid, *newGrid);

			newGrid->setName( srcFloatGrid->getName() );
			newVDBObject->insertGrid( newGrid );
		}
	}

	return newVDBObject;

}

bool TransformGrids::processesBound() const
{
	return true;
}

void TransformGrids::hashProcessedBound( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	SceneElementProcessor::hashProcessedBound( path, context, h );

	gridsPlug()->hash( h );
}

Imath::Box3f TransformGrids::computeProcessedBound( const ScenePath &path, const Gaffer::Context *context, const Imath::Box3f &inputBound ) const
{
	// todo calculate bounds from vdb grids
	return inputBound;
}


