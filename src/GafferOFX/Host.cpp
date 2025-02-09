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
#include "GafferOFX/Host.h"
#include "GafferOFX/EffectImageInstance.h"

#include <cstring>

#include <fstream>

using namespace GafferOFX;

Host::Host()
{
	_properties.setIntProperty(kOfxPropAPIVersion, 1, 0);
	_properties.setIntProperty(kOfxPropAPIVersion, 4, 1);
	_properties.setStringProperty(kOfxPropName, "GafferOFXHost");
	_properties.setStringProperty(kOfxPropLabel, "Gaffer OFX Host");
	_properties.setIntProperty(kOfxPropVersion, 1, 0);
	_properties.setIntProperty(kOfxPropVersion, 0, 1);
	_properties.setStringProperty(kOfxPropVersionLabel, "1.0");
	_properties.setIntProperty(kOfxImageEffectHostPropIsBackground, 0);
	_properties.setIntProperty(kOfxImageEffectPropSupportsOverlays, 0);
	_properties.setIntProperty(kOfxImageEffectPropSupportsMultiResolution, 0);
	_properties.setIntProperty(kOfxImageEffectPropSupportsTiles, true);
	_properties.setIntProperty(kOfxImageEffectPropTemporalClipAccess, true);
	_properties.setStringProperty(kOfxImageEffectPropSupportedComponents,  kOfxImageComponentRGBA, 0);
	_properties.setStringProperty(kOfxImageEffectPropSupportedComponents,  kOfxImageComponentAlpha, 1);
	_properties.setStringProperty(kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextGenerator, 0 );
	_properties.setStringProperty(kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextFilter, 1);
	_properties.setStringProperty(kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextGeneral, 2 );
	_properties.setStringProperty(kOfxImageEffectPropSupportedContexts, kOfxImageEffectContextTransition, 3 );
	_properties.setIntProperty(kOfxImageEffectPropSupportsMultipleClipDepths, 0);
	_properties.setIntProperty(kOfxImageEffectPropSupportsMultipleClipPARs, 0);
	_properties.setIntProperty(kOfxImageEffectPropSetableFrameRate, 0);
	_properties.setIntProperty(kOfxImageEffectPropSetableFielding, 0);
	_properties.setIntProperty(kOfxParamHostPropSupportsCustomInteract, 0 );
	_properties.setIntProperty(kOfxParamHostPropSupportsStringAnimation, 0 );
	_properties.setIntProperty(kOfxParamHostPropSupportsChoiceAnimation, 0 );
	_properties.setIntProperty(kOfxParamHostPropSupportsBooleanAnimation, 0 );
	_properties.setIntProperty(kOfxParamHostPropSupportsCustomAnimation, 0 );
	_properties.setIntProperty(kOfxParamHostPropMaxParameters, -1);
	_properties.setIntProperty(kOfxParamHostPropMaxPages, 0);
	_properties.setIntProperty(kOfxParamHostPropPageRowColumnCount, 0, 0 );
	_properties.setIntProperty(kOfxParamHostPropPageRowColumnCount, 0, 1 );
}

OFX::Host::ImageEffect::Instance* Host::newInstance(
	void* clientData,
	OFX::Host::ImageEffect::ImageEffectPlugin* plugin,
	OFX::Host::ImageEffect::Descriptor& desc,
	const std::string& context
)
{
	std::cout << "host new instance: " << clientData << std::endl;
	auto instance = new EffectImageInstance(plugin, desc, context);
	instance->setNode(reinterpret_cast<Gaffer::Node*>(clientData));
	return instance;
}

OFX::Host::ImageEffect::Descriptor *Host::makeDescriptor(
	OFX::Host::ImageEffect::ImageEffectPlugin* plugin
)
{
	OFX::Host::ImageEffect::Descriptor *desc = new OFX::Host::ImageEffect::Descriptor(plugin);
	return desc;
}

OFX::Host::ImageEffect::Descriptor *Host::makeDescriptor(
	const OFX::Host::ImageEffect::Descriptor &rootContext,
	OFX::Host::ImageEffect::ImageEffectPlugin *plugin
)
{
	OFX::Host::ImageEffect::Descriptor *desc = new OFX::Host::ImageEffect::Descriptor(rootContext, plugin);
	return desc;
}

OFX::Host::ImageEffect::Descriptor *Host::makeDescriptor(
	const std::string &bundlePath,
	OFX::Host::ImageEffect::ImageEffectPlugin *plugin
)
{
	OFX::Host::ImageEffect::Descriptor *desc = new OFX::Host::ImageEffect::Descriptor(bundlePath, plugin);
	return desc;
}

OfxStatus Host::vmessage(
	const char* type,
	const char* id,
	const char* format,
	va_list args
)
{
	bool isQuestion = false;
	const char *prefix = "Message : ";
	if (strcmp(type, kOfxMessageLog) == 0)
	{
	  prefix = "Log : ";
	}
	else if(
		strcmp(type, kOfxMessageFatal) == 0 ||
		strcmp(type, kOfxMessageError) == 0
	)
	{
	  prefix = "Error : ";
	}
	else if(strcmp(type, kOfxMessageQuestion) == 0) 
	{
	  prefix = "Question : ";
	  isQuestion = true;
	}

	// Just dump our message to stdout, should be done with a proper
	// UI in a full ap, and post a dialogue for yes/no questions.
	fputs(prefix, stdout);
	vprintf(format, args);
	printf("\n");

	if(isQuestion)
	{
		  /// cant do this properly inour example, as we need to raise a dialogue to ask a question, so just return yes
		  return kOfxStatReplyYes;      
	}
	else
	{
		  return kOfxStatOK;
	}
}

OfxStatus Host::setPersistentMessage(const char* type,
								   const char* id,
								   const char* format,
								   va_list args)
{
	return vmessage(type, id, format, args);
}

OfxStatus Host::clearPersistentMessage()
{
	return kOfxStatOK;
}

Host& Host::instance()
{
	static Host instance;
	return instance;
}

void Host::findOFXPlugins()
{
	OFX::Host::PluginCache::getPluginCache()->setCacheVersion("GafferOFXCache");

	m_pluginCache = OFX::Host::ImageEffect::PluginCache(Host::instance());
	m_pluginCache.registerInCache(*OFX::Host::PluginCache::getPluginCache());

	// try to read an old cache
	std::ifstream ifs("GafferOFXPluginCache.xml");
	OFX::Host::PluginCache::getPluginCache()->readCache(ifs);
	OFX::Host::PluginCache::getPluginCache()->scanPluginFiles();
	ifs.close();
	
	/// flush out the current cache
	std::ofstream of("GafferOFXPluginCache.xml");
	OFX::Host::PluginCache::getPluginCache()->writePluginCache(of);
	of.close();

	m_pluginCache.dumpToStdOut();

}

OFX::Host::ImageEffect::PluginCache GafferOFX::Host::m_pluginCache(instance());
