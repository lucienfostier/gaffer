##########################################################################
#
#  Copyright (c) 2025, Lucien Fostier. All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are
#  met:
#
#      * Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#
#      * Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials provided with
#        the distribution.
#
#      * Neither the name of John Haddon nor the names of
#        any other contributors to this software may be used to endorse or
#        promote products derived from this software without specific prior
#        written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
#  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
#  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
#  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
#  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
#  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
##########################################################################

import imath

import IECore

import Gaffer
import GafferUI

class OFXOverlayGadget( GafferUI.Gadget ) :

	def __init__( self, interact, **kw ) :

		GafferUI.Gadget.__init__( self )
		self.__interact = interact

	def renderLayer( self, layer, style, renderReason ) :

		if self.__interact is None :
			return

		IECore.msg( IECore.Msg.Level.Warning, "OFXOverlayGadget", "renderLayer: layer=" + str( layer ) + " reason=" + str( renderReason ) )

		self.__interact.setupGLProjection()
		try :
			if not getattr( self, "__focused", False ) :
				IECore.msg( IECore.Msg.Level.Warning, "OFXOverlayGadget", "renderLayer: calling gainFocusAction" )
				self.__interact.gainFocusAction( self.__interact.getTime(), ( 1.0, 1.0 ) )
				self.__focused = True
			result = self.__interact.drawAction( self.__interact.getTime(), ( 1.0, 1.0 ) )
			IECore.msg( IECore.Msg.Level.Warning, "OFXOverlayGadget", "renderLayer: drawAction returned " + str( result ) )
		finally :
			self.__interact.restoreGLProjection()

	def layerMask( self ) :

		return 32 # GafferUI.Gadget.Layer.Front

	def renderBound( self ) :

		return imath.Box3f( imath.V3f( -1e9 ), imath.V3f( 1e9 ) )
