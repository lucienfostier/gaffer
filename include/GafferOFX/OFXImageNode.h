//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2025, Lucien Fostier. All rights reserved.
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

#pragma once

#include "GafferOFX/Export.h"
#include "GafferOFX/TypeIds.h"
#include "GafferOFX/EffectImageInstance.h"

#include "Gaffer/StringPlug.h"

#include "GafferImage/ImageProcessor.h"

namespace GafferOFX
{

class GAFFEROFX_API OFXImageNode : public GafferImage::ImageProcessor
{

	public :

		explicit OFXImageNode( const std::string &name=defaultName<OFXImageNode>() );
		~OFXImageNode() override;
		
		GAFFER_NODE_DECLARE_TYPE( GafferOFX::OFXImageNode, OFXImageNodeTypeId, GafferImage::ImageProcessor );

		bool createPluginInstance();

		Gaffer::StringPlug* pluginIdPlug();
		const Gaffer::StringPlug* pluginIdPlug() const;


		Gaffer::Plug *parametersPlug();
		const Gaffer::Plug *parametersPlug() const;


		void affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const override;

		const GafferOFX::EffectImageInstance* effectInstance() const;

	protected :

		void hashViewNames( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		void hashFormat( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		void hashDataWindow( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		void hashSampleOffsets( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		void hashChannelNames( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		void hashChannelData( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;

		IECore::ConstStringVectorDataPtr computeViewNames( const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		GafferImage::Format computeFormat( const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		Imath::Box2i computeDataWindow( const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		IECore::ConstCompoundDataPtr computeMetadata( const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		bool computeDeep( const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		IECore::ConstIntVectorDataPtr computeSampleOffsets( const Imath::V2i &tileOrigin, const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		IECore::ConstStringVectorDataPtr computeChannelNames( const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;
		IECore::ConstFloatVectorDataPtr computeChannelData( const std::string &channelName, const Imath::V2i &tileOrigin, const Gaffer::Context *context, const GafferImage::ImagePlug *parent ) const override;

	private :

		static size_t g_firstPlugIndex;
		std::unique_ptr<GafferOFX::EffectImageInstance> m_instance;


};

IE_CORE_DECLAREPTR( OFXImageNode )

} // namespace GafferOFX
