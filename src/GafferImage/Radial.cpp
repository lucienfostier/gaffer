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
#include "IECore/BoxAlgo.h"

#include "Gaffer/Transform2DPlug.h"

#include "GafferImage/Radial.h"
#include "GafferImage/BufferAlgo.h"

#include <cmath>

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace Gaffer;
using namespace GafferImage;
using namespace BufferAlgo;

namespace
{
	float remap( float value, float oldMin, float oldMax, float newMin, float newMax )
	{
		return newMin + (value - oldMin) * (newMax - newMin) / (oldMax - oldMin); 
	}

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

} // namespace

//////////////////////////////////////////////////////////////////////////
// Radial node
//////////////////////////////////////////////////////////////////////////

IE_CORE_DEFINERUNTIMETYPED( Radial );

size_t Radial::g_firstPlugIndex = 0;

Radial::Radial( const std::string &name )
	:	Shape( name )
{
	storeIndexOfNextChild( g_firstPlugIndex );
	addChild( new Box2iPlug( "area" ) );
	addChild( new FloatPlug( "softness", Plug::In, 0.f, 0.f, 1.f ) );
	addChild( new Transform2DPlug( "transform" ) );
}

Radial::~Radial()
{
}

Gaffer::Box2iPlug *Radial::areaPlug()
{
	return getChild<Box2iPlug>( g_firstPlugIndex );
}

const Gaffer::Box2iPlug *Radial::areaPlug() const
{
	return getChild<Box2iPlug>( g_firstPlugIndex );
}

Gaffer::FloatPlug *Radial::softnessPlug()
{
	return getChild<FloatPlug>( g_firstPlugIndex + 1 );
}

const Gaffer::FloatPlug *Radial::softnessPlug() const
{
	return getChild<FloatPlug>( g_firstPlugIndex + 1 );
}

Gaffer::Transform2DPlug *Radial::transformPlug()
{
	return getChild<Transform2DPlug>( g_firstPlugIndex + 2 );
}

const Gaffer::Transform2DPlug *Radial::transformPlug() const
{
	return getChild<Transform2DPlug>( g_firstPlugIndex + 2 );
}

bool Radial::affectsShapeDataWindow( const Gaffer::Plug *input ) const
{
	if( Shape::affectsShapeDataWindow( input ) )
	{
		return true;
	}

	return areaPlug()->isAncestorOf( input ) || transformPlug()->isAncestorOf( input );
}

void Radial::hashShapeDataWindow( const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	Shape::hashShapeDataWindow( context, h );
	areaPlug()->hash( h );
	transformPlug()->hash( h );
}

Imath::Box2i Radial::computeShapeDataWindow( const Gaffer::Context *context ) const
{
	const M33f m = transformPlug()->matrix();
	return transform( areaPlug()->getValue(), m );
}

bool Radial::affectsShapeChannelData( const Gaffer::Plug *input ) const
{
	if( Shape::affectsShapeChannelData( input ) )
	{
		return true;
	}

	return areaPlug()->isAncestorOf( input ) || input == softnessPlug() || transformPlug()->isAncestorOf( input );
}

void Radial::hashShapeChannelData( const Imath::V2i &tileOrigin, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	Shape::hashShapeChannelData( tileOrigin, context, h );
	{
		ImagePlug::GlobalScope c( context );
		areaPlug()->hash( h );
		softnessPlug()->hash( h );
		transformPlug()->hash( h );
	}
	h.append( tileOrigin );
}

IECore::ConstFloatVectorDataPtr Radial::computeShapeChannelData(  const Imath::V2i &tileOrigin, const Gaffer::Context *context ) const
{
	FloatVectorDataPtr resultData = new FloatVectorData();
	vector<float> &result = resultData->writable();
	result.resize( ImagePlug::tileSize() * ImagePlug::tileSize(), 0 );
	
	Box2i tileBound( tileOrigin, tileOrigin + V2i( ImagePlug::tileSize() ) );

	const Box2i area = areaPlug()->getValue();

	const M33f t = transformPlug()->matrix();

	const V2i center( ( area.max.x - area.min.x ) * 0.5 + area.min.x, ( area.max.y - area.min.y ) * 0.5 + area.min.y );
	const V2f radius( pow( area.size().x * .5, 2 ), pow( area.size().y * .5, 2 ) );

	const Box2i transformedArea = transform( area, t );
	const Box2i validBound = BufferAlgo::intersection( tileBound, transformedArea );

	int yoffset;
	int offset;
	float dx;
	float dy;
	float v;

	for( int y = validBound.min.y; y < validBound.max.y; y++)
	{
		for( int x = validBound.min.x; x < validBound.max.x; x++ )
		{
			yoffset = ( y - tileOrigin.y )* ImagePlug::tileSize();
			offset = yoffset + ( x - tileOrigin.x );
			V2f p( x, y );
			p *= t.inverse();
			dx = pow( ( p.x - center.x ) , 2 );
			dy = pow( ( p.y - center.y ) , 2 );
			v = remap( ( dx / radius.x + dy / radius.y ), softnessPlug()->getValue() - 0.005 , 1, 1, 0 );
			result[offset] = clamp( v, 0.f, 1.f );
		}
	}

	return resultData;
}
