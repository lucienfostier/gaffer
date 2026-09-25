//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2025, Lucien Fostier. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above
//       copyright notice, this list of conditions and the following
//       disclaimer.
//
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided with
//       the distribution.
//
//     * Neither the name of Image Engine Design nor the names of
//       any other contributors to this software may be used to endorse or
//       promote products derived from this software without specific prior
//       written permission.
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

#include "GafferOFX/ParamAlgo.h"

#include "Gaffer/CompoundNumericPlug.h"
#include "Gaffer/Metadata.h"
#include "Gaffer/PlugAlgo.h"
#include "Gaffer/RampPlug.h"
#include "Gaffer/StringPlug.h"
#include "Gaffer/TypedPlug.h"
#include "Gaffer/ValuePlug.h"

#include "IECore/StringAlgo.h"
#include "IECore/CompoundData.h"
#include "IECore/Ramp.h"

#include <cmath>
#include <unordered_set>

using namespace Gaffer;
using namespace GafferOFX;

namespace
{

std::string sanitizeName( const std::string &name )
{
	std::string result = name;
	for( char &c : result )
	{
		if(
			!( c >= 'A' && c <= 'Z' ) &&
			!( c >= 'a' && c <= 'z' ) &&
			!( c >= '0' && c <= '9' ) &&
			c != '_' && c != ':'
		)
		{
			c = '_';
		}
	}
	return result;
}

template<typename PlugType>
Gaffer::Plug *setupTypedPlug( const std::string &paramName, Gaffer::Plug *plugParent, Gaffer::Plug::Direction direction, const typename PlugType::ValueType &defaultValue, bool keepExistingValues )
{
	PlugType *existingPlug = plugParent->getChild<PlugType>( paramName );
	if(
		existingPlug &&
		existingPlug->direction() == direction &&
		existingPlug->defaultValue() == defaultValue
	)
	{
		if( !keepExistingValues )
		{
			existingPlug->setValue( defaultValue );
		}
		return existingPlug;
	}

	typename PlugType::Ptr plug = new PlugType( paramName, direction, defaultValue );
	plug->setFlags( Gaffer::Plug::Dynamic, true );
	PlugAlgo::replacePlug( plugParent, plug );
	return plug.get();
}

void registerParameterMetadata( Gaffer::Plug *plug, const OFX::Host::Param::Descriptor &descriptor, const OFX::Host::Param::SetDescriptor *setDescriptor )
{
	const auto &props = descriptor.getProperties();
	const std::string type = descriptor.getType();

	try
	{
		std::string label = props.getStringProperty( kOfxPropLabel );
		if( !label.empty() )
		{
			Gaffer::Metadata::registerValue( plug, "label", new IECore::StringData( label ), false );
		}
	}
	catch( ... ) {}

	try
	{
		std::string hint = props.getStringProperty( kOfxParamPropHint );
		if( !hint.empty() )
		{
			Gaffer::Metadata::registerValue( plug, "description", new IECore::StringData( hint ), false );
		}
	}
	catch( ... ) {}

	try
	{
		if( props.getIntProperty( kOfxParamPropSecret ) )
		{
			Gaffer::Metadata::registerValue( plug, "nodule:type", new IECore::StringData( "" ), false );
		}
	}
	catch( ... ) {}

	try
	{
		if( !props.getIntProperty( kOfxParamPropEnabled ) )
		{
			Gaffer::Metadata::registerValue( plug, "nodule:type", new IECore::StringData( "" ), false );
		}
	}
	catch( ... ) {}

	if( type == kOfxParamTypeString )
	{
		try
		{
			std::string stringMode = props.getStringProperty( kOfxParamPropStringMode );
			if( stringMode == kOfxParamStringIsMultiLine )
			{
				Gaffer::Metadata::registerValue( plug, "plugValueWidget:type", new IECore::StringData( "GafferUI.MultiLineStringPlugValueWidget" ), false );
			}
			else if( stringMode == kOfxParamStringIsFilePath )
			{
				Gaffer::Metadata::registerValue( plug, "nodule:type", new IECore::StringData( "" ), false );
				Gaffer::Metadata::registerValue( plug, "plugValueWidget:type", new IECore::StringData( "GafferUI.FileSystemPathPlugValueWidget" ), false );
			}
		}
		catch( ... ) {}
	}

	if( type == kOfxParamTypePushButton )
	{
		Gaffer::Metadata::registerValue( plug, "plugValueWidget:type", new IECore::StringData( "GafferOFXUI.OFXImageNodeUI._PushButton" ), false );
		Gaffer::Metadata::registerValue( plug, "nodule:type", new IECore::StringData( "" ), false );
	}

	if( type == kOfxParamTypeParametric )
	{
		Gaffer::Metadata::registerValue( plug, "plugValueWidget:type", new IECore::StringData( "GafferUI.LayoutPlugValueWidget" ), false );
		Gaffer::Metadata::registerValue( plug, "nodule:type", new IECore::StringData( "" ), false );
		int dimension = 1;
		try { dimension = props.getIntProperty( kOfxParamPropParametricDimension ); } catch( ... ) {}
		if( dimension < 1 ) { dimension = 1; }
		const char *labels3[] = { "Red", "Green", "Blue" };
		const char *labels4[] = { "Red", "Green", "Blue", "Alpha" };
		const char *labels5[] = { "Master", "Red", "Green", "Blue", "Alpha" };
		const char **fallbackLabels = nullptr;
		int nFallback = 0;
		if( dimension == 5 )      { fallbackLabels = labels5; nFallback = 5; }
		else if( dimension == 4 ) { fallbackLabels = labels4; nFallback = 4; }
		else if( dimension == 3 ) { fallbackLabels = labels3; nFallback = 3; }
		for( int i = 0; i < dimension; ++i )
		{
			std::string childName = "curve" + std::to_string( i );
			Gaffer::Plug *child = plug->getChild<Gaffer::Plug>( childName );
			if( child )
			{
				std::string label;
				try { label = props.getStringProperty( kOfxParamPropDimensionLabel, i ); } catch( ... ) {}
				if( label.empty() )
				{
					label = ( i < nFallback ) ? fallbackLabels[i] : "Curve " + std::to_string( i );
				}
				Gaffer::Metadata::registerValue( child, "label", new IECore::StringData( label ), false );
				Gaffer::Metadata::registerValue( child, "plugValueWidget:type", new IECore::StringData( "GafferUI.RampPlugValueWidget" ), false );
			}
		}
	}

	if( type == kOfxParamTypeInteger || type == kOfxParamTypeDouble )
	{
		try
		{
			double min = props.getDoubleProperty( kOfxParamPropMin );
			double max = props.getDoubleProperty( kOfxParamPropMax );
			Gaffer::Metadata::registerValue( plug, "hardRange", new IECore::V2dData( Imath::V2d( min, max ) ), false );
		}
		catch( ... ) {}

		try
		{
			double min = props.getDoubleProperty( kOfxParamPropDisplayMin );
			double max = props.getDoubleProperty( kOfxParamPropDisplayMax );
			Gaffer::Metadata::registerValue( plug, "softRange", new IECore::V2dData( Imath::V2d( min, max ) ), false );
		}
		catch( ... ) {}
	}

	if( type == kOfxParamTypeChoice )
	{
		try
		{
			int numOptions = props.getDimension( kOfxParamPropChoiceOption );
			for( int i = 0; i < numOptions; ++i )
			{
				std::string optionName = props.getStringProperty( kOfxParamPropChoiceOption, i );
				Gaffer::Metadata::registerValue( plug, "preset:" + optionName, new IECore::IntData( i ), false );
			}
		}
		catch( ... ) {}

		Gaffer::Metadata::registerValue( plug, "plugValueWidget:type", new IECore::StringData( "GafferUI.PresetsPlugValueWidget" ), false );
	}

	std::string parentName;
	try { parentName = props.getStringProperty( kOfxParamPropParent ); } catch(...) {}
	if( parentName.empty() && setDescriptor )
	{
		const auto &paramMap = setDescriptor->getParams();
		const std::string &pname = plug->getName().string();
		for( const auto &[name, desc] : paramMap )
		{
			try
			{
				if( desc->getProperties().getStringProperty( kOfxParamPropType ) == kOfxParamTypePage )
				{
					int nChildren = desc->getProperties().getDimension( kOfxParamPropPageChild );
					for( int i = 0; i < nChildren; ++i )
					{
						if( desc->getProperties().getStringProperty( kOfxParamPropPageChild, i ) == pname )
						{
							parentName = name;
							break;
						}
					}
				}
			}
			catch( ... ) {}
			if( !parentName.empty() )
			{
				break;
			}
		}
	}
	if( !parentName.empty() )
	{
		std::string sectionName = parentName;
		if( setDescriptor )
		{
			const auto &paramMap = setDescriptor->getParams();
			auto it = paramMap.find( parentName );
			if( it != paramMap.end() )
			{
				try
				{
					std::string groupLabel = it->second->getProperties().getStringProperty( kOfxPropLabel );
					if( !groupLabel.empty() )
					{
						sectionName = groupLabel;
					}
				}
				catch( ... ) {}
			}
		}
		Gaffer::Metadata::registerValue( plug, "layout:section", new IECore::StringData( sectionName ), false );
	}

	{
		std::string parentName2;
		try { parentName2 = props.getStringProperty( kOfxParamPropParent ); } catch(...) {}
		if( !parentName2.empty() )
		{
			const std::string &pname = plug->getName().string();
			bool isMetadata =
				pname.compare( 0, 9, "paramType" ) == 0 ||
				pname.compare( 0, 9, "paramName" ) == 0 ||
				pname.compare( 0, 10, "paramLabel" ) == 0 ||
				pname.compare( 0, 9, "paramHint" ) == 0 ||
				pname.compare( 0, 12, "paramDefault" ) == 0 ||
				pname.compare( 0, 8, "paramMin" ) == 0 ||
				pname.compare( 0, 8, "paramMax" ) == 0 ||
				pname.compare( 0, 9, "inputHint" ) == 0 ||
				pname.compare( 0, 10, "inputLabel" ) == 0 ||
				pname.compare( 0, 9, "inputName" ) == 0 ||
				pname.compare( 0, 4, "wrap" ) == 0 ||
				pname.compare( 0, 6, "mipmap" ) == 0 ||
				pname == "bbox" ||
				pname == "startDate" ||
				pname == "NatronOfxParamStringSublabelName" ||
				pname.compare( 0, 17, "NatronParamFormat" ) == 0;
			if( isMetadata )
			{
				Gaffer::Metadata::registerValue( plug, "nodule:type", new IECore::StringData( "" ), false );
				Gaffer::Metadata::registerValue( plug, "plugValueWidget:type", new IECore::StringData( "" ), false );
			}
		}
	}
}

[[maybe_unused]] IECore::Rampff rampForCurve( const std::vector<std::vector<std::pair<double,double>>> &baseCurves, int curveIndex )
{
	IECore::Rampff result;
	result.interpolation = IECore::RampInterpolation::Linear;
	if( curveIndex >= 0 && curveIndex < (int)baseCurves.size() && !baseCurves[curveIndex].empty() )
	{
		for( const auto &cp : baseCurves[curveIndex] )
		{
			result.points.insert( IECore::Rampff::Point( (float)cp.first, (float)cp.second ) );
		}
	}
	if( result.points.empty() )
	{
		result.points.insert( IECore::Rampff::Point( 0.0f, 0.0f ) );
		result.points.insert( IECore::Rampff::Point( 1.0f, 1.0f ) );
	}
	return result;
}

} // namespace

