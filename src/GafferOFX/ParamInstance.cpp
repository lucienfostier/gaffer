//////////////////////////////////////////////////////////////////////////
//
//  Copyright ( c ) 2025, Lucien Fostier. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of Image Engine Design nor the names of any
//       other contributors to this software may be used to endorse or
//       promote products derived from this software without specific prior
//       written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES ( INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION ) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT ( INCLUDING
//  NEGLIGENCE OR OTHERWISE ) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////
#include "GafferOFX/ParamInstance.h"
#include "GafferOFX/OFXImageNode.h"

#include "Gaffer/CompoundNumericPlug.h"
#include "Gaffer/PlugAlgo.h"
#include "Gaffer/StringPlug.h"
#include "Gaffer/TypedPlug.h"

using namespace Gaffer;
using namespace GafferOFX;

namespace
{

template<typename PlugType>
Gaffer::Plug *setupTypedPlug( const IECore::InternedString &parameterName, Gaffer::GraphComponent *plugParent, Gaffer::Plug::Direction direction, const typename PlugType::ValueType &defaultValue )
{
	PlugType *existingPlug = plugParent->getChild<PlugType>( parameterName );
	if(
		existingPlug &&
		existingPlug->direction() == direction &&
		existingPlug->defaultValue() == defaultValue
	)
	{
		return existingPlug;
	}

	typename PlugType::Ptr plug = new PlugType( parameterName, direction, defaultValue );

	plug->setFlags( Gaffer::Plug::Dynamic, true );
	PlugAlgo::replacePlug( plugParent, plug );

	return plug.get();
}
}

IntegerInstance::IntegerInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::IntegerInstance( descriptor ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<IntPlug>( name, plugParent, Plug::In, 0 );
}

OfxStatus IntegerInstance::get( int& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus IntegerInstance::get( OfxTime time, int& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus IntegerInstance::set( int )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus IntegerInstance::set( OfxTime time, int )
{
	return kOfxStatErrMissingHostFeature;
}

GafferOFX::DoubleInstance::DoubleInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::DoubleInstance( descriptor ), m_effect( effect ), m_descriptor( descriptor )
{

	std::cout << "setup gaffer plug for double instance" << std::endl;
	// TODO clarify constness
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<FloatPlug>( name, plugParent, Plug::In, 0.0f );
}

OfxStatus DoubleInstance::get( double& d )
{
	d = 2.0;
	return kOfxStatOK;
}

OfxStatus DoubleInstance::get( OfxTime time, double& d )
{
	d = 2.0;
	return kOfxStatOK;
}

OfxStatus DoubleInstance::set( double )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus DoubleInstance::set( OfxTime time, double ) 
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus DoubleInstance::derive( OfxTime time, double& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus DoubleInstance::integrate( OfxTime time1, OfxTime time2, double& )
{
	return kOfxStatErrMissingHostFeature;
}

GafferOFX::BooleanInstance::BooleanInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::BooleanInstance( descriptor ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<BoolPlug>( name, plugParent, Plug::In, false );
}

OfxStatus BooleanInstance::get( bool& b )
{
	b = true;
	return kOfxStatOK;
}

OfxStatus BooleanInstance::get( OfxTime time, bool& b )
{
	b = true;
	return kOfxStatOK;
}

OfxStatus BooleanInstance::set( bool )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus BooleanInstance::set( OfxTime time, bool )
{
	return kOfxStatErrMissingHostFeature;
}

GafferOFX::ChoiceInstance::ChoiceInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::ChoiceInstance( descriptor ), m_effect( effect ), m_descriptor( descriptor )
{
}

OfxStatus ChoiceInstance::get( int& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus ChoiceInstance::get( OfxTime time, int& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus ChoiceInstance::set( int )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus ChoiceInstance::set( OfxTime time, int ) 
{
	return kOfxStatErrMissingHostFeature;
}

GafferOFX::RGBAInstance::RGBAInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::RGBAInstance( descriptor ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<Color4fPlug>( name, plugParent, Plug::In, Imath::Color4f() );
}

OfxStatus RGBAInstance::get( double&, double&, double&, double& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus RGBAInstance::get( OfxTime time, double&,double&,double&,double& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus RGBAInstance::set( double, double, double, double )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus RGBAInstance::set( OfxTime time, double, double, double, double )
{
	return kOfxStatErrMissingHostFeature;
}

GafferOFX::RGBInstance::RGBInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::RGBInstance( descriptor ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<Color3fPlug>( name, plugParent, Plug::In, Imath::Color3f() );
}

OfxStatus RGBInstance::get( double&, double&, double& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus RGBInstance::get( OfxTime time, double&, double&, double& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus RGBInstance::set( double, double, double )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus RGBInstance::set( OfxTime time, double, double, double )
{
	return kOfxStatErrMissingHostFeature;
}

GafferOFX::Double2DInstance::Double2DInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::Double2DInstance( descriptor ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<V2fPlug>( name, plugParent, Plug::In, Imath::V2f() );
}

OfxStatus Double2DInstance::get( double&, double& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus Double2DInstance::get( OfxTime time, double&, double& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus Double2DInstance::set( double, double )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus Double2DInstance::set( OfxTime time, double, double )
{
	return kOfxStatErrMissingHostFeature;
}

GafferOFX::Integer2DInstance::Integer2DInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::Integer2DInstance( descriptor ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<V2iPlug>( name, plugParent, Plug::In, Imath::V2i() );

}

OfxStatus Integer2DInstance::get( int&, int& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus Integer2DInstance::get( OfxTime time, int&, int& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus Integer2DInstance::set( int, int )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus Integer2DInstance::set( OfxTime time, int, int )
{
	return kOfxStatErrMissingHostFeature;
}

GafferOFX::Double3DInstance::Double3DInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::Double3DInstance( descriptor ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<V3fPlug>( name, plugParent, Plug::In, Imath::V3f() );
}

OfxStatus Double3DInstance::get( double&, double&, double& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus Double3DInstance::get( OfxTime time, double&, double&, double& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus Double3DInstance::set( double, double, double )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus Double3DInstance::set( OfxTime time, double, double, double )
{
	return kOfxStatErrMissingHostFeature;
}

GafferOFX::Integer3DInstance::Integer3DInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::Integer3DInstance( descriptor ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<V3iPlug>( name, plugParent, Plug::In, Imath::V3i() );

}

OfxStatus Integer3DInstance::get( int&, int&, int& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus Integer3DInstance::get( OfxTime time, int&, int&, int& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus Integer3DInstance::set( int, int, int )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus Integer3DInstance::set( OfxTime time, int, int, int )
{
	return kOfxStatErrMissingHostFeature;
}

PushbuttonInstance::PushbuttonInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::PushbuttonInstance( descriptor ), m_effect( effect ), m_descriptor( descriptor ) 
{
}

GafferOFX::StringInstance::StringInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::StringInstance( descriptor ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<StringPlug>( name, plugParent, Plug::In, "" );

}

OfxStatus StringInstance::get( std::string& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus StringInstance::get( OfxTime time, std::string& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus StringInstance::set( const char* )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus StringInstance::set( OfxTime time, const char* )
{
	return kOfxStatErrMissingHostFeature;
}


