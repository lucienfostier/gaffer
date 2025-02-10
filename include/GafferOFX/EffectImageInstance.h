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

#pragma once

#include "GafferOFX/Export.h"

#include "HostSupport/ofxhImageEffect.h"

#include "Gaffer/ScriptNode.h"
#include "Gaffer/Node.h"

namespace GafferOFX
{

class GAFFEROFX_API EffectImageInstance : public OFX::Host::ImageEffect::Instance
{
	public :

		EffectImageInstance(
			OFX::Host::ImageEffect::ImageEffectPlugin* plugin,
            OFX::Host::ImageEffect::Descriptor& desc,
            const std::string& context
			);

		const std::string &getDefaultOutputFielding() const override;
		
		OFX::Host::ImageEffect::ClipInstance* newClipInstance(
			OFX::Host::ImageEffect::Instance* plugin,
			OFX::Host::ImageEffect::ClipDescriptor* descriptor,
			int index
			) override;
	
		OfxStatus vmessage(const char* type,
								   const char* id,
								   const char* format,
								   va_list args) override;       
		
		OfxStatus setPersistentMessage(const char* type,
											   const char* id,
											   const char* format,
											   va_list args) override;
	
		OfxStatus clearPersistentMessage() override;

		void getProjectSize(double& xSize, double& ySize) const override;
		void getProjectOffset(double& xOffset, double& yOffset) const override;
		void getProjectExtent(double& xSize, double& ySize) const override;
		double getProjectPixelAspectRatio() const override;
		double getEffectDuration() const override;
		double getFrameRate() const override;
		double getFrameRecursive() const override;
		void getRenderScaleRecursive(double &x, double &y) const override;
	
	
		OFX::Host::Param::Instance* newParam(const std::string& name, OFX::Host::Param::Descriptor& Descriptor) override;
		
		OfxStatus editBegin(const std::string& name) override;
		OfxStatus editEnd() override;
	
		void progressStart(const std::string &message, const std::string &messageid) override;
		void progressEnd() override;
		bool progressUpdate(double t) override;        
	
		double timeLineGetTime() override;
		void timeLineGotoTime(double t) override;
		void timeLineGetBounds(double &t1, double &t2) override;

		void setNode(const Gaffer::Node* node);

		protected:

		const Gaffer::Node* node() const;
		const Gaffer::ScriptNode* scriptNode() const;

		private:

		const Gaffer::Node* m_node;
		
};

} // GafferOFX

