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

import Gaffer
import GafferUI
import GafferOFX

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
					with Gaffer.UndoScope( self.scriptNode() ) :
						print("button create instance plugin")
						self.getPlug().node().createPluginInstance()

