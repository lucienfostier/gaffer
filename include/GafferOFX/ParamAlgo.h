//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2026, Lucien Fostier. All rights reserved.
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

#include "IECore/Ramp.h"

#include "HostSupport/ofxhParam.h"

#include <utility>
#include <vector>

namespace GafferOFX
{

namespace ParamAlgo
{

/// Returns the canonical plug name for a descriptor. This is the single
/// authority for the descriptor-to-plug-name mapping shared by setup
/// (`ParamAlgo`) and binding (`ParamInstance`, `EffectImageInstance`).
/// Invalid `GraphComponent` characters are replaced with underscores.
std::string plugName( const OFX::Host::Param::Descriptor &descriptor );
std::string plugName( const std::string &paramName );

/// Builds a `Rampff` default for one parametric curve index. Falls back
/// to the identity curve `(0,0)->(1,1)` when the base has no points for
/// that curve.
IECore::Rampff rampForCurve( const std::vector<std::vector<std::pair<double,double>>> &baseCurves, int curveIndex );

/// Narrows an OFX double to float range. OFX params may default to
/// +/-infinity (for example Shadertoy min/max params), which float plugs
/// cannot round-trip through scripts: imath Python constructors reject
/// infinities, and even `FLT_MAX` re-parses out of range at Gaffer's
/// serialisation precision. Clamps to a large but round-trippable
/// magnitude.
float floatForOFXDefault( double v );

/// Registers static UI metadata (label, section, presets, ranges) for a
/// reconciled plug. `setDescriptor` may be null, in which case section
/// lookup is skipped.
void registerParameterMetadata( Gaffer::Plug *plug, const OFX::Host::Param::Descriptor &descriptor, const OFX::Host::Param::SetDescriptor *setDescriptor );

/// Creates or reconciles a plug for the given param descriptor under
/// `parent`. Returns the plug, which may be new or existing, or nullptr
/// for Group and Page params and for unhandled types. Keeps an existing
/// plug if its type, structure and default match, preserving values,
/// connections and animation; otherwise calls `PlugAlgo::replacePlug`.
/// If `keepExistingValues` is false, resets the value to the default
/// even when the plug is kept.
Gaffer::Plug *setupPlug( const OFX::Host::Param::Descriptor &descriptor, Gaffer::Plug *parent, bool keepExistingValues = true );

/// Walks a `SetDescriptor` and sets up all param plugs under `parent`.
/// Stale plugs that are not in the descriptor are removed. If
/// `keepExistingValues` is false, existing values are reset to their
/// defaults.
void setupPlugs( const OFX::Host::Param::SetDescriptor &setDescriptor, Gaffer::Plug *parent, bool keepExistingValues = true );

} // namespace ParamAlgo

} // namespace GafferOFX
