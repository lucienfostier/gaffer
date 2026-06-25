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

#include <cstdio>
#include "GafferOFX/OFXImageNode.h"

#include "Gaffer/CompoundNumericPlug.h"
#include "Gaffer/PlugAlgo.h"
#include "Gaffer/StringPlug.h"
#include "Gaffer/TypedPlug.h"

using namespace Gaffer;
using namespace GafferOFX;

namespace
{

// Replace characters invalid for Gaffer plug names with underscores.
// Gaffer::GraphComponent only allows A-Za-z0-9_: in names.
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
Gaffer::Plug *setupTypedPlug( const IECore::InternedString &parameterName_, Gaffer::GraphComponent *plugParent, Gaffer::Plug::Direction direction, const typename PlugType::ValueType &defaultValue )
{
	const std::string parameterName = sanitizeName( parameterName_.string() );
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

} // namespace

IntegerInstance::IntegerInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::IntegerInstance( descriptor, effect ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	int defaultValue = 0;
	try { defaultValue = descriptor.getProperties().getIntProperty( kOfxParamPropDefault ); } catch( ... ) {}
	setupTypedPlug<IntPlug>( name, plugParent, Plug::In, defaultValue );
}

OfxStatus IntegerInstance::get( int& i )
{
	auto* node = static_cast<const OFXImageNode*>( m_effect->node() );
	auto* plug = node->parametersPlug()->getChild<IntPlug>( m_descriptor.getName() );
	if( plug )
	{
		i = plug->getValue();
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus IntegerInstance::get( OfxTime time, int& i )
{
	return get( i );
}

OfxStatus IntegerInstance::set( int value )
{
	auto* node = const_cast<OFXImageNode*>( static_cast<const OFXImageNode*>( m_effect->node() ) );
	auto* plug = node->parametersPlug()->getChild<IntPlug>( m_descriptor.getName() );
	if( plug )
	{
		plug->setValue( value );
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus IntegerInstance::set( OfxTime time, int value )
{
	return set( value );
}

GafferOFX::DoubleInstance::DoubleInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::DoubleInstance( descriptor, effect ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	double defaultValue = 0.0;
	try { defaultValue = descriptor.getProperties().getDoubleProperty( kOfxParamPropDefault ); } catch( ... ) {}
	setupTypedPlug<FloatPlug>( name, plugParent, Plug::In, (float)defaultValue );
}

OfxStatus DoubleInstance::get( double& d )
{
	auto* node = static_cast<const OFXImageNode*>( m_effect->node() );
	auto* plug = node->parametersPlug()->getChild<FloatPlug>( m_descriptor.getName() );
	if( plug )
	{
		d = plug->getValue();
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus DoubleInstance::get( OfxTime time, double& d )
{
	return get( d );
}

OfxStatus DoubleInstance::set( double value )
{
	auto* node = const_cast<OFXImageNode*>( static_cast<const OFXImageNode*>( m_effect->node() ) );
	auto* plug = node->parametersPlug()->getChild<FloatPlug>( m_descriptor.getName() );
	if( plug )
	{
		plug->setValue( (float)value );
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus DoubleInstance::set( OfxTime time, double value ) 
{
	return set( value );
}

OfxStatus DoubleInstance::derive( OfxTime time, double& )
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus DoubleInstance::integrate( OfxTime time1, OfxTime time2, double& )
{
	return kOfxStatErrMissingHostFeature;
}

GafferOFX::BooleanInstance::BooleanInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::BooleanInstance( descriptor, effect ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	bool defaultValue = false;
	try { defaultValue = descriptor.getProperties().getIntProperty( kOfxParamPropDefault ) != 0; } catch( ... ) {}
	setupTypedPlug<BoolPlug>( name, plugParent, Plug::In, defaultValue );
}

OfxStatus BooleanInstance::get( bool& b )
{
	auto* node = static_cast<const OFXImageNode*>( m_effect->node() );
	auto* plug = node->parametersPlug()->getChild<BoolPlug>( m_descriptor.getName() );
	if( plug )
	{
		b = plug->getValue();
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus BooleanInstance::get( OfxTime time, bool& b )
{
	return get( b );
}

OfxStatus BooleanInstance::set( bool v )
{
	auto* node = const_cast<OFXImageNode*>( static_cast<const OFXImageNode*>( m_effect->node() ) );
	auto* plug = node->parametersPlug()->getChild<BoolPlug>( m_descriptor.getName() );
	if( plug )
	{
		plug->setValue( v );
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus BooleanInstance::set( OfxTime time, bool v )
{
	return set( v );
}

GafferOFX::ChoiceInstance::ChoiceInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::ChoiceInstance( descriptor, effect ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	int defaultValue = 0;
	try
	{
		defaultValue = descriptor.getProperties().getIntProperty( kOfxParamPropDefault );
	}
	catch( ... )
	{
	}
	setupTypedPlug<IntPlug>( name, plugParent, Plug::In, defaultValue );
}

OfxStatus ChoiceInstance::get( int& i )
{
	auto* node = static_cast<const OFXImageNode*>( m_effect->node() );
	auto* plug = node->parametersPlug()->getChild<IntPlug>( m_descriptor.getName() );
	if( plug )
	{
		i = plug->getValue();
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus ChoiceInstance::get( OfxTime time, int& i )
{
	return get( i );
}

OfxStatus ChoiceInstance::set( int value )
{
	auto* node = const_cast<OFXImageNode*>( static_cast<const OFXImageNode*>( m_effect->node() ) );
	auto* plug = node->parametersPlug()->getChild<IntPlug>( m_descriptor.getName() );
	if( plug )
	{
		plug->setValue( value );
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus ChoiceInstance::set( OfxTime time, int value ) 
{
	return set( value );
}

GafferOFX::RGBAInstance::RGBAInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::RGBAInstance( descriptor, effect ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	Imath::Color4f defaultValue(0.0f, 0.0f, 0.0f, 1.0f);
	try {
		double vals[4];
		descriptor.getProperties().getDoublePropertyN(kOfxParamPropDefault, vals, 4);
		defaultValue = Imath::Color4f(static_cast<float>(vals[0]), static_cast<float>(vals[1]), static_cast<float>(vals[2]), static_cast<float>(vals[3]));
	} catch (...) {}
	setupTypedPlug<Color4fPlug>( name, plugParent, Plug::In, defaultValue );
}

OfxStatus RGBAInstance::get( double& r, double& g, double& b, double& a )
{
	auto* node = static_cast<const OFXImageNode*>( m_effect->node() );
	auto* plug = node->parametersPlug()->getChild<Color4fPlug>( m_descriptor.getName() );
	if( plug )
	{
		Imath::Color4f c = plug->getValue();
		r = c.r; g = c.g; b = c.b; a = c.a;
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus RGBAInstance::get( OfxTime time, double& r, double& g, double& b, double& a )
{
	return get( r, g, b, a );
}

OfxStatus RGBAInstance::set( double r, double g, double b, double a )
{
	auto* node = const_cast<OFXImageNode*>( static_cast<const OFXImageNode*>( m_effect->node() ) );
	auto* plug = node->parametersPlug()->getChild<Color4fPlug>( m_descriptor.getName() );
	if( plug )
	{
		plug->setValue( Imath::Color4f( (float)r, (float)g, (float)b, (float)a ) );
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus RGBAInstance::set( OfxTime time, double r, double g, double b, double a )
{
	return set( r, g, b, a );
}

GafferOFX::RGBInstance::RGBInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::RGBInstance( descriptor, effect ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<Color3fPlug>( name, plugParent, Plug::In, Imath::Color3f() );
}

OfxStatus RGBInstance::get( double& r, double& g, double& b )
{
	auto* node = static_cast<const OFXImageNode*>( m_effect->node() );
	auto* plug = node->parametersPlug()->getChild<Color3fPlug>( m_descriptor.getName() );
	if( plug )
	{
		Imath::Color3f c = plug->getValue();
		r = c.x; g = c.y; b = c.z;
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus RGBInstance::get( OfxTime time, double& r, double& g, double& b )
{
	return get( r, g, b );
}

OfxStatus RGBInstance::set( double r, double g, double b )
{
	auto* node = const_cast<OFXImageNode*>( static_cast<const OFXImageNode*>( m_effect->node() ) );
	auto* plug = node->parametersPlug()->getChild<Color3fPlug>( m_descriptor.getName() );
	if( plug )
	{
		plug->setValue( Imath::Color3f( (float)r, (float)g, (float)b ) );
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus RGBInstance::set( OfxTime time, double r, double g, double b )
{
	return set( r, g, b );
}

GafferOFX::Double2DInstance::Double2DInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::Double2DInstance( descriptor, effect ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<V2fPlug>( name, plugParent, Plug::In, Imath::V2f() );
}

OfxStatus Double2DInstance::get( double& x, double& y )
{
	auto* node = static_cast<const OFXImageNode*>( m_effect->node() );
	auto* plug = node->parametersPlug()->getChild<V2fPlug>( m_descriptor.getName() );
	if( plug )
	{
		Imath::V2f v = plug->getValue();
		x = v.x; y = v.y;
		fprintf( stderr, "DBG get(%s) =(%.4f,%.4f)\n", m_descriptor.getName().c_str(), x, y );
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus Double2DInstance::get( OfxTime time, double& x, double& y )
{
	return get( x, y );
}

OfxStatus Double2DInstance::set( double x, double y )
{
	auto* node = const_cast<OFXImageNode*>( static_cast<const OFXImageNode*>( m_effect->node() ) );
	auto* plug = node->parametersPlug()->getChild<V2fPlug>( m_descriptor.getName() );
	fprintf( stderr, "DBG set(%s, %.4f, %.4f) plug=%p\n", m_descriptor.getName().c_str(), x, y, (void*)plug );
	if( plug )
	{
		Imath::V2f before = plug->getValue();
		plug->setValue( Imath::V2f( (float)x, (float)y ) );
		Imath::V2f after = plug->getValue();
		fprintf( stderr, "DBG set(%s) before=(%.4f,%.4f) after=(%.4f,%.4f)\n",
			m_descriptor.getName().c_str(), before.x, before.y, after.x, after.y );
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus Double2DInstance::set( OfxTime time, double x, double y )
{
	fprintf( stderr, "DBG set(%s, t=%.3f, %.4f, %.4f) -> time variant\n",
		m_descriptor.getName().c_str(), time, x, y );
	return set( x, y );
}

GafferOFX::Integer2DInstance::Integer2DInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::Integer2DInstance( descriptor, effect ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<V2iPlug>( name, plugParent, Plug::In, Imath::V2i() );
}

OfxStatus Integer2DInstance::get( int& x, int& y )
{
	auto* node = static_cast<const OFXImageNode*>( m_effect->node() );
	auto* plug = node->parametersPlug()->getChild<V2iPlug>( m_descriptor.getName() );
	if( plug )
	{
		Imath::V2i v = plug->getValue();
		x = v.x; y = v.y;
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus Integer2DInstance::get( OfxTime time, int& x, int& y )
{
	return get( x, y );
}

OfxStatus Integer2DInstance::set( int x, int y )
{
	auto* node = const_cast<OFXImageNode*>( static_cast<const OFXImageNode*>( m_effect->node() ) );
	auto* plug = node->parametersPlug()->getChild<V2iPlug>( m_descriptor.getName() );
	if( plug )
	{
		plug->setValue( Imath::V2i( x, y ) );
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus Integer2DInstance::set( OfxTime time, int x, int y )
{
	return set( x, y );
}

GafferOFX::Double3DInstance::Double3DInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::Double3DInstance( descriptor, effect ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<V3fPlug>( name, plugParent, Plug::In, Imath::V3f() );
}

OfxStatus Double3DInstance::get( double& x, double& y, double& z )
{
	auto* node = static_cast<const OFXImageNode*>( m_effect->node() );
	auto* plug = node->parametersPlug()->getChild<V3fPlug>( m_descriptor.getName() );
	if( plug )
	{
		Imath::V3f v = plug->getValue();
		x = v.x; y = v.y; z = v.z;
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus Double3DInstance::get( OfxTime time, double& x, double& y, double& z )
{
	return get( x, y, z );
}

OfxStatus Double3DInstance::set( double x, double y, double z )
{
	auto* node = const_cast<OFXImageNode*>( static_cast<const OFXImageNode*>( m_effect->node() ) );
	auto* plug = node->parametersPlug()->getChild<V3fPlug>( m_descriptor.getName() );
	if( plug )
	{
		plug->setValue( Imath::V3f( (float)x, (float)y, (float)z ) );
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus Double3DInstance::set( OfxTime time, double x, double y, double z )
{
	return set( x, y, z );
}

GafferOFX::Integer3DInstance::Integer3DInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::Integer3DInstance( descriptor, effect ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	setupTypedPlug<V3iPlug>( name, plugParent, Plug::In, Imath::V3i() );
}

OfxStatus Integer3DInstance::get( int& x, int& y, int& z )
{
	auto* node = static_cast<const OFXImageNode*>( m_effect->node() );
	auto* plug = node->parametersPlug()->getChild<V3iPlug>( m_descriptor.getName() );
	if( plug )
	{
		Imath::V3i v = plug->getValue();
		x = v.x; y = v.y; z = v.z;
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus Integer3DInstance::get( OfxTime time, int& x, int& y, int& z )
{
	return get( x, y, z );
}

OfxStatus Integer3DInstance::set( int x, int y, int z )
{
	auto* node = const_cast<OFXImageNode*>( static_cast<const OFXImageNode*>( m_effect->node() ) );
	auto* plug = node->parametersPlug()->getChild<V3iPlug>( m_descriptor.getName() );
	if( plug )
	{
		plug->setValue( Imath::V3i( x, y, z ) );
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus Integer3DInstance::set( OfxTime time, int x, int y, int z )
{
	return set( x, y, z );
}

PushbuttonInstance::PushbuttonInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::PushbuttonInstance( descriptor, effect ), m_effect( effect ), m_descriptor( descriptor ) 
{
}

GafferOFX::StringInstance::StringInstance( GafferOFX::EffectImageInstance* effect, const std::string& name, OFX::Host::Param::Descriptor& descriptor ) : OFX::Host::Param::StringInstance( descriptor, effect ), m_effect( effect ), m_descriptor( descriptor )
{
	auto* plugParent = const_cast<GafferOFX::OFXImageNode*>(static_cast<const GafferOFX::OFXImageNode*>(m_effect->node()))->parametersPlug();
	std::string defaultValue;
	try { defaultValue = descriptor.getProperties().getStringProperty( kOfxParamPropDefault ); } catch( ... ) {}
	setupTypedPlug<StringPlug>( name, plugParent, Plug::In, defaultValue );
}

OfxStatus StringInstance::get( std::string& s )
{
	auto* node = static_cast<const OFXImageNode*>( m_effect->node() );
	auto* plug = node->parametersPlug()->getChild<StringPlug>( m_descriptor.getName() );
	if( plug )
	{
		s = plug->getValue();
		return kOfxStatOK;
	}
	return kOfxStatFailed;
}

OfxStatus StringInstance::get( OfxTime time, std::string& s )
{
	return get( s );
}

OfxStatus StringInstance::set( const char* s )
{
	// The sublabel is typically a display-only string set by the plugin
	// during CreateInstance (e.g. FrameHold::updateSublabel).  Gaffer's
	// Plug::setValue goes through the undo system and cannot be safely
	// called from inside createInstanceAction().  We acknowledge the
	// value but do not store it on the Gaffer plug.

	// Set the plug value but wrap it in a blocking context to avoid
	// undo/action issues during CreateInstance.
	return kOfxStatOK;
}

OfxStatus StringInstance::set( OfxTime time, const char* s )
{
	return set( s );
}


