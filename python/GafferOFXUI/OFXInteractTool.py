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

import IECore

import Gaffer
import GafferUI
import GafferImage

import GafferImageUI

import GafferOFX
from GafferOFXUI.OFXOverlayGadget import OFXOverlayGadget

class OFXInteractTool( GafferUI.Tool ) :

	def __init__( self, view, name = "OFXInteractTool" ) :

		GafferUI.Tool.__init__( self, view, name )

		self.__ofxNode = None
		self.__overlayGadget = None
		self.__interact = None
		self.__viewportGadget = view.viewportGadget()

		self.__preRenderConnection = self.__viewportGadget.preRenderSignal().connect(
			Gaffer.WeakMethod( self.__preRender )
		)
		self.plugDirtiedSignal().connect( Gaffer.WeakMethod( self.__plugDirtied ) )

	def __plugDirtied( self, plug ) :
		if plug.isSame( self["active"] ) :
			self.__viewportGadget.renderRequestSignal()( self.__viewportGadget )

	def __preRender( self, viewportGadget ) :
		if not self["active"].getValue() :
			self.__setOverlayVisible( False )
			return

		node = self.__findOFXNode()
		if node is None :
			self.__setOverlayVisible( False )
			return

		if node is not self.__ofxNode :
			self.__destroyOverlay()
			self.__setupOverlay( node, viewportGadget )
			if self.__interact is None :
				return

		self.__setOverlayVisible( True )

	def __setupOverlay( self, node, viewportGadget ) :
		self.__ofxNode = node
		self.__interact = node.getInteract()
		if self.__interact is None :
			self.__ofxNode = None
			return

		nodeFormat = node["out"]["format"].getValue()
		pixelAspect = nodeFormat.getPixelAspect()
		imageWidth = nodeFormat.getDisplayWindow().size().x
		imageHeight = nodeFormat.getDisplayWindow().size().y

		self.__overlayGadget = OFXOverlayGadget(
			self.__interact, viewportGadget,
			pixelAspect, imageWidth, imageHeight
		)

		overlayName = "__ofxInteractOverlay"
		existing = viewportGadget.getChild( overlayName )
		if existing is not None :
			viewportGadget.removeChild( existing )

		viewportGadget.setChild( overlayName, self.__overlayGadget )

	def __destroyOverlay( self ) :
		if self.__ofxNode is not None :
			self.__ofxNode.destroyInteract()
		overlayName = "__ofxInteractOverlay"
		existing = self.__viewportGadget.getChild( overlayName )
		if existing is not None :
			self.__viewportGadget.removeChild( existing )
		self.__overlayGadget = None
		self.__ofxNode = None
		self.__interact = None

	def __setOverlayVisible( self, visible ) :
		if self.__overlayGadget is not None :
			self.__overlayGadget.setVisible( visible )

	def __findOFXNode( self ) :
		view = self.view()
		visited = set()
		if isinstance( view, GafferImageUI.ImageView ) :
			return self.__findOFXNodeFromPlug( view["in"], visited )
		return None

	@staticmethod
	def __findOFXNodeFromPlug( plug, visited ) :

		node = plug.node()
		if node in visited :
			return None
		visited.add( node )

		if isinstance( node, GafferOFX.OFXImageNode ) :
			if node.hasOverlay() :
				return node

		for childPlug in node.children( Gaffer.Plug ) :
			if childPlug.direction() == Gaffer.Plug.Direction.In :
				inputPlug = childPlug.getInput()
				if inputPlug is not None :
					result = OFXInteractTool.__findOFXNodeFromPlug( inputPlug, visited )
					if result is not None :
						return result

		return None

	def __viewportPosToOfx( self, viewportGadget, event ) :

		w = viewportGadget.getViewport()
		# Gaffer coordinate system: (0,0) top-left, Y down
		# OFX coordinate system: (0,0) bottom-left, Y up
		ofxX = event.line.p0.x
		ofxY = w.y - event.line.p0.y
		return ( ofxX, ofxY )

	def __getRenderScale( self ) :

		return ( 1.0, 1.0 )

	def __getPressure( self, event ) :

		return 1.0

	def __buttonPress( self, viewportGadget, event ) :
		print("button press")
		if self.__interact is None :
			print("button press with no interact")
			return False

		penPos = self.__viewportPosToOfx( viewportGadget, event )
		renderScale = self.__getRenderScale()
		pressure = self.__getPressure( event )
		vpSize = viewportGadget.getViewport()
		penPosViewport = ( int( event.line.p0.x ), int( vpSize.y - event.line.p0.y ) )

		self.__interact.setTime( self.__interact.getTime() )
		result = self.__interact.penDownAction(
			self.__interact.getTime(), renderScale, penPos, penPosViewport, pressure
		)
		return result != 0

	def __buttonRelease( self, viewportGadget, event ) :

		if self.__interact is None :
			return False

		penPos = self.__viewportPosToOfx( viewportGadget, event )
		renderScale = self.__getRenderScale()
		pressure = self.__getPressure( event )
		vpSize = viewportGadget.getViewport()
		penPosViewport = ( int( event.line.p0.x ), int( vpSize.y - event.line.p0.y ) )

		result = self.__interact.penUpAction(
			self.__interact.getTime(), renderScale, penPos, penPosViewport, pressure
		)
		return result != 0

	def __mouseMove( self, viewportGadget, event ) :

		if self.__interact is None :
			return False

		penPos = self.__viewportPosToOfx( viewportGadget, event )
		renderScale = self.__getRenderScale()
		pressure = self.__getPressure( event )
		vpSize = viewportGadget.getViewport()
		penPosViewport = ( int( event.line.p0.x ), int( vpSize.y - event.line.p0.y ) )

		result = self.__interact.penMotionAction(
			self.__interact.getTime(), renderScale, penPos, penPosViewport, pressure
		)
		return result != 0

	def __keyPress( self, gadget, event ) :

		if self.__interact is None :
			return False

		key = event.key
		keyString = key

		renderScale = self.__getRenderScale()

		result = self.__interact.keyDownAction(
			self.__interact.getTime(), renderScale, _gafferKeyToOfx( key ), keyString
		)
		return result != 0

	def __keyRelease( self, gadget, event ) :

		if self.__interact is None :
			return False

		key = event.key
		keyString = key

		renderScale = self.__getRenderScale()

		result = self.__interact.keyUpAction(
			self.__interact.getTime(), renderScale, _gafferKeyToOfx( key ), keyString
		)
		return result != 0

	def __del__( self ) :
		self.__destroyOverlay()

_gafferKeyCodes = {
	"BackSpace" : 8,
	"Tab" : 9,
	"Return" : 13,
	"Escape" : 27,
	"Delete" : 127,
}

def _gafferKeyToOfx( gafferKey ) :

	if gafferKey in _gafferKeyCodes :
		return _gafferKeyCodes[gafferKey]
	if len( gafferKey ) == 1 :
		return ord( gafferKey[0] )
	return 0

IECore.registerRunTimeTyped( OFXInteractTool, typeName = "GafferOFXUI::OFXInteractTool" )
GafferUI.Tool.registerTool( "OFXInteractTool", GafferImageUI.ImageView, OFXInteractTool )

Gaffer.Metadata.registerNode(
	OFXInteractTool,
	"description",
	"""
	Tool for interacting with OFX plugin overlays (grading, roto, etc.).
	""",
	plugs = {
		"active" : [
			"boolPlugValueWidget:image", lambda plug : "gafferOFXTool.png",
		],
	}
)
