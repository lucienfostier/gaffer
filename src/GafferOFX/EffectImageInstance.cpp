//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2025, Lucien Fostier. All rights reserved.
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
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////
#include "GafferOFX/EffectImageInstance.h"
#include "GafferOFX/ClipInstance.h"
#include "GafferOFX/ParamInstance.h"
#include "GafferOFX/OFXImageNode.h"

#include "Gaffer/Context.h"
#include "Gaffer/Metadata.h"
#include "Gaffer/Plug.h"

#include "GafferImage/FormatPlug.h"

#include "IECore/SimpleTypedData.h"
#include "IECore/VectorTypedData.h"

#include "HostSupport/ofxhPluginCache.h"
#include "HostSupport/ofxhImageEffectAPI.h"



#include <iostream>

using namespace GafferOFX;

EffectImageInstance::EffectImageInstance( OFX::Host::ImageEffect::ImageEffectPlugin* plugin, OFX::Host::ImageEffect::Descriptor& desc, const std::string& context): OFX::Host::ImageEffect::Instance(plugin,desc,context,false)
{
}

OFX::Host::ImageEffect::ClipInstance* EffectImageInstance::newClipInstance(OFX::Host::ImageEffect::Instance* plugin, OFX::Host::ImageEffect::ClipDescriptor* descriptor, int index)
{
	return new ClipInstance(this,descriptor);
}


const std::string &EffectImageInstance::getDefaultOutputFielding() const
{
	static const std::string v(kOfxImageFieldNone);
	return v;    
}

OfxStatus EffectImageInstance::vmessage(const char* type, const char* id, const char* format, va_list args)
{
	printf("%s %s ",type,id);
	vprintf(format,args);
	return kOfxStatOK;
}

OfxStatus EffectImageInstance::setPersistentMessage(const char* type, const char* id, const char* format, va_list args)
{
	return vmessage(type, id, format, args);
}

OfxStatus EffectImageInstance::clearPersistentMessage()
{
	return kOfxStatOK;
}

void EffectImageInstance::getProjectSize(double& xSize, double& ySize) const
{
	if( auto ctx = Gaffer::Context::current() )
	{
		auto gafferFormat = GafferImage::FormatPlug::getDefaultFormat( ctx );
		xSize = gafferFormat.width();
		ySize = gafferFormat.height();
	}
	else
	{
		xSize = 1920;
		ySize = 1080;
	}
}

void EffectImageInstance::getProjectOffset(double& xOffset, double& yOffset) const
{
	xOffset = 0;
	yOffset = 0;
}

void EffectImageInstance::getProjectExtent(double& xSize, double& ySize) const
{
	if( auto ctx = Gaffer::Context::current() )
	{
		auto gafferFormat = GafferImage::FormatPlug::getDefaultFormat( ctx );
		xSize = gafferFormat.width();
		ySize = gafferFormat.height();
	}
	else
	{
		xSize = 1920;
		ySize = 1080;
	}
}

double EffectImageInstance::getProjectPixelAspectRatio() const
{
	if( auto ctx = Gaffer::Context::current() )
	{
		auto gafferFormat = GafferImage::FormatPlug::getDefaultFormat( ctx );
		return gafferFormat.getPixelAspect();
	}
	return 1.0;
}

double EffectImageInstance::getEffectDuration() const
{
	
	auto start = 1;
	auto end = 100;
	if( auto sn = scriptNode() )
	{
		start = sn->frameStartPlug()->getValue();
		end = sn->frameEndPlug()->getValue();
	}
	return end - start;
}

double EffectImageInstance::getFrameRate() const
{
	if( auto ctx = Gaffer::Context::current() )
		return ctx->getFramesPerSecond();
	return 24.0;
}

double EffectImageInstance::getFrameRecursive() const
{
	return Gaffer::Context::current()->getFrame();
;
}

void EffectImageInstance::getRenderScaleRecursive(double &x, double &y) const
{
	x = y = 1.0;
}

