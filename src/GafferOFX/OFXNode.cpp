//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2025, Lucien Fostier. All rights reserved.
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
//      * Neither the name of John Haddon nor the names of
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

#include "GafferOFX/OFXNode.h"
#include "GafferOFX/Host.h"


#include <fstream>

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace GafferImage;
using namespace Gaffer;
using namespace GafferOFX;

//////////////////////////////////////////////////////////////////////////
// OFXNode implementation
//////////////////////////////////////////////////////////////////////////

GAFFER_NODE_DEFINE_TYPE( OFXNode );

size_t OFXNode::g_firstPlugIndex = 0;

OFXNode::OFXNode( const std::string &name )
	: ImageNode( name )
{
	storeIndexOfNextChild( g_firstPlugIndex );
	addChild( new StringPlug( "pluginId" ) );
}

OFXNode::~OFXNode()
{
}

void OFXNode::createPluginInstance()
{
	Host& host = Host::instance();
	std::cout  << "host: " << &host << std::endl;
	OFX::Host::ImageEffect::PluginCache imageEffectPluginCache(host);


	OFX::Host::PluginCache::getPluginCache()->setCacheVersion("GafferOFX");



	imageEffectPluginCache.registerInCache(*OFX::Host::PluginCache::getPluginCache());


	// try to read an old cache
	std::ifstream ifs("hostDemoPluginCache.xml");
	OFX::Host::PluginCache::getPluginCache()->readCache(ifs);
	OFX::Host::PluginCache::getPluginCache()->scanPluginFiles();
	ifs.close();
	
	/// flush out the current cache
	std::ofstream of("hostDemoPluginCache.xml");
	OFX::Host::PluginCache::getPluginCache()->writePluginCache(of);
	of.close();

	OFX::Host::ImageEffect::ImageEffectPlugin* plugin = imageEffectPluginCache.getPluginById("net.sf.openfx.invertPlugin");

	imageEffectPluginCache.dumpToStdOut();
	std::cout << "createPluginInstance() : " << plugin << std::endl;

}

Gaffer::StringPlug* OFXNode::pluginIdPlug()
{
	return getChild<StringPlug>( g_firstPlugIndex );
}

const Gaffer::StringPlug* OFXNode::pluginIdPlug() const
{
	return getChild<StringPlug>( g_firstPlugIndex );
}

void OFXNode::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	ImageNode::affects( input, outputs );
}

void OFXNode::hashViewNames( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageNode::hashViewNames( output, context, h );
}

IECore::ConstStringVectorDataPtr OFXNode::computeViewNames( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return ImagePlug::defaultViewNames();
}

void OFXNode::hashFormat( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageNode::hashFormat( output, context, h );
}

GafferImage::Format OFXNode::computeFormat( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return GafferImage::Format();
}

void OFXNode::hashDataWindow( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageNode::hashDataWindow( output, context, h );
}

Imath::Box2i OFXNode::computeDataWindow( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return Box2i();
}

IECore::ConstCompoundDataPtr OFXNode::computeMetadata( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return outPlug()->metadataPlug()->defaultValue();
}

bool OFXNode::computeDeep( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return true;
}

void OFXNode::hashSampleOffsets( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	h = ImagePlug::emptyTileSampleOffsets()->Object::hash();
}

IECore::ConstIntVectorDataPtr OFXNode::computeSampleOffsets( const Imath::V2i &tileOrigin, const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return ImagePlug::emptyTileSampleOffsets();
}

void OFXNode::hashChannelNames( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageNode::hashChannelNames( output, context, h );
}

IECore::ConstStringVectorDataPtr OFXNode::computeChannelNames( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return new IECore::StringVectorData();
}

void OFXNode::hashChannelData( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	h = ImagePlug::emptyTile()->Object::hash();
}

IECore::ConstFloatVectorDataPtr OFXNode::computeChannelData( const std::string &channelName, const Imath::V2i &tileOrigin, const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return ImagePlug::emptyTile();
}
