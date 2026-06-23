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

import sys
import imath
import IECore

import Gaffer
import GafferUI
import GafferImage

import GafferImageUI

import GafferOFX
from GafferOFXUI.OFXOverlayGadget import OFXOverlayGadget

def D( msg ) :
	sys.stderr.write( "OFXINTERACTOOL: " + msg + "\n" )
	sys.stderr.flush()

class OFXInteractTool( GafferUI.Tool ) :

	def __init__( self, view, name = "OFXInteractTool" ) :

		GafferUI.Tool.__init__( self, view, name )

		self.__ofxNode = None
		self.__overlayGadget = None
		self.__interact = None
		self.__inInteraction = False
		self.__overlaySetupDone = False
		self.__viewportGadget = view.viewportGadget()
		self.__buttonPressTime = None

		self.__preRenderConnection = self.__viewportGadget.preRenderSignal().connect(
			Gaffer.WeakMethod( self.__preRender )
		)
		self.plugDirtiedSignal().connect( Gaffer.WeakMethod( self.__plugDirtied ) )

		self.__viewportGadget.keyPressSignal().connectFront(
			Gaffer.WeakMethod( self.__keyPress )
		)
		self.__viewportGadget.keyReleaseSignal().connectFront(
			Gaffer.WeakMethod( self.__keyRelease )
		)

		# Hover
		self.__viewportGadget.mouseMoveSignal().connect(
			Gaffer.WeakMethod( self.__mouseMove )
		)
		# Drag interaction chain
		self.__viewportGadget.buttonPressSignal().connect(
			Gaffer.WeakMethod( self.__buttonPress )
		)
		self.__viewportGadget.buttonReleaseSignal().connect(
			Gaffer.WeakMethod( self.__buttonRelease )
		)
		self.__viewportGadget.dragBeginSignal().connect(
			Gaffer.WeakMethod( self.__dragBegin )
		)
		self.__viewportGadget.dragEnterSignal().connect(
			Gaffer.WeakMethod( self.__dragEnter )
		)
		self.__viewportGadget.dragMoveSignal().connect(
			Gaffer.WeakMethod( self.__dragMove )
		)
		self.__viewportGadget.dragEndSignal().connect(
			Gaffer.WeakMethod( self.__dragEnd )
		)

	def __plugDirtied( self, plug ) :
		if plug.isSame( self["active"] ) :
			if not self["active"].getValue() :
				self.__destroyOverlay()
			self.__viewportGadget.renderRequestSignal()( self.__viewportGadget )

	def __preRender( self, viewportGadget ) :
		if not self["active"].getValue() :
			self.__setOverlayVisible( False )
			return

		node = self.__findOFXNode()
		if node is None :
			self.__setOverlayVisible( False )
			return

		if self.__overlaySetupDone and self.__interact is not None and self.__ofxNode is not None and node.isSame( self.__ofxNode ) :
			self.__setOverlayVisible( True )
			return

		self.__destroyOverlay()
		self.__setupOverlay( node, viewportGadget )
		if self.__interact is None :
			return
		self.__overlaySetupDone = True
		self.__setOverlayVisible( True )

	def __setupOverlay( self, node, viewportGadget ) :
		self.__ofxNode = node
		self.__interact = node.getInteract()
		D( f"__setupOverlay: node={node.getName()}, interact={self.__interact}" )
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
		self.__overlaySetupDone = False

	def __setOverlayVisible( self, visible ) :
		if self.__overlayGadget is not None and self.__overlayGadget.getVisible() != visible :
			self.__overlayGadget.setVisible( visible )

	def __findOFXNode( self ) :
		view = self.view()
		visited = set()
		if isinstance( view, GafferImageUI.ImageView ) :
			result = self.__findOFXNodeFromPlug( view["in"], visited )
			return result
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

		line = viewportGadget.rasterToWorldSpace( imath.V2f( event.line.p0.x, event.line.p0.y ) )
		worldPos = line.p0
		pixelAspect = self.__ofxNode["out"]["format"].getValue().getPixelAspect()
		ofxX = worldPos.x / pixelAspect
		ofxY = worldPos.y
		return ( ofxX, ofxY )

	def __penPosViewport( self, event ) :

		vpSize = self.__viewportGadget.getViewport()
		return (
			int( event.line.p0.x ),
			int( vpSize.y - event.line.p0.y )
		)

	def __getRenderScale( self ) :

		return ( 1.0, 1.0 )

	def __mouseMove( self, gadget, event ) :

		D( "SIGNAL mouseMove" )
		if not self["active"].getValue() :
			return False
		if self.__interact is None :
			return False

		ofxPos = self.__viewportPosToOfx( gadget, event )
		renderScale = self.__getRenderScale()
		ppv = self.__penPosViewport( event )

		result = self.__interact.penMotionAction(
			self.__interact.getTime(), renderScale, ofxPos, ppv, 1.0
		)
		D( f"penMotionAction (hover) at ({ofxPos[0]:.1f},{ofxPos[1]:.1f}) returned {result}" )
		return result == 0

	def __buttonPress( self, gadget, event ) :

		D( f"SIGNAL buttonPress buttons={event.buttons} mods={event.modifiers}" )
		if not self["active"].getValue() :
			return False
		if self.__interact is None :
			return False
		if event.buttons != event.Buttons.Left or event.modifiers :
			return False

		ofxPos = self.__viewportPosToOfx( gadget, event )
		renderScale = self.__getRenderScale()
		ppv = self.__penPosViewport( event )

		self.__interact.setTime( self.__interact.getTime() )
		result = self.__interact.penDownAction(
			self.__interact.getTime(), renderScale, ofxPos, ppv, 1.0
		)
		D( f"penDownAction at ({ofxPos[0]:.1f},{ofxPos[1]:.1f}) returned {result}" )

		if result == 0 :
			self.__inInteraction = True
			self.__buttonPressTime = self.__interact.getTime()
			return True

		return False

	def __buttonRelease( self, gadget, event ) :

		if not self.__inInteraction :
			return False

		# Drag never started (mouse didn't move past threshold).
		# We need to clean up the interaction that began in
		# __buttonPress.
		self.__inInteraction = False
		self.__buttonPressTime = None

		if self.__interact is None :
			return False

		ofxPos = self.__viewportPosToOfx( gadget, event )
		renderScale = self.__getRenderScale()
		ppv = self.__penPosViewport( event )

		result = self.__interact.penUpAction(
			self.__interact.getTime(), renderScale, ofxPos, ppv, 1.0
		)
		D( f"penUpAction (no-drag release) at ({ofxPos[0]:.1f},{ofxPos[1]:.1f}) returned {result}" )
		return True

	def __dragBegin( self, gadget, event ) :

		if not self.__inInteraction :
			return None

		return { "time" : self.__buttonPressTime }

	def __dragEnter( self, gadget, event ) :

		if isinstance( event.data, dict ) and "time" in event.data :
			return True
		return False

	def __dragMove( self, gadget, event ) :

		if not self.__inInteraction :
			return True

		if self.__interact is None :
			return True

		ofxPos = self.__viewportPosToOfx( gadget, event )
		renderScale = self.__getRenderScale()
		ppv = self.__penPosViewport( event )

		result = self.__interact.penMotionAction(
			self.__interact.getTime(), renderScale, ofxPos, ppv, 1.0
		)
		D( f"penMotionAction (drag) at ({ofxPos[0]:.1f},{ofxPos[1]:.1f}) returned {result}" )
		self.__viewportGadget.renderRequestSignal()( self.__viewportGadget )
		return True

	def __dragEnd( self, gadget, event ) :

		if not self.__inInteraction :
			return True

		self.__inInteraction = False
		self.__buttonPressTime = None

		if self.__interact is None :
			return True

		ofxPos = self.__viewportPosToOfx( gadget, event )
		renderScale = self.__getRenderScale()
		ppv = self.__penPosViewport( event )

		result = self.__interact.penUpAction(
			self.__interact.getTime(), renderScale, ofxPos, ppv, 1.0
		)
		D( f"penUpAction at ({ofxPos[0]:.1f},{ofxPos[1]:.1f}) returned {result}" )
		self.__viewportGadget.renderRequestSignal()( self.__viewportGadget )
		return True

	def __keyPress( self, gadget, event ) :

		if self.__interact is None :
			return False

		key = event.key
		if key == "Escape" :
			return False

		keyString = key

		renderScale = self.__getRenderScale()

		result = self.__interact.keyDownAction(
			self.__interact.getTime(), renderScale, _gafferKeyToOfx( key ), keyString
		)
		D( f"keyDownAction returned {result}" )
		return True

	def __keyRelease( self, gadget, event ) :

		if self.__interact is None :
			return False

		key = event.key
		if key == "Escape" :
			return False

		keyString = key

		renderScale = self.__getRenderScale()

		result = self.__interact.keyUpAction(
			self.__interact.getTime(), renderScale, _gafferKeyToOfx( key ), keyString
		)
		D( f"keyUpAction returned {result}" )
		return True

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
