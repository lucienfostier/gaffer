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

#include "Gaffer/Context.h"

#include "GafferImage/FormatPlug.h"

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
	auto gafferFormat = GafferImage::FormatPlug::getDefaultFormat(Gaffer::Context::current());
	xSize = gafferFormat.width();
	ySize = gafferFormat.height();
}

void EffectImageInstance::getProjectOffset(double& xOffset, double& yOffset) const
{
	xOffset = 0;
	yOffset = 0;
}

void EffectImageInstance::getProjectExtent(double& xSize, double& ySize) const
{
	auto gafferFormat = GafferImage::FormatPlug::getDefaultFormat(Gaffer::Context::current());
	xSize = gafferFormat.width();
	ySize = gafferFormat.height();
}

double EffectImageInstance::getProjectPixelAspectRatio() const
{
	auto gafferFormat = GafferImage::FormatPlug::getDefaultFormat(Gaffer::Context::current());
	auto width = gafferFormat.width();
	auto height = gafferFormat.height();
	return double(width)/double(height);
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
	return Gaffer::Context::current()->getFramesPerSecond();
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
OFX::Host::Param::Instance* EffectImageInstance::newParam(const std::string& name, OFX::Host::Param::Descriptor& descriptor)
{
	std::cout << "name: " << name << " type: " << descriptor.getType() << std::endl;
	if(descriptor.getType()==kOfxParamTypeInteger)
	{
	  return new IntegerInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeDouble)
	{
	  return new DoubleInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeBoolean)
	{
	  return new BooleanInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeChoice)
	{
	  return new ChoiceInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeRGBA)
	{
	  return new RGBAInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeRGB)
	{
	  return new RGBInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeDouble2D)
	{
	  return new Double2DInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeInteger2D)
	{
	  return new Integer2DInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypePushButton)
	{
	  return new PushbuttonInstance(this,name,descriptor);
	}
	else if(descriptor.getType()==kOfxParamTypeGroup)
	{
	  return new OFX::Host::Param::GroupInstance(descriptor,this);
	}
	else if(descriptor.getType()==kOfxParamTypePage)
	{
	  return new OFX::Host::Param::PageInstance(descriptor,this);
	}
	else
	{
	  return nullptr;
	}
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
