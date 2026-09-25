//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2025, Lucien Fostier. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above
//       copyright notice, this list of conditions and the following
//       disclaimer.
//
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided with
//       the distribution.
//
//     * Neither the name of Image Engine Design nor the names of
//       any other contributors to this software may be used to endorse or
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

#include "Gaffer/Plug.h"

#include "HostSupport/ofxhParam.h"

namespace GafferOFX
{

namespace ParamAlgo
{

/// Canonical plug name for a descriptor (wraps sanitizeName).
std::string plugName( const OFX::Host::Param::Descriptor &descriptor );
std::string plugName( const std::string &paramName );

/// Create or reconcile a plug for the given param descriptor under `parent`.
/// Returns the plug (new or existing) or nullptr for Group/Page/unhandled types.
/// Keeps existing plug iff type+structure+default match (preserving values,
/// connections, animation). Otherwise PlugAlgo::replacePlug.
/// If keepExistingValues==false, resets value to default even when keeping.
Gaffer::Plug *setupPlug( const OFX::Host::Param::Descriptor &descriptor, Gaffer::Plug *parent, bool keepExistingValues = true );

/// Walk a SetDescriptor and setup all param plugs under `parent`.
/// Removes stale plugs not in descriptor when keepExistingValues==true.
void setupPlugs( const OFX::Host::Param::SetDescriptor &setDescriptor, Gaffer::Plug *parent, bool keepExistingValues = true );

} // namespace ParamAlgo

} // namespace GafferOFX
