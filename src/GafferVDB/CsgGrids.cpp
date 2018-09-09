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

#include "GafferVDB/CsgGrids.h"

#include "IECore/StringAlgo.h"

#include "IECoreScene/PointsPrimitive.h"

#include "IECoreVDB/VDBObject.h"

#include "Gaffer/StringPlug.h"

#include "openvdb/openvdb.h"
#include "openvdb/tools/Composite.h"

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreVDB;
using namespace Gaffer;
using namespace GafferVDB;
using namespace GafferScene;

IE_CORE_DEFINERUNTIMETYPED( CSGGrids );

size_t CSGGrids::g_firstPlugIndex = 0;

CSGGrids::CSGGrids( const std::string &name )
		: SceneElementProcessor( name, IECore::PathMatcher::NoMatch )
{
	storeIndexOfNextChild(g_firstPlugIndex);

	addChild( new ScenePlug( "in", Gaffer::Plug::In ) );

	addChild( new StringPlug( "vdbLocation", Plug::In, "/vdb" ) );
	addChild( new StringPlug( "grid", Plug::In, "density" ) );
	addChild( new IntPlug( "operation", Plug::In, 0) );

}

CSGGrids::~CSGGrids()
{
}

GafferScene::ScenePlug *CSGGrids::otherPlug()
{
	return  getChild<ScenePlug>( g_firstPlugIndex );
}

const GafferScene::ScenePlug *CSGGrids::otherPlug() const
{
	return  getChild<ScenePlug>( g_firstPlugIndex );
}

Gaffer::StringPlug *CSGGrids::vdbLocationPlug()
{
	return  getChild<StringPlug>( g_firstPlugIndex + 1);
}

const Gaffer::StringPlug *CSGGrids::vdbLocationPlug() const
{
	return  getChild<StringPlug>( g_firstPlugIndex + 1);
}

Gaffer::StringPlug *CSGGrids::gridPlug()
{
	return  getChild<StringPlug>( g_firstPlugIndex + 2 );
}

const Gaffer::StringPlug *CSGGrids::gridPlug() const
{
	return  getChild<StringPlug>( g_firstPlugIndex + 2 );
}

Gaffer::IntPlug *CSGGrids::operationPlug()
{
	return  getChild<IntPlug>( g_firstPlugIndex + 3 );
}

const Gaffer::IntPlug *CSGGrids::operationPlug() const
{
	return  getChild<IntPlug>( g_firstPlugIndex + 3 );
}

void CSGGrids::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	SceneElementProcessor::affects( input, outputs );

	if( input == operationPlug() || input->parent() == otherPlug() || input == gridPlug() || input == vdbLocationPlug() )
	{
		outputs.push_back( outPlug()->objectPlug() );
		outputs.push_back( outPlug()->boundPlug() );
	}
}

bool CSGGrids::processesObject() const
{
	return true;
}

void CSGGrids::hashProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	SceneElementProcessor::hashProcessedObject( path, context, h );

	ScenePlug::ScenePath p ;
	ScenePlug::stringToPath(vdbLocationPlug()->getValue(), p);
	operationPlug()->hash( h );
	h.append( otherPlug()->objectHash( p ) );
	h.append( gridPlug()->hash() );
	h.append( vdbLocationPlug()->hash() );
}

template<template<typename> class F>
class Dispatcher
{
public:

	openvdb::GridBase::Ptr operator()( openvdb::GridBase::Ptr grid ) const
	{
		if ( grid->valueType() == openvdb::typeNameAsString<double>() )
		{
			F<openvdb::DoubleGrid> f;
			auto p = f( grid );
			return p;
		}

		return nullptr;
	}

	openvdb::GridBase::Ptr operator()( openvdb::GridBase::ConstPtr grid ) const
	{
		if ( grid->valueType() == openvdb::typeNameAsString<double>() )
		{
			F<openvdb::DoubleGrid> f;
			auto p = f( grid );
			return p;
		}
		return nullptr;
	}

	openvdb::GridBase::Ptr operator()( openvdb::GridBase::Ptr gridA, openvdb::GridBase::Ptr gridB  ) const
	{
		if ( gridA->valueType() == openvdb::typeNameAsString<double>() && gridB->valueType() == openvdb::typeNameAsString<double>() )
		{
			F<openvdb::DoubleGrid> f;
			auto p = f( gridA, gridB );
			return p;
		}
		return nullptr;
	}

