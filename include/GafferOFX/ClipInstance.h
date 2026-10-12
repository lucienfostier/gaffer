//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2026, Lucien Fostier. All rights reserved.
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
//  NONINFRINGEMENT) OR OTHERWISE ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#pragma once

#include "GafferOFX/EffectImageInstance.h"
#include "GafferOFX/Export.h"

#include "IECore/BoxOps.h"
#include "ImathBox.h"

#include "HostSupport/ofxhClip.h"

#include "ofxPixels.h"

#include <map>
#include <mutex>

namespace GafferOFX
{
	class ClipInstance;

	class Image : public OFX::Host::ImageEffect::Image
	{
		protected :

			// Raw float buffer: RGBA-interleaved (4 floats/pixel) for
			// chromatic clips, single-channel for Alpha (mask) clips.
			std::unique_ptr<float[]> m_data;
			int m_components = 4;

		public :

			explicit Image( ClipInstance &clip, OfxTime t, int view = 0, const OfxRectI *bounds = nullptr );
			OfxRGBAColourF* pixel( int x, int y ) const;
			float* alphaPixel( int x, int y ) const;
			~Image();
	};

	class ClipInstance : public OFX::Host::ImageEffect::ClipInstance
	{
		protected :

			GafferOFX::EffectImageInstance*	m_effect;
			std::string	m_name;
			std::string m_plugName;

			// GL output texture ID for OpenGL rendering
			unsigned int m_outputTexture = 0;

			// Cached source input texture for GL rendering (avoids glGenTextures per render)
			unsigned int m_inputTexture = 0;
			int m_inputTexW = 0;
			int m_inputTexH = 0;
			/// GLContextManager generation at last upload; stale after a context rebuild.
			int m_inputTextureGeneration = 0;

		public :

			ClipInstance(
				GafferOFX::EffectImageInstance* effect,
				OFX::Host::ImageEffect::ClipDescriptor* desc
			);

			~ClipInstance();
			Image* getOutputImage();

			void setPlugName( const std::string &name ) { m_plugName = name; }
			const std::string &plugName() const { return m_plugName; }

			// Bit depth reported to the plugin: always float.
			const std::string &getUnmappedBitDepth() const override;

			// Unmapped components reported to the plugin: RGBA.
			const std::string &getUnmappedComponents() const override;

			// Premultiplication reported to the plugin: premultiplied.
			const std::string &getPremult() const override;

			// Project pixel aspect ratio from the effect instance.
			double getAspectRatio() const override;

			// Frames per second from the current context, defaulting to 24.
			double getFrameRate() const override;

			// Script frame range, defaulting to 1-100.
			void getFrameRange(double &startFrame, double &endFrame) const override;

			// Field order reported to the plugin: unfielded.
			const std::string &getFieldOrder() const override;

			// Output is always connected; inputs follow their Gaffer plug.
			bool getConnected() const override;

			// Same as the mapped frame rate.
			double getUnmappedFrameRate() const override;

			// Same as the mapped frame range.
			void getUnmappedFrameRange(double &unmappedStartFrame, double &unmappedEndFrame) const override;

			// Images sample at discrete frames only.
			bool getContinuousSamples() const override;

			// Fetch the clip image at the given time, optionally restricted
			// to optionalBounds.
			OFX::Host::ImageEffect::Image* getImage(OfxTime time, const OfxRectD *optionalBounds) override;

			// Region of definition: project size for Output, mapped plug
			// data window for inputs.
			OfxRectD getRegionOfDefinition(OfxTime time) const override;

#ifdef OFX_SUPPORTS_OPENGLRENDER
			/// load a texture from the given clip, for OpenGL-based rendering.
			OFX::Host::ImageEffect::Texture* loadTexture(OfxTime time, const char *format, const OfxRectD *optionalBounds) override;
#endif

			/// Set an output OpenGL texture that the plugin will render into.
			void setOutputTexture( 			unsigned int textureId ) { m_outputTexture = textureId; }
			unsigned int outputTexture() const { return m_outputTexture; }

	};

	/// A texture view over either a host-owned FBO texture (input clips,
	/// lifetime managed via GLContextManager::registerTexture) or the
	/// shared render-target texture (Output clip, owned by the context
	/// manager). Non-owning: destruction releases only the HostSupport
	/// reference, never the GL object.
	class GafferTexture : public OFX::Host::ImageEffect::Texture
	{
		public :
			GafferTexture(
				ClipInstance& instance,
				double renderScaleX,
				double renderScaleY,
				unsigned int index,
				unsigned int target,
				const OfxRectI &bounds,
				const OfxRectI &rod,
				int rowBytes,
				const std::string &field,
				const std::string &uniqueIdentifier
			);

			~GafferTexture() override;

		private :
			unsigned int m_textureId;
	};

}