// make a parameter instance
namespace
{

void registerParameterMetadata( Gaffer::Plug *plug, const OFX::Host::Param::Descriptor &descriptor )
{
	const auto &props = descriptor.getProperties();
	const std::string type = descriptor.getType();

	// Label
	try
	{
		std::string label = props.getStringProperty( kOfxPropLabel );
		if( !label.empty() )
		{
			Gaffer::Metadata::registerValue( plug, "label", new IECore::StringData( label ), false );
		}
	}
	catch( ... )
	{
	}

	// Tooltip
	try
	{
		std::string hint = props.getStringProperty( kOfxParamPropHint );
		if( !hint.empty() )
		{
			Gaffer::Metadata::registerValue( plug, "description", new IECore::StringData( hint ), false );
		}
	}
	catch( ... )
	{
	}

	// Secret params — hide nodule
	try
	{
		if( props.getIntProperty( kOfxParamPropSecret ) )
		{
			Gaffer::Metadata::registerValue( plug, "nodule:type", new IECore::StringData( "" ), false );
		}
	}
	catch( ... )
	{
	}

	// Numeric range limits
	if( type == kOfxParamTypeInteger || type == kOfxParamTypeDouble )
	{
		try
		{
			double min = props.getDoubleProperty( kOfxParamPropMin );
			double max = props.getDoubleProperty( kOfxParamPropMax );
			Gaffer::Metadata::registerValue( plug, "hardRange", new IECore::V2dData( Imath::V2d( min, max ) ), false );
		}
		catch( ... )
		{
		}

		try
		{
			double min = props.getDoubleProperty( kOfxParamPropDisplayMin );
			double max = props.getDoubleProperty( kOfxParamPropDisplayMax );
			Gaffer::Metadata::registerValue( plug, "softRange", new IECore::V2dData( Imath::V2d( min, max ) ), false );
		}
		catch( ... )
		{
		}
	}

	// Choice presets
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
		catch( ... )
		{
		}

		Gaffer::Metadata::registerValue( plug, "plugValueWidget:type", new IECore::StringData( "GafferUI.PresetsPlugValueWidget" ), false );
	}
}

} // anonymous namespace

OFX::Host::Param::Instance* EffectImageInstance::newParam(const std::string& name, OFX::Host::Param::Descriptor& descriptor)
{
	OFX::Host::Param::Instance *result = nullptr;

	if(descriptor.getType()==kOfxParamTypeInteger)
	{
		result = new IntegerInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeDouble)
	{
		result = new DoubleInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeBoolean)
	{
		result = new BooleanInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeChoice)
	{
		result = new ChoiceInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeRGBA)
	{
		result = new RGBAInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeRGB)
	{
		result = new RGBInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeDouble2D)
	{
		result = new Double2DInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeInteger2D)
	{
		result = new Integer2DInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypePushButton)
	{
		result = new PushbuttonInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeString)
	{
		result = new StringInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeGroup)
	{
		result = new OFX::Host::Param::GroupInstance(descriptor,this);
	}
	else if(descriptor.getType()==kOfxParamTypePage)
	{
		result = new OFX::Host::Param::PageInstance(descriptor,this);
	}

	if( result )
	{
		auto *plug = const_cast<OFXImageNode*>( static_cast<const OFXImageNode*>( node() ) )->parametersPlug()->getChild<Gaffer::Plug>( name );
		if( plug )
		{
			registerParameterMetadata( plug, descriptor );
		}
	}

	return result;
}

OfxStatus EffectImageInstance::editBegin(const std::string& name)
{
	return kOfxStatErrMissingHostFeature;
}

OfxStatus EffectImageInstance::editEnd()
{
	return kOfxStatErrMissingHostFeature;
}

void  EffectImageInstance::progressStart(const std::string &message, const std::string &messageid)
{
}

void  EffectImageInstance::progressEnd()
{
}

bool  EffectImageInstance::progressUpdate(double t)
{
	return true;
}


double  EffectImageInstance::timeLineGetTime()
{
	return Gaffer::Context::current()->getFrame();
}

void  EffectImageInstance::timeLineGotoTime(double t)
{
}

void  EffectImageInstance::timeLineGetBounds(double &t1, double &t2)
{

	t1 = 1;
	t2 = 100;
	if( auto sn = scriptNode() )
	{
		t1 = sn->frameStartPlug()->getValue();
		t2 = sn->frameEndPlug()->getValue();
	}
}

const Gaffer::Node* EffectImageInstance::node() const
{
	return m_node;
}

const Gaffer::ScriptNode* EffectImageInstance::scriptNode() const
{
	return m_node->ancestor<Gaffer::ScriptNode>();
}

void EffectImageInstance::setNode(const Gaffer::Node* node)
{
	m_node = node;
}

// Helper: detect if this is a Mode 1 call (vtable[13](this) → return handle)
// vs a Mode 2 call (vtable[13](this, action, handle, inArgs, outArgs) → dispatch).
// In Mode 1, the action pointer is whatever was left in rsi (typically not "Ofx*").
// In Mode 2, action is always a valid OFX action string starting with "Ofx".
static inline bool isMode1Call( const char* action, const void* handle )
{
	if( !action )
		return true;
	const char* p = action;
	// Fast check: an OFX action always starts with "OfxAction" or "OfxImageEffectAction"
	// For a valid string, the first 3 bytes are readable. If they're not 'O','f','x',
	// this cannot be a valid OFX action string → Mode 1.
	if( p[0] != 'O' || p[1] != 'f' || p[2] != 'x' )
		return true;
	// Could be Mode 2 - let the caller verify further.
	return false;
}

OfxStatus EffectImageInstance::mainEntry(const char *action, const void *handle, OFX::Host::Property::Set *inArgs, OFX::Host::Property::Set *outArgs)
{
	typedef OFX::Host::ImageEffect::Instance BaseInstance;
	return BaseInstance::mainEntry( action, handle, inArgs, outArgs );
}
