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

#include "GafferVDB/Clip.h"

#include "IECore/StringAlgo.h"

#include "IECoreScene/PointsPrimitive.h"

#include "IECoreVDB/VDBObject.h"

#include "Gaffer/StringPlug.h"

#include "openvdb/openvdb.h"
#include "openvdb/tools/Clip.h"

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreVDB;
using namespace Gaffer;
using namespace GafferVDB;
using namespace GafferScene;

IE_CORE_DEFINERUNTIMETYPED( Clip );

size_t Clip::g_firstPlugIndex = 0;

Clip::Clip( const std::string &name )
		: SceneElementProcessor( name, IECore::PathMatcher::NoMatch )
{
	storeIndexOfNextChild(g_firstPlugIndex);

	addChild( new ScenePlug( "in", Gaffer::Plug::In ) );

	addChild( new StringPlug( "vdbLocation", Plug::In, "/vdb" ) );
	addChild( new StringPlug( "grids", Plug::In, "density" ) );
	addChild( new IntPlug( "operation", Plug::In, 0) );

}

Clip::~Clip()
{
}

GafferScene::ScenePlug *Clip::otherPlug()
{
	return  getChild<ScenePlug>( g_firstPlugIndex );
}

const GafferScene::ScenePlug *Clip::otherPlug() const
{
	return  getChild<ScenePlug>( g_firstPlugIndex );
}

Gaffer::StringPlug *Clip::vdbLocationPlug()
{
	return  getChild<StringPlug>( g_firstPlugIndex + 1);
}

const Gaffer::StringPlug *Clip::vdbLocationPlug() const
{
	return  getChild<StringPlug>( g_firstPlugIndex + 1);
}

Gaffer::StringPlug *Clip::gridsPlug()
{
	return  getChild<StringPlug>( g_firstPlugIndex + 2 );
}

const Gaffer::StringPlug *Clip::gridsPlug() const
{
	return  getChild<StringPlug>( g_firstPlugIndex + 2 );
}

Gaffer::IntPlug *Clip::operationPlug()
{
	return  getChild<IntPlug>( g_firstPlugIndex + 3 );
}

const Gaffer::IntPlug *Clip::operationPlug() const
{
	return  getChild<IntPlug>( g_firstPlugIndex + 3 );
}

void Clip::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	SceneElementProcessor::affects( input, outputs );

	if( input == operationPlug() || input->parent() == otherPlug() || input == gridsPlug() || input == vdbLocationPlug() )
	{
		outputs.push_back( outPlug()->objectPlug() );
		outputs.push_back( outPlug()->boundPlug() );
	}
}

bool Clip::processesObject() const
{
	return true;
}

void Clip::hashProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	SceneElementProcessor::hashProcessedObject( path, context, h );

	ScenePlug::ScenePath p ;
	ScenePlug::stringToPath(vdbLocationPlug()->getValue(), p);
	operationPlug()->hash( h );
	h.append( otherPlug()->objectHash( p ) );
	h.append( gridsPlug()->hash() );
	h.append( vdbLocationPlug()->hash() );
}

IECore::ConstObjectPtr Clip::computeProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::ConstObjectPtr inputObject ) const
{
	const VDBObject *vdbObject = runTimeCast<const VDBObject>(inputObject.get());
	if( !vdbObject )
	{
		return inputObject;
	}

	std::vector<std::string> grids = vdbObject->gridNames();
	std::string gridsToProcess = gridsPlug()->getValue();

	ScenePlug::ScenePath p ;
	ScenePlug::stringToPath(vdbLocationPlug()->getValue(), p);

	Imath::Box3f bound = otherPlug()->bound( p );
	Imath::M44f transform = otherPlug()->fullTransform( p );

	Imath::M44f inputTransform = inPlug()->fullTransform( path );
	inputTransform.inverse();
	bound = Imath::transform( bound, transform * inputTransform);

	IECoreVDB::VDBObjectPtr newVDBObject = vdbObject->copy();

	for (const auto &gridName : grids )
	{
		if (IECore::StringAlgo::matchMultiple(gridName, gridsToProcess))
		{
			openvdb::GridBase::ConstPtr grid = vdbObject->findGrid(gridName);
			openvdb::FloatGrid::ConstPtr floatGrid = openvdb::GridBase::constGrid<openvdb::FloatGrid>(grid);

			if (floatGrid)
			{
				openvdb::BBoxd bbox (openvdb::Vec3d(bound.min[0],bound.min[1],bound.min[2]), openvdb::Vec3d(bound.max[0], bound.max[1], bound.max[2]));

				openvdb::FloatGrid::Ptr clippedGrid = openvdb::tools::clip(*floatGrid, bbox);
				newVDBObject->insertGrid( clippedGrid );
			}
		}
	}
	return newVDBObject;
}

bool Clip::processesBound() const
{
	return true;
}

void Clip::hashProcessedBound( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	SceneElementProcessor::hashProcessedBound( path, context, h );

	gridsPlug()->hash( h );
}

Imath::Box3f Clip::computeProcessedBound( const ScenePath &path, const Gaffer::Context *context, const Imath::Box3f &inputBound ) const
{
	// todo calculate bounds from vdb grids
	return inputBound;
}


