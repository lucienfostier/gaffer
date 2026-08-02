##########################################################################
#
#  Copyright (c) 2026, Lucien Fostier. All rights reserved.
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

import threading

import Gaffer
import GafferUI
import GafferOFX
import GafferImageUI

from Qt import QtWidgets

from GafferOFXUI.OFXInteractTool import OFXInteractTool


class _MessageDialogue( GafferUI.Dialogue ) :

	# Bounded dialogue for plugin messages, which can be essays (e.g. the
	# Shadertoy help text). Full text in a scrollable view capped at a
	# sane size instead of a screen-filling label. Mirrors the
	# ConfirmationDialogue button protocol (confirm / cancel / closed).
	def __init__( self, title, message, confirmLabel = "OK", cancelLabel = None ) :

		GafferUI.Dialogue.__init__( self, title )

		with GafferUI.ListContainer( GafferUI.ListContainer.Orientation.Vertical, spacing = 8 ) as column :
			scrolled = GafferUI.ScrolledContainer()
			scrolled.addChild(
				GafferUI.MultiLineTextWidget( text = message, editable = False )
			)
			scrolled._qtWidget().setMaximumHeight( 400 )
			scrolled._qtWidget().setMaximumWidth( 600 )

		self._setWidget( column )

		if cancelLabel is not None :
			self._addButton( cancelLabel )

		self.__confirmButton = self._addButton( confirmLabel )

	def waitForConfirmation( self, **kw ) :

		self.__confirmButton._qtWidget().setFocus()
		button = self.waitForButton( **kw )

		if button is None :
			return None
		else :
			return button is self.__confirmButton


def __messageHook( type, id, message ) :

	# Dialogues need the main thread and a QApplication. Anything else
	# (worker threads, headless processes) keeps default handling -
	# already logged.
	if threading.current_thread() is not threading.main_thread() :
		return None
	if QtWidgets.QApplication.instance() is None :
		return None

	title = id if id else "OFX Plugin"

	if type == "OfxMessageQuestion" :
		dialogue = _MessageDialogue(
			title = title,
			message = message,
			cancelLabel = "No",
			confirmLabel = "Yes",
		)
		return True if dialogue.waitForConfirmation() else False

	# Errors, warnings and plain messages all pop up, matching Natron.
	# Log type is for the log only (per the OFX spec) and stays silent.
	if type in ( "OfxMessageError", "OfxMessageFatal", "OfxMessageWarning", "OfxMessageMessage" ) :
		dialogue = _MessageDialogue(
			title = title,
			message = message,
		)
		dialogue.waitForConfirmation()
		return None

	return None


def registerMessageHook() :

	# Called from GUI-only startup (startup/gui/menus.py) - never at
	# import, so headless processes (tests, farm renders) keep default
	# handling and can never block on a modal dialogue.
	GafferOFX.Host.setMessageHook( __messageHook )


Gaffer.Metadata.registerNode(

	GafferOFX.OFXImageNode,

	"description",
	"""
	Load an OFX Image Effect plugin.
	""",

	"layout:customWidget:loadButton:widgetType", "GafferOFXUI.OFXImageNodeUI._LoadButton",
	"layout:customWidget:loadButton:section", "Settings",
	"layout:customWidget:loadButton:accessory", True,
	"layout:customWidget:loadButton:index", 1,

	"layout:activator:supportsGL", lambda node : node.supportsGL(),

	plugs = {

		"pluginId" : [

			"description",
			"""
			OFX id of the plugin to load.
			Call `createPluginInstance()` or press the reload button to configure
			the `in` and `parameters` plugs to match the plugin.
			""",

			"nodule:type", "",
		],

		"parameters" : [

			"description",
			"""
			Where the parameters for the OFX plugin are represented.
			""",

			"plugValueWidget:type", "GafferUI.LayoutPlugValueWidget",

		],

		"GLRenderMode" : [

			"description",
			"""
			Renderer preference for GL-capable plugins.  Auto prefers the
			GPU and falls back to software rendering; CPU renders with the
			software device; GPU renders with the hardware device.
			""",

			"plugValueWidget:type", "GafferUI.PresetsPlugValueWidget",

			"layout:visibilityActivator", "supportsGL",

			"preset:Auto", 0,
			"preset:CPU", 1,
			"preset:GPU", 2,

		],

	}

)

class _LoadButton( GafferUI.PlugValueWidget ) :

	def __init__( self, node, **kw ) :

		button = GafferUI.Button( image = "refresh.png", hasFrame = False )
		GafferUI.PlugValueWidget.__init__( self, button, node["pluginId"], **kw )

		button.clickedSignal().connect( Gaffer.WeakMethod( self.__clicked ) )

	def __clicked( self, button ) :

		with self.context() :
			if self.getPlug().getValue() :
				with GafferUI.ErrorDialogue.ErrorHandler(
					title = "Error loading plugin",
					parentWindow = self.ancestor( GafferUI.Window )
				) :
					# loadPlugin (not createPluginInstance) re-runs descriptor
					# setup and reconciles stale parameter plugs. It scopes
					# its own undo.
					node = self.getPlug().node()
					if not node.loadPlugin( self.getPlug().getValue(), True ) :
						raise RuntimeError( "Failed to load OFX plugin \"{}\"".format( self.getPlug().getValue() ) )


class _PushButton( GafferUI.PlugValueWidget ) :

	def __init__( self, plug, **kw ) :

		label = Gaffer.Metadata.value( plug, "label" ) or plug.getName()
		button = GafferUI.Button( label )
		GafferUI.PlugValueWidget.__init__( self, button, plug, **kw )

		button.clickedSignal().connect( Gaffer.WeakMethod( self.__clicked ) )

	def __clicked( self, button ) :

		with self.context() :
			plug = self.getPlug()
			plug.setValue( not plug.getValue() )
