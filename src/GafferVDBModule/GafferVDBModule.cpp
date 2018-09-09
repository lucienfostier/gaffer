//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2017, Image Engine Design. All rights reserved.
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
//      * Neither the name of Image Engine Design nor the names of
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

#include "boost/python.hpp"

#include "GafferVDB/LevelSetOffset.h"
#include "GafferVDB/LevelSetToMesh.h"
#include "GafferVDB/MeshToLevelSet.h"
#include "GafferVDB/PointsGridToPoints.h"
#include "GafferVDB/DeleteGrids.h"
#include "GafferVDB/ScatterPoints.h"
#include "GafferVDB/AdvectGrids.h"
#include "GafferVDB/MathOp.h"
#include "GafferVDB/Statistics.h"
#include "GafferVDB/CsgGrids.h"
#include "GafferVDB/TransformGrids.h"
#include "GafferVDB/PointsToLevelSet.h"
#include "GafferVDB/VDBObject.h"
#include "GafferVDB/Sample.h"
#include "GafferVDB/FilterGrids.h"
#include "GafferVDB/LevelSetMeasure.h"
#include "GafferVDB/LevelSetFilter.h"
#include "GafferVDB/VolumeToSpheres.h"
#include "GafferVDB/Clip.h"
#include "GafferVDB/LevelSetToFog.h"


#include "GafferBindings/DependencyNodeBinding.h"

using namespace boost::python;
using namespace GafferVDB;

BOOST_PYTHON_MODULE( _GafferVDB )
{

	GafferBindings::DependencyNodeClass<MeshToLevelSet>();
	GafferBindings::DependencyNodeClass<LevelSetToMesh>();
	GafferBindings::DependencyNodeClass<LevelSetOffset>();
	GafferBindings::DependencyNodeClass<PointsGridToPoints>();
	GafferBindings::DependencyNodeClass<DeleteGrids>();
	GafferBindings::DependencyNodeClass<ScatterPoints>();
    GafferBindings::DependencyNodeClass<AdvectGrids>();
	GafferBindings::DependencyNodeClass<MathOp>();
	GafferBindings::DependencyNodeClass<Statistics>();
	GafferBindings::DependencyNodeClass<CSGGrids>();
	GafferBindings::DependencyNodeClass<TransformGrids>();
	GafferBindings::DependencyNodeClass<PointsToLevelSet>();
	GafferBindings::DependencyNodeClass<VDBObject>();
	GafferBindings::DependencyNodeClass<Sample>();
	GafferBindings::DependencyNodeClass<FilterGrids>();
	GafferBindings::DependencyNodeClass<LevelSetMeasure>();
	GafferBindings::DependencyNodeClass<LevelSetFilter>();
	GafferBindings::DependencyNodeClass<VolumeToSpheres>();
	GafferBindings::DependencyNodeClass<Clip>();
	GafferBindings::DependencyNodeClass<LevelSetToFog>();
}
