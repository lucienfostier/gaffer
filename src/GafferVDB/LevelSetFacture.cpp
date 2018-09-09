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

#include "GafferVDB/LevelSetFracture.h"

#include "IECore/StringAlgo.h"

#include "IECoreScene/PointsPrimitive.h"

#include "IECoreVDB/VDBObject.h"

#include "Gaffer/StringPlug.h"
#include "GafferScene/ScenePlug.h"

#include "openvdb/openvdb.h"
#include "openvdb/tools/GridOperators.h"
#include "openvdb/tools/LevelSetFracture.h"


using namespace std;
using namespace Imath;
using namespace IECore;
using namespace IECoreVDB;
using namespace Gaffer;
using namespace GafferScene;
using namespace GafferVDB;

IE_CORE_DEFINERUNTIMETYPED( LevelSetFracture );

size_t LevelSetFracture::g_firstPlugIndex = 0;

LevelSetFracture::LevelSetFracture( const std::string &name )
		: SceneElementProcessor( name, IECore::PathMatcher::NoMatch )
{
	storeIndexOfNextChild( g_firstPlugIndex );

	addChild ( new StringPlug( "grids", Plug::In, "*" ) );

	outPlug()->boundPlug()->setInput( inPlug()->boundPlug() );
	outPlug()->objectPlug()->setInput( inPlug()->objectPlug() );
}

LevelSetFracture::~LevelSetFracture()
{
}

Gaffer::StringPlug *LevelSetFracture::gridsPlug()
{
	return getChild<StringPlug>( g_firstPlugIndex );
}

const Gaffer::StringPlug *LevelSetFracture::gridsPlug() const
{
	return getChild<const StringPlug>( g_firstPlugIndex );
}

void LevelSetFracture::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	SceneElementProcessor::affects( input, outputs );

	if ( input == gridsPlug()  )
	{
		outputs.push_back( outPlug()->attributesPlug() );
	}
}

bool LevelSetFracture::processesObject() const
{
	return true;
}

void LevelSetFracture::hashProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	SceneElementProcessor::hashProcessedAttributes( path, context, h );

	h.append( inPlug()->objectHash( path ) );
	h.append( gridsPlug()->getValue() );
}

IECore::ConstObjectPtr LevelSetFracture::computeProcessedObject( const ScenePath &path, const Gaffer::Context *context, IECore::ConstObjectPtr inputObject ) const
{
	return inputObject;
}


