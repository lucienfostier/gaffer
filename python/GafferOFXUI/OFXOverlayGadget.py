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

	def __init__( self, interact, viewportGadget ) :

		GafferUI.Gadget.__init__( self )
		self.__interact = interact
		self.__viewportGadget = viewportGadget
		self.__lastViewportSize = ( -1, -1 )

	def renderLayer( self, layer, style, renderReason ) :

		if self.__interact is None :
			return

		# Only draw during the normal Draw pass.
		if renderReason != GafferUI.Gadget.RenderReason.Draw :
			return

		self.__updateViewportSize()
		self.__interact.setupGLProjection()
		try :
			self.__interact.debugDraw()
		finally :
			self.__interact.restoreGLProjection()

	def __updateViewportSize( self ) :

		vpSize = self.__viewportGadget.getViewport()
		vw, vh = int( vpSize.x ), int( vpSize.y )
		if ( vw, vh ) != self.__lastViewportSize :
			self.__interact.setViewportSize( float( vw ), float( vh ) )
			self.__lastViewportSize = ( vw, vh )

	def layerMask( self ) :

		# LayerMask is not exposed to Python - return the raw int.
		# 0x20 = OverlayFront, the topmost layer, correct for OFX overlays.
		return 0x20

	def renderBound( self ) :

		return imath.Box3f( imath.V3f( -1e9 ), imath.V3f( 1e9 ) )
