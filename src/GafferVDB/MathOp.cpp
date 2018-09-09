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

#include "GafferVDB/MathOp.h"

#include "IECore/StringAlgo.h"

#include "IECoreScene/PointsPrimitive.h"

#include "IECoreVDB/VDBObject.h"

#include "Gaffer/StringPlug.h"
#include "GafferScene/ScenePlug.h"

#include "openvdb/openvdb.h"
#include "openvdb/tools/GridOperators.h"


using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreVDB;
using namespace Gaffer;
using namespace GafferScene;
using namespace GafferVDB;

IE_CORE_DEFINERUNTIMETYPED( MathOp );

size_t MathOp::g_firstPlugIndex = 0;

MathOp::MathOp( const std::string &name )
        : SceneElementProcessor( name, IECore::PathMatcher::NoMatch )
{
    storeIndexOfNextChild( g_firstPlugIndex );

    addChild ( new StringPlug( "grids", Plug::In, "" ) );
    addChild ( new IntPlug( "type", Plug::In, 0 ) );
}

MathOp::~MathOp()
{
}



Gaffer::StringPlug *MathOp::gridsPlug()
{
    return getChild<StringPlug>( g_firstPlugIndex );
}

const Gaffer::StringPlug *MathOp::gridsPlug() const
{
    return getChild<const StringPlug>( g_firstPlugIndex );
}


Gaffer::IntPlug *MathOp::typePlug()
{
    return getChild<IntPlug>( g_firstPlugIndex + 1);
}

const Gaffer::IntPlug *MathOp::typePlug() const
{
    return getChild<const IntPlug>( g_firstPlugIndex + 1);
}

void MathOp::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
    SceneElementProcessor::affects( input, outputs );

    if ( input == gridsPlug() || input ==  typePlug() )
    {
        outputs.push_back( outPlug()->objectPlug() );
        outputs.push_back( outPlug()->boundPlug() );
    }
}

bool MathOp::processesObject() const
{
    return true;
}

void MathOp::hashProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
    SceneElementProcessor::hashProcessedObject( path, context, h );

    h.append( gridsPlug()->getValue() );
    h.append( typePlug()->getValue() );
}

IECore::ConstObjectPtr MathOp::computeProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::ConstObjectPtr inputObject ) const
{
    auto vdbObject = runTimeCast<const VDBObject>(inputObject.get());
    if( !vdbObject )
    {
        return inputObject;
    }

    std::string grids = gridsPlug()->getValue();

    VDBObjectPtr newVDBObject = runTimeCast<VDBObject>(vdbObject->copy());
    std::vector<std::string> gridNames = newVDBObject->gridNames();

    for (const auto &gridName : gridNames )
    {
        if ( IECore::StringAlgo::matchMultiple( gridName, grids ) )
        {
            openvdb::GridBase::ConstPtr srcGrid = newVDBObject->findGrid( gridName );

            // todo support multiple grid types
            const int type = typePlug()->getValue();
            if ( type == 0 )
            {
                openvdb::FloatGrid::ConstPtr floatGrid = openvdb::GridBase::constGrid<openvdb::FloatGrid>( srcGrid );
                typename openvdb::tools::ScalarToVectorConverter<openvdb::FloatGrid>::Type::Ptr gradientGrid = openvdb::tools::gradient( *floatGrid );
                gradientGrid->setName( floatGrid->getName() );
                newVDBObject->insertGrid( gradientGrid );
            }
            else if ( type == 1 )
            {
                openvdb::FloatGrid::ConstPtr floatGrid = openvdb::GridBase::constGrid<openvdb::FloatGrid>( srcGrid );
                openvdb::FloatGrid::Ptr laplacianGrid = openvdb::tools::laplacian( *floatGrid );
                laplacianGrid->setName( floatGrid->getName() );
                newVDBObject->insertGrid( laplacianGrid );
            }
            else if ( type == 2 )
            {
                openvdb::Vec3fGrid::ConstPtr v3grid = openvdb::GridBase::constGrid<openvdb::Vec3fGrid>( srcGrid );

                openvdb::FloatGrid::Ptr divGrid = openvdb::tools::divergence( *v3grid );
                divGrid->setName( v3grid->getName() );
                newVDBObject->insertGrid( divGrid );
            }
            else if ( type == 3 )
            {
                openvdb::Vec3fGrid::ConstPtr v3grid = openvdb::GridBase::constGrid<openvdb::Vec3fGrid>( srcGrid );

                openvdb::Vec3fGrid::Ptr curlGrid = openvdb::tools::curl( *v3grid );
                curlGrid->setName( v3grid->getName() );
                newVDBObject->insertGrid( curlGrid );
            }
            else if ( type == 4 )
            {
                openvdb::Vec3fGrid::ConstPtr v3grid = openvdb::GridBase::constGrid<openvdb::Vec3fGrid>( srcGrid );

                openvdb::FloatGrid::Ptr magnitudeGrid = openvdb::tools::magnitude( *v3grid );
                magnitudeGrid->setName( v3grid->getName() );
                newVDBObject->insertGrid( magnitudeGrid );
            }
            else if ( type == 5 )
            {
                openvdb::Vec3fGrid::ConstPtr v3grid = openvdb::GridBase::constGrid<openvdb::Vec3fGrid>( srcGrid );

                openvdb::Vec3fGrid::Ptr normalizedGrid = openvdb::tools::normalize( *v3grid );
                normalizedGrid->setName( v3grid->getName() );
                newVDBObject->insertGrid( normalizedGrid );
            }
            else if ( type == 6 )
            {
                openvdb::FloatGrid::ConstPtr srcGrid = openvdb::GridBase::constGrid<openvdb::FloatGrid>( srcGrid );

                openvdb::FloatGrid::Ptr meanCurvature = openvdb::tools::meanCurvature( *srcGrid );
                meanCurvature->setName( srcGrid->getName() );
                newVDBObject->insertGrid( meanCurvature );
            }
        }
    }

    return newVDBObject;

}

bool MathOp::processesBound() const
{
    return true;
}

void MathOp::hashProcessedBound( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
    SceneElementProcessor::hashProcessedBound( path, context, h );

    gridsPlug()->hash( h );
}

Imath::Box3f MathOp::computeProcessedBound( const ScenePath &path, const Gaffer::Context *context, const Imath::Box3f &inputBound ) const
{
    // todo calculate bounds from vdb grids
    return inputBound;
}

