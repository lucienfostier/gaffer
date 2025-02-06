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
#include "GafferOFX/PluginCache.h"
#include "GafferOFX/Host.h"

#include <fstream>

using namespace GafferOFX;

void GafferOFX::findOFXPlugins()
{
	std::cout << "find OFX plugins" << std::endl;
	Host& host = Host::instance();
	OFX::Host::ImageEffect::PluginCache imageEffectPluginCache(host);

	OFX::Host::PluginCache::getPluginCache()->setCacheVersion("GafferOFX");

	imageEffectPluginCache.registerInCache(*OFX::Host::PluginCache::getPluginCache());

	// try to read an old cache
	std::ifstream ifs("GafferOFXPluginCache.xml");
	OFX::Host::PluginCache::getPluginCache()->readCache(ifs);
	OFX::Host::PluginCache::getPluginCache()->scanPluginFiles();
	ifs.close();
	
	/// flush out the current cache
	std::ofstream of("GafferOFXPluginCache.xml");
	OFX::Host::PluginCache::getPluginCache()->writePluginCache(of);
	of.close();

	for(const auto* p : imageEffectPluginCache.getPlugins() )
	{
		std::cout << "find ofx plugins: " << p->getIdentifier() << std::endl;
	}

}
