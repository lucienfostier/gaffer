//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2013, John Haddon. All rights reserved.
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

#include "GafferOSL/Export.h"
#include "GafferOSL/OSLCode.h"
#include "GafferOSL/TypeIds.h"

#include "GafferScene/Deformer.h"
#include "GafferScene/ShaderPlug.h"

#include "Gaffer/NumericPlug.h"
#include "Gaffer/StringPlug.h"

namespace GafferOSL
{

IE_CORE_FORWARDDECLARE( ShadingEngine )

class GAFFEROSL_API OSLVDB : public GafferScene::Deformer
{

	public :

		explicit OSLVDB( const std::string &name=defaultName<OSLVDB>() );
		~OSLVDB() override;

		GAFFER_NODE_DECLARE_TYPE( GafferOSL::OSLVDB, OSLVDBTypeId, GafferScene::Deformer );

		Gaffer::Plug *gridsPlug();
		const Gaffer::Plug *gridsPlug() const;

	protected :

		bool affectsProcessedObject( const Gaffer::Plug *input ) const override;
		void hashProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const override;
		IECore::ConstObjectPtr computeProcessedObject( const ScenePath &path, const Gaffer::Context *context, const IECore::Object *inputObject ) const override;
		Gaffer::ValuePlug::CachePolicy processedObjectComputeCachePolicy() const override;
		bool adjustBounds() const override;

	private :

		GafferScene::ShaderPlug *shaderPlug();
		const GafferScene::ShaderPlug *shaderPlug() const;

		ConstShadingEnginePtr shadingEngine( const Gaffer::Context *context, const IECore::CompoundObject *substitutions ) const;

		GafferOSL::OSLCode *oslCode();
		const GafferOSL::OSLCode *oslCode() const;

		void gridAdded( const Gaffer::GraphComponent *parent, Gaffer::GraphComponent *child );
		void gridRemoved( const Gaffer::GraphComponent *parent, Gaffer::GraphComponent *child );

		void updatePrimitiveVariables();

		static size_t g_firstPlugIndex;

};

} // namespace GafferOSL
