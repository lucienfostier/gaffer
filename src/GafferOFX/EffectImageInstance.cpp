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

#include <iostream>

using namespace GafferOFX;

EffectImageInstance::EffectImageInstance( OFX::Host::ImageEffect::ImageEffectPlugin* plugin, OFX::Host::ImageEffect::Descriptor& desc, const std::string& context): OFX::Host::ImageEffect::Instance(plugin,desc,context,false)
{
	std::cout << "GafferOFX Effect Image Instance ctor" << std::endl;
}

OFX::Host::ImageEffect::ClipInstance* EffectImageInstance::newClipInstance(OFX::Host::ImageEffect::Instance* plugin, OFX::Host::ImageEffect::ClipDescriptor* descriptor, int index)
{
	return nullptr;
	//return new MyClipInstance(this,descriptor);
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
	xSize = 768;
	ySize = 576;
}

void EffectImageInstance::getProjectOffset(double& xOffset, double& yOffset) const
{
	xOffset = 0;
	yOffset = 0;
}

void EffectImageInstance::getProjectExtent(double& xSize, double& ySize) const
{
	xSize = 768;
	ySize = 576;
}

double EffectImageInstance::getProjectPixelAspectRatio() const
{
	return double(768)/double(720);
}

double EffectImageInstance::getEffectDuration() const
{
	return 25.0;
}

double EffectImageInstance::getFrameRate() const
{
	return 25.0;
}

double EffectImageInstance::getFrameRecursive() const
{
	return 0.0;
}

void EffectImageInstance::getRenderScaleRecursive(double &x, double &y) const
{
	x = y = 1.0;
}

// make a parameter instance
OFX::Host::Param::Instance* EffectImageInstance::newParam(const std::string& name, OFX::Host::Param::Descriptor& descriptor)
{
	return nullptr;
	/*
	if(descriptor.getType()==kOfxParamTypeInteger)
	  return new MyIntegerInstance(this,name,descriptor);
	else if(descriptor.getType()==kOfxParamTypeDouble)
	  return new MyDoubleInstance(this,name,descriptor);
	else if(descriptor.getType()==kOfxParamTypeBoolean)
	  return new MyBooleanInstance(this,name,descriptor);
	else if(descriptor.getType()==kOfxParamTypeChoice)
	  return new MyChoiceInstance(this,name,descriptor);
	else if(descriptor.getType()==kOfxParamTypeRGBA)
	  return new MyRGBAInstance(this,name,descriptor);
	else if(descriptor.getType()==kOfxParamTypeRGB)
	  return new MyRGBInstance(this,name,descriptor);
	else if(descriptor.getType()==kOfxParamTypeDouble2D)
	  return new MyDouble2DInstance(this,name,descriptor);
	else if(descriptor.getType()==kOfxParamTypeInteger2D)
	  return new MyInteger2DInstance(this,name,descriptor);
	else if(descriptor.getType()==kOfxParamTypePushButton)
	  return new MyPushbuttonInstance(this,name,descriptor);
	else if(descriptor.getType()==kOfxParamTypeGroup)
	  return new OFX::Host::Param::GroupInstance(descriptor,this);
	else if(descriptor.getType()==kOfxParamTypePage)
	  return new OFX::Host::Param::PageInstance(descriptor,this);
	else
	  return 0;
	*/
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
	return 0;
}

void  EffectImageInstance::timeLineGotoTime(double t)
{
}

void  EffectImageInstance::timeLineGetBounds(double &t1, double &t2)
{
	t1 = 0;
	t2 = 25;
}
