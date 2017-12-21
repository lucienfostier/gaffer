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

#ifndef GAFFERIMAGE_RECTANGLE_H
#define GAFFERIMAGE_RECTANGLE_H

#include "Gaffer/BoxPlug.h"
#include "Gaffer/NumericPlug.h"

#include "GafferImage/Shape.h"

namespace Gaffer
{

IE_CORE_FORWARDDECLARE( Transform2DPlug )

} // namespace Gaffer

namespace GafferImage
{

class Rectangle : public Shape 
{

	public :

		Rectangle( const std::string &name=defaultName<Rectangle>() );
		~Rectangle() override;

		IE_CORE_DECLARERUNTIMETYPEDEXTENSION( GafferImage::Rectangle, RectangleTypeId, ImageProcessor );

		Gaffer::Box2iPlug *areaPlug();
		const Gaffer::Box2iPlug *areaPlug() const;

		Gaffer::FloatPlug *softnessPlug();
		const Gaffer::FloatPlug *softnessPlug() const;

		Gaffer::Transform2DPlug *transformPlug();
		const Gaffer::Transform2DPlug *transformPlug() const;


	protected :

		/// Must be implemented to return true if the input plug affects the computation of the
		/// data window for the shape.
		bool affectsShapeDataWindow( const Gaffer::Plug *input ) const override;
		/// Must be implemented to call the base class implementation and then append any
		/// plugs that will be used in computing the data window.
		void hashShapeDataWindow( const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		/// Must be implemented to return the data window for the shape.
		Imath::Box2i computeShapeDataWindow( const Gaffer::Context *context ) const override;

		/// Must be implemented to return true if the input plug affects the computation of the
		/// channel data for the shape.
		bool affectsShapeChannelData( const Gaffer::Plug *input ) const override;
		/// Must be implemented to call the base class implementation and then append any
		/// plugs that will be used in computing the shape channel data.
		void hashShapeChannelData( const Imath::V2i &tileOrigin, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		/// Must be implemented to return the channel data for the shape.
		IECore::ConstFloatVectorDataPtr computeShapeChannelData(  const Imath::V2i &tileOrigin, const Gaffer::Context *context ) const override;

	private :

		static size_t g_firstPlugIndex;

};

IE_CORE_DECLAREPTR( Rectangle )

} // namespace GafferImage

#endif // GAFFERIMAGE_RECTANGLE_H
