//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2015, Image Engine Design Inc. All rights reserved.
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

#include "Gaffer/Transform2DPlug.h"

#include "GafferImage/Rectangle.h"
#include "GafferImage/BufferAlgo.h"

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace Gaffer;
using namespace GafferImage;
using namespace BufferAlgo;

namespace
{
	Box2i transform( const Box2i &b, const M33f &m )
	{
		if( b.isEmpty() )
		{
			return b;
		}

		Box2i r;
		r.extendBy( V2i( b.min.x, b.min.y ) * m );
		r.extendBy( V2i( b.max.x, b.min.y ) * m );
		r.extendBy( V2i( b.max.x, b.max.y ) * m );
		r.extendBy( V2i( b.min.x, b.max.y ) * m );
		return r;
	}

	float filteredPulse( float x, float edge0, float edge1, float width )
	{
		float x0 = x - width * .5f;
		float x1 = x0 + width;


		return max( 0.f, ( min ( x1, edge1 ) - max( x0, edge0 ) ) / width );	
	}

} // namespace

//////////////////////////////////////////////////////////////////////////
// Rectangle node
//////////////////////////////////////////////////////////////////////////

IE_CORE_DEFINERUNTIMETYPED( Rectangle );

size_t Rectangle::g_firstPlugIndex = 0;

Rectangle::Rectangle( const std::string &name )
	:	Shape( name )
{
	storeIndexOfNextChild( g_firstPlugIndex );
	addChild( new Box2iPlug( "area" ) );
	addChild( new FloatPlug( "softness", Plug::In, 1.f, 0.1f ) );
	addChild( new Transform2DPlug( "transform" ) );

}

Rectangle::~Rectangle()
{
}

Gaffer::Box2iPlug *Rectangle::areaPlug()
{
	return getChild<Box2iPlug>( g_firstPlugIndex );
}

const Gaffer::Box2iPlug *Rectangle::areaPlug() const
{
	return getChild<Box2iPlug>( g_firstPlugIndex );
}

Gaffer::FloatPlug *Rectangle::softnessPlug()
{
	return getChild<FloatPlug>( g_firstPlugIndex + 1 );
}

const Gaffer::FloatPlug *Rectangle::softnessPlug() const
{
	return getChild<FloatPlug>( g_firstPlugIndex + 1 );
}

Gaffer::Transform2DPlug *Rectangle::transformPlug()
{
	return getChild<Transform2DPlug>( g_firstPlugIndex + 2 );
}

const Gaffer::Transform2DPlug *Rectangle::transformPlug() const
{
	return getChild<Transform2DPlug>( g_firstPlugIndex + 2 );
}

bool Rectangle::affectsShapeDataWindow( const Gaffer::Plug *input ) const
{
	if( Shape::affectsShapeDataWindow( input ) )
	{
		return true;
	}

	return areaPlug()->isAncestorOf( input ) || transformPlug()->isAncestorOf( input ) || input == softnessPlug();
}

void Rectangle::hashShapeDataWindow( const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	Shape::hashShapeDataWindow( context, h );
	areaPlug()->hash( h );
	transformPlug()->hash( h );
	softnessPlug()->hash( h );
}

Imath::Box2i Rectangle::computeShapeDataWindow( const Gaffer::Context *context ) const
{

	const Box2i area = areaPlug()->getValue();
	const M33f m = transformPlug()->matrix();
	Box2i transformedArea = area;
	float softnessHalf = softnessPlug()->getValue() * .5f;
	transformedArea.extendBy( ( V2i( transformedArea.min.x - softnessHalf,  transformedArea.min.y - softnessHalf ) ) );
	transformedArea.extendBy( ( V2i( softnessHalf + transformedArea.max.x, softnessHalf + transformedArea.max.y ) ) );
	transformedArea = transform( transformedArea, m );

	return transformedArea;
}

bool Rectangle::affectsShapeChannelData( const Gaffer::Plug *input ) const
{
	if( Shape::affectsShapeChannelData( input ) )
	{
		return true;
	}

	return areaPlug()->isAncestorOf( input ) || transformPlug()->isAncestorOf( input ) || input == softnessPlug();
}

void Rectangle::hashShapeChannelData( const Imath::V2i &tileOrigin, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	Shape::hashShapeChannelData( tileOrigin, context, h );
	{
		ImagePlug::GlobalScope c( context );
		areaPlug()->hash( h );
		transformPlug()->hash( h );
		softnessPlug()->hash( h );
		
	}
	h.append( tileOrigin );
}

IECore::ConstFloatVectorDataPtr Rectangle::computeShapeChannelData(  const Imath::V2i &tileOrigin, const Gaffer::Context *context ) const
{

	FloatVectorDataPtr resultData = new FloatVectorData();
	vector<float> &result = resultData->writable();
	result.resize( ImagePlug::tileSize() * ImagePlug::tileSize(), 0 );
	
	Box2i tileBound( tileOrigin, tileOrigin + V2i( ImagePlug::tileSize() ) );
	const Box2i area = areaPlug()->getValue();
	const M33f t = transformPlug()->matrix();
	Box2i transformedArea = area;
	float softnessHalf = softnessPlug()->getValue() * .5f;
	transformedArea.extendBy( ( V2i( transformedArea.min.x - softnessHalf,  transformedArea.min.y - softnessHalf ) ) );
	transformedArea.extendBy( ( V2i( softnessHalf + transformedArea.max.x, softnessHalf + transformedArea.max.y ) ) );
	transformedArea = transform( transformedArea, t );
	const Box2i validBound = BufferAlgo::intersection( tileBound, transformedArea );

	int yoffset;
	int offset;
	float w;
	float h;
	float v;
	for( int y = validBound.min.y; y < validBound.max.y; y++)
	{
		for( int x = validBound.min.x; x < validBound.max.x; x++ )
		{
			yoffset = ( y - tileOrigin.y )* ImagePlug::tileSize();
			offset = yoffset + ( x - tileOrigin.x );
			V2f p( x, y );
			p *= t.inverse();
			w = filteredPulse( p.x, area.min.x, area.max.x, softnessPlug()->getValue() );
			h = filteredPulse( p.y, area.min.y, area.max.y, softnessPlug()->getValue() );
			v = w * h;
			result[offset] = v;
		}
	}


	return resultData;
}
