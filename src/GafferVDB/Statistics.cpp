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

#include "GafferVDB/Statistics.h"

#include "IECore/StringAlgo.h"

#include "IECoreScene/PointsPrimitive.h"

#include "IECoreVDB/VDBObject.h"

#include "Gaffer/StringPlug.h"
#include "GafferScene/ScenePlug.h"

#include "openvdb/openvdb.h"
#include "openvdb/tools/GridOperators.h"
#include "openvdb/tools/Statistics.h"

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreVDB;
using namespace Gaffer;
using namespace GafferScene;
using namespace GafferVDB;

IE_CORE_DEFINERUNTIMETYPED( Statistics );

size_t Statistics::g_firstPlugIndex = 0;

Statistics::Statistics( const std::string &name ) : SceneElementProcessor( name, IECore::PathMatcher::NoMatch )
{
	storeIndexOfNextChild( g_firstPlugIndex );

	addChild( new StringPlug( "grids", Plug::In, "*" ) );
	addChild( new IntPlug( "bins", Plug::In, 5 ) );

	outPlug()->boundPlug()->setInput( inPlug()->boundPlug() );
	outPlug()->objectPlug()->setInput( inPlug()->objectPlug() );
}

Statistics::~Statistics()
{
}

Gaffer::StringPlug *Statistics::gridsPlug()
{
	return getChild<StringPlug>( g_firstPlugIndex );
}

const Gaffer::StringPlug *Statistics::gridsPlug() const
{
	return getChild<const StringPlug>( g_firstPlugIndex );
}

Gaffer::IntPlug *Statistics::binsPlug()
{
	return getChild<IntPlug>( g_firstPlugIndex + 1 );
}

const Gaffer::IntPlug *Statistics::binsPlug() const
{
	return getChild<const IntPlug>( g_firstPlugIndex + 1 );
}

void Statistics::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	SceneElementProcessor::affects( input, outputs );

	if( input == gridsPlug() || input == binsPlug() )
	{
		outputs.push_back( outPlug()->attributesPlug() );
	}
}

bool Statistics::processesAttributes() const
{
	return true;
}

void Statistics::hashProcessedAttributes( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	SceneElementProcessor::hashProcessedAttributes( path, context, h );

	h.append( inPlug()->objectHash( path ) );
	h.append( gridsPlug()->getValue() );
	h.append( binsPlug()->getValue() );
}

IECore::ConstCompoundObjectPtr Statistics::computeProcessedAttributes(
	const ScenePath &path, const Gaffer::Context *context, IECore::ConstCompoundObjectPtr inputAttributes
) const
{

	auto vdbObject = IECore::runTimeCast<const IECoreVDB::VDBObject>( inPlug()->object( path ) );

	if( !vdbObject )
	{
		return inputAttributes;
	}

	std::vector<std::string> gridNames = vdbObject->gridNames();
	std::string grids = gridsPlug()->getValue();

	IECore::CompoundObjectPtr newAttributes = inputAttributes->copy();

	int bins = binsPlug()->getValue();

	for( const auto &gridName : gridNames )
	{
		if( IECore::StringAlgo::matchMultiple( gridName, grids ) )
		{
			openvdb::GridBase::ConstPtr srcGrid = vdbObject->findGrid( gridName );

			openvdb::FloatGrid::ConstPtr grid = openvdb::GridBase::constGrid<openvdb::FloatGrid>( srcGrid );

			// todo stats for other grid types
			if( !grid )
			{
				continue;
			}

			openvdb::math::Stats stats = openvdb::tools::statistics( grid->cbeginValueOn() ); //, openvdb::Local::addIfPositive, /*threaded=*/true

			newAttributes->members().insert( std::make_pair( std::string( "volume_stats:" ) + gridName + ":average", new IECore::FloatData( stats.avg() ) ) );
			newAttributes->members().insert( std::make_pair( std::string( "volume_stats:" ) + gridName + ":min", new IECore::FloatData( stats.min() ) ) );
			newAttributes->members().insert( std::make_pair( std::string( "volume_stats:" ) + gridName + ":max", new IECore::FloatData( stats.max() ) ) );
			newAttributes->members().insert( std::make_pair( std::string( "volume_stats:" ) + gridName + ":stddev", new IECore::FloatData( stats.stdDev() ) ) );

			// todo consider adding plug for histogram bin count.
			openvdb::math::Histogram histogram = openvdb::tools::histogram( grid->cbeginValueOn(), stats.min(), stats.max(), bins );

			std::vector<float> binValues( histogram.numBins() );

			for( size_t i = 0; i < histogram.numBins(); ++i )
			{
				binValues[i] = (float) ( histogram.count( i ) / ( (double) histogram.size() ) );
			}

			newAttributes->members().insert(
				std::make_pair(
					std::string( "volume_stats:" ) + gridName + ":histogram", new IECore::FloatVectorData( binValues )
				)
			);

		}
	}

	return newAttributes;
}


