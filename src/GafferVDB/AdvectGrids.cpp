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

#include "GafferVDB/AdvectGrids.h"

#include "IECore/StringAlgo.h"

#include "IECoreScene/PointsPrimitive.h"

#include "IECoreVDB/VDBObject.h"

#include "Gaffer/StringPlug.h"
#include "GafferScene/ScenePlug.h"

#include "openvdb/openvdb.h"
#include "openvdb/tools/VolumeAdvect.h"
#include "openvdb/tools/LevelSetAdvect.h"
#include "openvdb/tools/Interpolation.h"


using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreVDB;
using namespace Gaffer;
using namespace GafferScene;
using namespace GafferVDB;

IE_CORE_DEFINERUNTIMETYPED( AdvectGrids );

size_t AdvectGrids::g_firstPlugIndex = 0;

AdvectGrids::AdvectGrids( const std::string &name )
        : SceneElementProcessor( name, IECore::PathMatcher::NoMatch )
{
    storeIndexOfNextChild( g_firstPlugIndex );

    addChild ( new ScenePlug( "velocityScene", Plug::In ) );
    addChild ( new StringPlug( "velocityLocation", Plug::In, "" ) );
    addChild ( new StringPlug( "velocityGrid", Plug::In, "vel" ) );

    addChild ( new StringPlug( "grids", Plug::In, "" ) );
    addChild ( new FloatPlug( "time", Plug::In, 0.0f ) );
}

AdvectGrids::~AdvectGrids()
{
}

GafferScene::ScenePlug *AdvectGrids::velocityScenePlug()
{
    return getChild<GafferScene::ScenePlug>( g_firstPlugIndex );
}

const GafferScene::ScenePlug *AdvectGrids::velocityScenePlug() const
{
    return getChild<const GafferScene::ScenePlug>( g_firstPlugIndex );
}

Gaffer::StringPlug *AdvectGrids::velocityLocationPlug()
{
    return getChild<StringPlug>( g_firstPlugIndex + 1);
}

const Gaffer::StringPlug *AdvectGrids::velocityLocationPlug() const
{
    return getChild<const StringPlug>( g_firstPlugIndex + 1);
}

Gaffer::StringPlug *AdvectGrids::velocityGridPlug()
{
    return getChild<StringPlug>( g_firstPlugIndex + 2);
}

const Gaffer::StringPlug *AdvectGrids::velocityGridPlug() const
{
    return getChild<const StringPlug>( g_firstPlugIndex + 2);
}

Gaffer::StringPlug *AdvectGrids::gridsPlug()
{
    return getChild<StringPlug>( g_firstPlugIndex + 3);
}

const Gaffer::StringPlug *AdvectGrids::gridsPlug() const
{
    return getChild<const StringPlug>( g_firstPlugIndex + 3);
}

Gaffer::FloatPlug *AdvectGrids::timePlug()
{
    return getChild<FloatPlug>( g_firstPlugIndex + 4);
}

const Gaffer::FloatPlug *AdvectGrids::timePlug() const
{
    return getChild<const FloatPlug>( g_firstPlugIndex + 4);
}


void AdvectGrids::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
    SceneElementProcessor::affects( input, outputs );

    if ( input->parent() == velocityScenePlug()
        ||  input == velocityLocationPlug()
        ||  input == velocityGridPlug()
        ||  input == gridsPlug()
        ||  input == timePlug()
        )
    {
        outputs.push_back( outPlug()->objectPlug() );
        outputs.push_back( outPlug()->boundPlug() );
    }
}

bool AdvectGrids::processesObject() const
{
    return true;
}

void AdvectGrids::hashProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
    SceneElementProcessor::hashProcessedObject( path, context, h );

    ScenePlug::ScenePath velocityLocationPath;
    ScenePlug::stringToPath( velocityLocationPlug()->getValue(), velocityLocationPath);

    h.append ( velocityScenePlug()->objectHash( velocityLocationPath ) );
    h.append ( velocityScenePlug()->transformHash( velocityLocationPath ) );

    h.append( gridsPlug()->getValue() );
    h.append( timePlug()->getValue() );

}

IECore::ConstObjectPtr AdvectGrids::computeProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::ConstObjectPtr inputObject ) const
{
    const VDBObject *vdbObject = runTimeCast<const VDBObject>(inputObject.get());
    if( !vdbObject )
    {
        return inputObject;
    }

    ScenePlug::ScenePath velocityLocationPath;
    ScenePlug::stringToPath( velocityLocationPlug()->getValue(), velocityLocationPath);

    IECore::ConstObjectPtr obj = velocityScenePlug()->object( velocityLocationPath );

    IECoreVDB::ConstVDBObjectPtr velocityVDBObject = runTimeCast<const VDBObject>( obj );

    openvdb::GridBase::ConstPtr velocityGrid = velocityVDBObject->findGrid( velocityGridPlug()->getValue() );

    openvdb::Vec3fGrid::ConstPtr v3fGrid = openvdb::GridBase::constGrid<openvdb::Vec3fGrid>( velocityGrid );

    openvdb::tools::VolumeAdvection<> volumeAdvection( *v3fGrid );

    std::vector<std::string> gridNames = vdbObject->gridNames();

    std::string gridsToAdvect = gridsPlug()->getValue();

    VDBObjectPtr newVDBObject = runTimeCast<VDBObject>(vdbObject->copy());

    for (const auto &gridName : gridNames )
    {
        if ( IECore::StringAlgo::matchMultiple( gridName, gridsToAdvect ) )
        {
            openvdb::GridBase::ConstPtr srcGrid = newVDBObject->findGrid( gridName );

            // todo support multiple grid types
            openvdb::FloatGrid::ConstPtr floatGrid = openvdb::GridBase::constGrid<openvdb::FloatGrid>( srcGrid );

            // todo there are other choices for the sampler type other than point sampler
            openvdb::FloatGrid::Ptr dstGrid = volumeAdvection.advect<openvdb::FloatGrid, openvdb::tools::PointSampler >( *floatGrid, (double) timePlug()->getValue() );

            dstGrid->setName( floatGrid->getName() + "_advect");
            newVDBObject->insertGrid( dstGrid );
        }
    }

    return newVDBObject;
}

bool AdvectGrids::processesBound() const
{
    return true;
}

void AdvectGrids::hashProcessedBound( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
    SceneElementProcessor::hashProcessedBound( path, context, h );

    //gridsPlug()->hash( h );
}

Imath::Box3f AdvectGrids::computeProcessedBound( const ScenePath &path, const Gaffer::Context *context, const Imath::Box3f &inputBound ) const
{
    // todo calculate bounds from vdb grids
    return inputBound;
}

