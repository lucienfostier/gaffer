//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2014, Lucien Fostier All rights reserved.
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

#include "GafferML/TensorToMesh.h"

#include "IECoreScene/MeshPrimitive.h"


using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreScene;
using namespace Gaffer;
using namespace GafferML;

GAFFER_NODE_DEFINE_TYPE( TensorToMesh );

size_t TensorToMesh::g_firstPlugIndex = 0;

TensorToMesh::TensorToMesh( const std::string &name )
	:	ObjectSource( name, "tensorMesh" )
{

	storeIndexOfNextChild( g_firstPlugIndex );

	addChild( new TensorPlug( "vertices" ) );
	addChild( new TensorPlug( "faces" ) );

}

TensorToMesh::~TensorToMesh()
{
}

TensorPlug *TensorToMesh::verticesTensorPlug()
{
	return getChild<TensorPlug>( g_firstPlugIndex );
}

const TensorPlug *TensorToMesh::verticesTensorPlug() const
{
	return getChild<TensorPlug>( g_firstPlugIndex );
}

TensorPlug *TensorToMesh::facesTensorPlug()
{
	return getChild<TensorPlug>( g_firstPlugIndex + 1 );
}

const TensorPlug *TensorToMesh::facesTensorPlug() const
{
	return getChild<TensorPlug>( g_firstPlugIndex + 1 );
}

void TensorToMesh::affects( const Plug *input, AffectedPlugsContainer &outputs ) const
{
	ObjectSource::affects( input, outputs );
	if ( input == verticesTensorPlug() )
	{
		outputs.push_back(outPlug()->boundPlug());
		outputs.push_back(outPlug()->objectPlug());
	}
	if ( input == facesTensorPlug() )
	{
		outputs.push_back(outPlug()->objectPlug());
	}

}

void TensorToMesh::hashSource( const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	verticesTensorPlug()->hash( h );
	facesTensorPlug()->hash( h );
}

IECore::ConstObjectPtr TensorToMesh::computeSource( const Context *context ) const
{
	ConstTensorPtr verticesTensorData = verticesTensorPlug()->getValue();
	const size_t count = verticesTensorData->value().GetTensorTypeAndShapeInfo().GetElementCount();
	const float *sourceData = verticesTensorData->value().GetTensorData<float>();

	ConstTensorPtr facesTensorData = facesTensorPlug()->getValue();
	const int64_t *sourceFacesData = facesTensorData->value().GetTensorData<int64_t>();

	// Copy out topology
	IntVectorDataPtr verticesPerFaceData = new IntVectorData;
	vector<int> &verticesPerFace = verticesPerFaceData->writable();

	IntVectorDataPtr vertexIdsData = new IntVectorData;
	vector<int> &vertexIds = vertexIdsData->writable();

	V3fVectorDataPtr pointsData = new V3fVectorData;
	vector<V3f> &points = pointsData->writable();

	for( size_t i = 0; i < count; i += 3 )
	{
		Imath::V3f v;
		for( size_t j = 0; j < 3; j++ )
		{
			v[j] = *( sourceData + ( i + j ) );
		}
		points.push_back( v );
	}

	int vertexPerFace = facesTensorData->value().GetTensorTypeAndShapeInfo().GetShape()[1];
	for( int i = 0; i < facesTensorData->value().GetTensorTypeAndShapeInfo().GetShape()[0]; i++ )
	{
		verticesPerFace.push_back(vertexPerFace);
		for ( int j = 0; j < vertexPerFace; j++ )
		{
			vertexIds.push_back( *( sourceFacesData + i * vertexPerFace + j ) );
		}
	}

	return new MeshPrimitive( verticesPerFaceData, vertexIdsData, "linear", pointsData );
}