	openvdb::GridBase::Ptr operator()( openvdb::GridBase::ConstPtr gridA, openvdb::GridBase::ConstPtr gridB  ) const
	{
		if ( gridA->valueType() == openvdb::typeNameAsString<double>() && gridB->valueType() == openvdb::typeNameAsString<double>() )
		{
			F<openvdb::DoubleGrid> f;
			auto p = f( openvdb::GridBase::constGrid<openvdb::DoubleGrid>( gridA ), openvdb::GridBase::constGrid<openvdb::DoubleGrid>( gridB ) );
			return p;
		}
		else if ( gridA->valueType() == openvdb::typeNameAsString<float>() && gridB->valueType() == openvdb::typeNameAsString<float>() )
		{
			F<openvdb::FloatGrid> f;
			auto p = f( openvdb::GridBase::constGrid<openvdb::FloatGrid>( gridA ), openvdb::GridBase::constGrid<openvdb::FloatGrid>( gridB ) );
			return p;
		}
		return nullptr;
	}
};

template<typename VDBGridType>
class UnionFunctor
{
public:
	typedef typename VDBGridType::Ptr Ptr;
	typedef typename VDBGridType::ConstPtr ConstPtr;

	Ptr operator() (ConstPtr gridA, ConstPtr gridB )
	{
		Ptr copyOfGridA = gridA->deepCopy();
		Ptr copyOfGridB = gridB->deepCopy();

		openvdb::tools::csgUnion(*copyOfGridA, *copyOfGridB);

		return copyOfGridA;
	}
};

template<typename VDBGridType>
class IntersectionFunctor
{
public:
	typedef typename VDBGridType::Ptr Ptr;
	typedef typename VDBGridType::ConstPtr ConstPtr;

	Ptr operator() (ConstPtr gridA, ConstPtr gridB )
	{
		Ptr copyOfGridA = gridA->deepCopy();
		Ptr copyOfGridB = gridB->deepCopy();

		openvdb::tools::csgIntersection(*copyOfGridA, *copyOfGridB);

		return copyOfGridA;
	}
};

template<typename VDBGridType>
class DifferenceFunctor
{
public:
	typedef typename VDBGridType::Ptr Ptr;
	typedef typename VDBGridType::ConstPtr ConstPtr;

	Ptr operator() (ConstPtr gridA, ConstPtr gridB )
	{
		Ptr copyOfGridA = gridA->deepCopy();
		Ptr copyOfGridB = gridB->deepCopy();

		openvdb::tools::csgDifference(*copyOfGridA, *copyOfGridB);

		return copyOfGridA;
	}
};


IECore::ConstObjectPtr CSGGrids::computeProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::ConstObjectPtr inputObject ) const
{
	const VDBObject *vdbObjectA = runTimeCast<const VDBObject>(inputObject.get());
	if( !vdbObjectA )
	{
		return inputObject;
	}

	ScenePlug::ScenePath p ;
	ScenePlug::stringToPath(vdbLocationPlug()->getValue(), p);

	ConstVDBObjectPtr vdbObjectB = runTimeCast<const VDBObject>( otherPlug()->object( p ) );

	if ( !vdbObjectB )
	{
		return inputObject;
	}

	openvdb::FloatGrid::ConstPtr srcGridA =  openvdb::GridBase::constGrid<openvdb::FloatGrid>( vdbObjectA->findGrid( gridPlug()->getValue() ) );
	openvdb::FloatGrid::ConstPtr srcGridB =  openvdb::GridBase::constGrid<openvdb::FloatGrid>( vdbObjectB->findGrid( gridPlug()->getValue() ) );

	if (!srcGridA || !srcGridB)
	{
		return inputObject;
	}

	VDBObjectPtr newVDBObject = vdbObjectA->copy();


	openvdb::FloatGrid::Ptr copyOfGridA = srcGridA->deepCopy();
	openvdb::FloatGrid::Ptr copyOfGridB = srcGridB->deepCopy();


	switch(operationPlug()->getValue())
	{
		case 0:
		{
			Dispatcher<UnionFunctor> d;
			newVDBObject->insertGrid( d( srcGridA, srcGridB ) );
			break;
		}
		case 1:
		{
			Dispatcher<IntersectionFunctor> d;
			newVDBObject->insertGrid( d( srcGridA, srcGridB ) );
			break;
		}
		case 2:
		{
			Dispatcher<DifferenceFunctor> d;
			newVDBObject->insertGrid( d( srcGridA, srcGridB ) );
			break;
		}
		case 3:
		{
			Dispatcher<DifferenceFunctor> d;
			newVDBObject->insertGrid( d( srcGridB, srcGridA ) );
			break;
		}
		default:
			return inputObject;
	}


	return newVDBObject;
}

bool CSGGrids::processesBound() const
{
	return true;
}

void CSGGrids::hashProcessedBound( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	SceneElementProcessor::hashProcessedBound( path, context, h );

	gridPlug()->hash( h );
}

Imath::Box3f CSGGrids::computeProcessedBound( const ScenePath &path, const Gaffer::Context *context, const Imath::Box3f &inputBound ) const
{
	// todo calculate bounds from vdb grids
	return inputBound;
}