namespace GafferOFX
{

namespace ParamAlgo
{

std::string plugName( const OFX::Host::Param::Descriptor &descriptor )
{
	return sanitizeName( descriptor.getName() );
}

std::string plugName( const std::string &paramName )
{
	return sanitizeName( paramName );
}

Gaffer::Plug *setupPlug( const OFX::Host::Param::Descriptor &descriptor, Gaffer::Plug *parent, bool keepExistingValues )
{
	const std::string type = descriptor.getType();
	const std::string name = plugName( descriptor );
	const auto &props = descriptor.getProperties();

	if( type == kOfxParamTypeInteger )
	{
		int defaultValue = 0;
		try { defaultValue = props.getIntProperty( kOfxParamPropDefault ); } catch( ... ) {}
		auto *plug = setupTypedPlug<IntPlug>( name, parent, Plug::In, defaultValue, keepExistingValues );
		// register metadata after ensuring plug exists
		if( plug )
		{
			// need setDescriptor for section; we don't have it here, caller will handle section via setupPlugs second pass
			registerParameterMetadata( plug, descriptor, nullptr );
		}
		return plug;
	}
	else if( type == kOfxParamTypeDouble )
	{
		double defaultValue = 0.0;
		try { defaultValue = props.getDoubleProperty( kOfxParamPropDefault ); } catch( ... ) {}
		auto *plug = setupTypedPlug<FloatPlug>( name, parent, Plug::In, (float)defaultValue, keepExistingValues );
		if( plug ) registerParameterMetadata( plug, descriptor, nullptr );
		return plug;
	}
	else if( type == kOfxParamTypeBoolean )
	{
		bool defaultValue = false;
		try { defaultValue = props.getIntProperty( kOfxParamPropDefault ) != 0; } catch( ... ) {}
		auto *plug = setupTypedPlug<BoolPlug>( name, parent, Plug::In, defaultValue, keepExistingValues );
		if( plug ) registerParameterMetadata( plug, descriptor, nullptr );
		return plug;
	}
	else if( type == kOfxParamTypeChoice )
	{
		int defaultValue = 0;
		try { defaultValue = props.getIntProperty( kOfxParamPropDefault ); } catch( ... ) {}
		auto *plug = setupTypedPlug<IntPlug>( name, parent, Plug::In, defaultValue, keepExistingValues );
		if( plug ) registerParameterMetadata( plug, descriptor, nullptr );
		return plug;
	}
	else if( type == kOfxParamTypeRGBA )
	{
		Imath::Color4f defaultValue(0.0f, 0.0f, 0.0f, 1.0f);
		try {
			double vals[4];
			props.getDoublePropertyN(kOfxParamPropDefault, vals, 4);
			defaultValue = Imath::Color4f((float)vals[0], (float)vals[1], (float)vals[2], (float)vals[3]);
		} catch (...) {}
		auto *plug = setupTypedPlug<Color4fPlug>( name, parent, Plug::In, defaultValue, keepExistingValues );
		if( plug ) registerParameterMetadata( plug, descriptor, nullptr );
		return plug;
	}
	else if( type == kOfxParamTypeRGB )
	{
		Imath::Color3f defaultValue( 0.0f );
		try {
			defaultValue.x = props.getDoubleProperty( kOfxParamPropDefault, 0 );
			defaultValue.y = props.getDoubleProperty( kOfxParamPropDefault, 1 );
			defaultValue.z = props.getDoubleProperty( kOfxParamPropDefault, 2 );
		} catch( ... ) {}
		auto *plug = setupTypedPlug<Color3fPlug>( name, parent, Plug::In, defaultValue, keepExistingValues );
		if( plug ) registerParameterMetadata( plug, descriptor, nullptr );
		return plug;
	}
	else if( type == kOfxParamTypeDouble2D )
	{
		Imath::V2f defaultValue( 0.0f );
		try {
			defaultValue.x = props.getDoubleProperty( kOfxParamPropDefault, 0 );
			defaultValue.y = props.getDoubleProperty( kOfxParamPropDefault, 1 );
		} catch( ... ) {}
		auto *plug = setupTypedPlug<V2fPlug>( name, parent, Plug::In, defaultValue, keepExistingValues );
		if( plug ) registerParameterMetadata( plug, descriptor, nullptr );
		return plug;
	}
	else if( type == kOfxParamTypeInteger2D )
	{
		Imath::V2i defaultValue( 0 );
		try {
			defaultValue.x = props.getIntProperty( kOfxParamPropDefault, 0 );
			defaultValue.y = props.getIntProperty( kOfxParamPropDefault, 1 );
		} catch( ... ) {}
		auto *plug = setupTypedPlug<V2iPlug>( name, parent, Plug::In, defaultValue, keepExistingValues );
		if( plug ) registerParameterMetadata( plug, descriptor, nullptr );
		return plug;
	}
	else if( type == kOfxParamTypeDouble3D )
	{
		Imath::V3f defaultValue( 0.0f );
		try {
			defaultValue.x = props.getDoubleProperty( kOfxParamPropDefault, 0 );
			defaultValue.y = props.getDoubleProperty( kOfxParamPropDefault, 1 );
			defaultValue.z = props.getDoubleProperty( kOfxParamPropDefault, 2 );
		} catch( ... ) {}
		auto *plug = setupTypedPlug<V3fPlug>( name, parent, Plug::In, defaultValue, keepExistingValues );
		if( plug ) registerParameterMetadata( plug, descriptor, nullptr );
		return plug;
	}
	else if( type == kOfxParamTypeInteger3D )
	{
		Imath::V3i defaultValue( 0 );
		try {
			defaultValue.x = props.getIntProperty( kOfxParamPropDefault, 0 );
			defaultValue.y = props.getIntProperty( kOfxParamPropDefault, 1 );
			defaultValue.z = props.getIntProperty( kOfxParamPropDefault, 2 );
		} catch( ... ) {}
		auto *plug = setupTypedPlug<V3iPlug>( name, parent, Plug::In, defaultValue, keepExistingValues );
		if( plug ) registerParameterMetadata( plug, descriptor, nullptr );
		return plug;
	}
	else if( type == kOfxParamTypeString || type == kOfxParamTypeCustom )
	{
		std::string defaultValue;
		try { defaultValue = props.getStringProperty( kOfxParamPropDefault ); } catch( ... ) {}
		StringPlug *existingPlug = parent->getChild<StringPlug>( name );
		if(
			existingPlug &&
			existingPlug->direction() == Plug::In &&
			existingPlug->defaultValue() == defaultValue
		)
		{
			if( !keepExistingValues )
			{
				existingPlug->setValue( defaultValue );
			}
			registerParameterMetadata( existingPlug, descriptor, nullptr );
			return existingPlug;
		}
		StringPlug::Ptr plug = new StringPlug( name, Plug::In, defaultValue, Plug::Default, IECore::StringAlgo::NoSubstitutions );
		plug->setFlags( Gaffer::Plug::Dynamic, true );
		PlugAlgo::replacePlug( parent, plug );
		registerParameterMetadata( plug.get(), descriptor, nullptr );
		return plug.get();
	}
	else if( type == kOfxParamTypePushButton )
	{
		auto *plug = setupTypedPlug<BoolPlug>( name, parent, Plug::In, false, keepExistingValues );
		if( plug ) registerParameterMetadata( plug, descriptor, nullptr );
		return plug;
	}
	else if( type == kOfxParamTypeParametric )
	{
#ifdef OFX_SUPPORTS_PARAMETRIC
		int dimension = 1;
		try { dimension = props.getIntProperty( kOfxParamPropParametricDimension ); } catch( ... ) {}
		if( dimension < 1 ) { dimension = 1; }

		// Query described curves via HostSupport (populated during
		// DescribeInContext). Falls back to identity when absent.
		std::vector<std::vector<std::pair<double,double>>> describedCurves;
		bool haveDescribed = false;
		try
		{
			haveDescribed = OFX::Host::Param::getDescriptorCurves( &descriptor, describedCurves );
		}
		catch( ... ) { haveDescribed = false; }

		auto defaultForCurve = [&]( int idx ) -> IECore::Rampff
		{
			return rampForCurve( haveDescribed ? describedCurves : std::vector<std::vector<std::pair<double,double>>>(), idx );
		};

		// Validate existing plug: dimension + per-curve defaults
		Gaffer::ValuePlug *existingPlug = parent->getChild<Gaffer::ValuePlug>( name );
		if( existingPlug && existingPlug->direction() == Plug::In )
		{
			int childCount = 0;
			for( int i = 0; ; ++i )
			{
				if( !existingPlug->getChild<Gaffer::RampffPlug>( "curve" + std::to_string( i ) ) )
				{
					break;
				}
				++childCount;
			}
			if( childCount == dimension )
			{
				bool defaultsMatch = true;
				for( int i = 0; i < dimension; ++i )
				{
					auto *child = existingPlug->getChild<Gaffer::RampffPlug>( "curve" + std::to_string( i ) );
					if( !child || child->defaultValue() != defaultForCurve( i ) )
					{
						defaultsMatch = false;
						break;
					}
				}
				if( defaultsMatch )
				{
					if( !keepExistingValues )
					{
						for( int i = 0; i < dimension; ++i )
						{
							auto *child = existingPlug->getChild<Gaffer::RampffPlug>( "curve" + std::to_string( i ) );
							if( child )
							{
								child->setValue( child->defaultValue() );
							}
						}
					}
					registerParameterMetadata( existingPlug, descriptor, nullptr );
					return existingPlug;
				}
				else if( keepExistingValues )
				{
					IECore::msg( IECore::Msg::Warning, "GafferOFX::ParamAlgo",
						"Parametric defaults changed for \"" + name + "\" — replacing plug, user values reset" );
				}
			}
		}

		Gaffer::ValuePlug::Ptr parentPlug = new Gaffer::ValuePlug( name, Plug::In, Plug::Default | Plug::Dynamic );
		for( int i = 0; i < dimension; ++i )
		{
			IECore::Rampff def = defaultForCurve( i );
			Gaffer::RampffPlug::Ptr ramp = new Gaffer::RampffPlug( "curve" + std::to_string( i ), Plug::In, def, Gaffer::Plug::Default | Gaffer::Plug::Dynamic );
			parentPlug->addChild( ramp );
		}
		PlugAlgo::replacePlug( parent, parentPlug );
		registerParameterMetadata( parentPlug.get(), descriptor, nullptr );
		return parentPlug.get();
#else
		return nullptr;
#endif
	}
	else if( type == kOfxParamTypeGroup || type == kOfxParamTypePage )
	{
		// Groups/pages are layout:section metadata, not plugs
		return nullptr;
	}
	else
	{
		IECore::msg( IECore::Msg::Level::Warning, "GafferOFX::ParamAlgo", "Unhandled OFX parameter type \"" + type + "\" for \"" + name + "\"" );
		return nullptr;
	}
}

void setupPlugs( const OFX::Host::Param::SetDescriptor &setDescriptor, Gaffer::Plug *parent, bool keepExistingValues )
{
	// First pass: create/reconcile each param plug in plugin declaration
	// order (getParamList preserves Describe order; getParams map is alphabetical).
	std::unordered_set<std::string> validNames;
	for( auto *desc : setDescriptor.getParamList() )
	{
		if( !desc )
		{
			continue;
		}
		// Group/Page don't create plugs, skip adding to validNames
		if( desc->getType() == kOfxParamTypeGroup || desc->getType() == kOfxParamTypePage )
		{
			continue;
		}
		Gaffer::Plug *plug = setupPlug( *desc, parent, keepExistingValues );
		if( plug )
		{
			validNames.insert( plug->getName().string() );
			// Second call to register section metadata with full setDescriptor
			registerParameterMetadata( plug, *desc, &setDescriptor );
		}
		else
		{
			// For types that didn't get section via setupPlug (because we passed nullptr), register now
			// Find plug if it already existed? No, skip.
		}
	}

	// Remove stale plugs not in descriptor (e.g. legacy ObjectPlug
	// lookupTable from saved scripts). Loud warning naming parent +
	// param so user edits aren't silently lost.
	std::vector<Gaffer::Plug*> toRemove;
	for( Gaffer::Plug::Iterator it( parent ); !it.done(); ++it )
	{
		Gaffer::Plug *child = it->get();
		// Only consider dynamic plugs under parameters
		if( !( child->getFlags() & Gaffer::Plug::Dynamic ) )
		{
			continue;
		}
		if( validNames.find( child->getName().string() ) == validNames.end() )
		{
			toRemove.push_back( child );
		}
	}
	for( auto *plug : toRemove )
	{
		std::string parentPath = ".";
		if( const Gaffer::Node *node = parent->ancestor<Gaffer::Node>() )
		{
			parentPath = node->getName().string() + "." + parent->getName().string();
		}
		IECore::msg( IECore::Msg::Warning, "GafferOFX::ParamAlgo",
			"Removing stale param plug \"" + parentPath + "." + plug->getName().string() +
			"\" (" + std::string( plug->typeName() ) + ") not in plugin descriptor" );
		parent->removeChild( plug );
	}
}

} // namespace ParamAlgo

} // namespace GafferOFX
