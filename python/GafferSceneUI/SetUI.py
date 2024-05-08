##########################################################################
#
#  Copyright (c) 2015, Image Engine Design Inc. All rights reserved.
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

import functools
from collections import deque

import IECore

import Gaffer
import GafferUI

import GafferScene
import GafferSceneUI

## Menu Presentation
# -----------------
# Many facilities have large numbers of sets with structured names. When
# presented in a flat list, it can be hard to navigate. The path function
# allows the name of a set to be amended or turned into a hierarchical menu
# path (ie, by returning a string containing '/'s) - creating sub-menus
# wherever Gaffer displays a list of sets.

__menuPathFunction = lambda n : n

## 'f' should be a callable that takes a set name, and returns a relative menu
## path @see IECore.MenuDefinition.
def setMenuPathFunction( f ) :

	global __menuPathFunction
	__menuPathFunction = f

def getMenuPathFunction() :

	global __menuPathFunction
	return __menuPathFunction

## Metadata
#  --------

Gaffer.Metadata.registerNode(

	GafferScene.Set,

	"description",
	"""
	Creates and edits sets of objects. Each set contains a list of paths
	to locations within the scene. After creation, sets can be used
	by the SetFilter to limit scene operations to only the members of
	a particular set.
	""",

	"layout:activator:pathsInUse", lambda node : node["paths"].getInput() is not None or len( node["paths"].getValue() ),

	plugs = {

		"mode" : [

			"description",
			"""
			Create mode creates a new set containing only the
			specified paths. If a set with the same name already
			exists, it is replaced.

			Add mode adds the specified paths to an existing set,
			keeping the paths already in the set. If the set does
			not exist yet, this is the same as create mode.

			Remove mode removes the specified paths from an
			existing set. If the set does not exist yet, nothing
			is done.
			""",

			"preset:Create", GafferScene.Set.Mode.Create,
			"preset:Add", GafferScene.Set.Mode.Add,
			"preset:Remove", GafferScene.Set.Mode.Remove,

			"plugValueWidget:type", "GafferUI.PresetsPlugValueWidget",

		],

		"name" : [

			"description",
			"""
			The name of the set that will be created or edited. Multiple sets
			may be created or modified by entering their names separated by
			spaces. Wildcards may also be used to match multiple input sets to
			be modified.
			""",

			"ui:scene:acceptsSetName", True,

		],

		"setVariable" : [

			"description",
			"""
			A context variable created to pass the name of the set
			being processed to the nodes connected to the `filter`
			plug. This can be used to vary the filter for each set.
			""",

		],

		"paths" : [

			"description",
			"""
			The paths to be added to or removed from the set.

			> Caution : This plug is deprecated and will be removed
			in a future release. No validity checks are performed on
			these paths, so it is possible to accidentally generate
			invalid sets.
			""",

			"vectorDataPlugValueWidget:dragPointer", "objects",
			"layout:visibilityActivator", "pathsInUse",

		],

		"filter" : [

			"description",
			"""
			Defines the locations to be added to or removed from the set.
			""",

		],

	}

)

Gaffer.Metadata.registerValue( Gaffer.Plug, "ui:scene:acceptsSetName:promotable", False )
Gaffer.Metadata.registerValue( Gaffer.Plug, "ui:scene:acceptsSetNames:promotable", False )
Gaffer.Metadata.registerValue( Gaffer.Plug, "ui:scene:acceptsSetExpression:promotable", False )

##########################################################################
# Right click menu for sets
# This is driven by metadata so it can be used for plugs on other
# nodes too.
##########################################################################

def __setValue( plug, value, *unused ) :

	with Gaffer.UndoScope( plug.ancestor( Gaffer.ScriptNode ) ) :
		plug.setValue( value )

def __setText( textWidget, text, *unused ) :

	textWidget.setText( text )

def __insertText( textWidget, text ) :

	if textWidget.getSelection() != ( 0, 0 ) :
		prefix = textWidget.getText()[:textWidget.getSelection()[0]]
		suffix = textWidget.getText()[textWidget.getSelection()[1]:]
	else :
		## \todo This would be unnecessary if an empty selection used the cursor position.
		prefix = textWidget.getText()[:textWidget.getCursorPosition()]
		suffix = textWidget.getText()[textWidget.getCursorPosition():]

	if prefix and prefix[-1] not in " \n" :
		text = " " + text
	if not suffix or suffix[0] != " " :
		text = text + " "

	textWidget.insertText( text )

def __scenePlugs( node ) :

	result = []
	for plug in node.children( Gaffer.Plug ) :
		if plug.direction() != plug.Direction.In :
			continue
		if isinstance( plug, Gaffer.ArrayPlug ) and len( plug ) :
			plug = plug[0]
		if isinstance( plug, GafferScene.ScenePlug ) :
			result.append( plug )

	return result

def __selectAffected( context, nodes, setExpression ) :

	result = IECore.PathMatcher()

	with context :
		for node in nodes :
			for scenePlug in __scenePlugs( node ) :
				result.addPaths( GafferScene.SetAlgo.evaluateSetExpression( setExpression, scenePlug ) )

	GafferSceneUI.ContextAlgo.setSelectedPaths( context, result )

def __popupMenu( menuDefinition, plugValueWidget ) :

	# See if plug wants a set, a list of sets, or a set expression.
	# If not, we have no work to do.

	if not hasattr( plugValueWidget, "textWidget" ) :
		return

	textWidget = plugValueWidget.textWidget()

	plug = plugValueWidget.getPlug()
	if plug is None :
		return

	destinationPlug = Gaffer.PlugAlgo.findDestination(
		plug,
		lambda p :
			p if (
				Gaffer.Metadata.value( p, "ui:scene:acceptsSetName" ) or
				Gaffer.Metadata.value( p, "ui:scene:acceptsSetNames" ) or
				Gaffer.Metadata.value( p, "ui:scene:acceptsSetExpression" )
			) else None
	)
	if destinationPlug is None :
		return

	acceptsSetName = Gaffer.Metadata.value( destinationPlug, "ui:scene:acceptsSetName" )
	acceptsSetNames = Gaffer.Metadata.value( destinationPlug, "ui:scene:acceptsSetNames" )
	acceptsSetExpression = Gaffer.Metadata.value( destinationPlug, "ui:scene:acceptsSetExpression" )
	if not acceptsSetName and not acceptsSetNames and not acceptsSetExpression :
		return

	# Get all the sets available at this point in the graph.

	node = destinationPlug.node()

	if isinstance( node, GafferScene.Filter ) :
		nodes = GafferScene.SceneAlgo.filteredNodes( node )
	else :
		nodes = { node }

	setNames = set()
	with plugValueWidget.getContext() :
		for node in nodes :
			for scenePlug in __scenePlugs( node ) :
				setNames.update( [ str( n ) for n in scenePlug["setNames"].getValue() if not str( n ).startswith( "__" ) ] )

	# Build the menus

	menuDefinition.prepend( "/SetsDivider", { "divider" : True } )

	# `Select Affected` command

	selectionSetExpression = textWidget.selectedText() or textWidget.getText()
	menuDefinition.prepend(
		"Select Affected Objects",
		{
			"command" : functools.partial(
				__selectAffected,
				plugValueWidget.getContext(),
				nodes,
				selectionSetExpression
			),
			"active" : bool( selectionSetExpression )
		}
	)

	# `Operators` menu

	if acceptsSetExpression:
		for name, operator in zip( ( "Union", "Intersection", "Difference", "In", "Containing" ), ( "|", "&", "-", "in", "containing" ) ) :
			menuDefinition.prepend(
				f"/Operators/{name}",
				{
					"command" : functools.partial( __insertText, textWidget, operator ),
					"active" : textWidget.getEditable(),
				}
			)

	# `Sets` menu

	pathFn = getMenuPathFunction()

	currentText = textWidget.getText()
	currentNames = set( currentText.split() )

	if not setNames :
		menuDefinition.prepend(
			"/Sets/No Sets Available", { "active" : False },
		)
		return

	for setName in reversed( sorted( setNames ) ) :

		if acceptsSetExpression :

			menuDefinition.prepend(
				"/Sets/{}".format( pathFn( setName ) ),
				{
					"command" : functools.partial( __insertText, textWidget, setName ),
					"active" : textWidget.getEditable(),
				}
			)

		elif acceptsSetNames :

			newNames = set( currentNames )

			if setName not in currentNames :
				newNames.add( setName )
			else :
				newNames.discard( setName )

			menuDefinition.prepend(
				"/Sets/{}".format( pathFn( setName ) ),
				{
					"command" : functools.partial( __setText, textWidget, " ".join( sorted( newNames ) ) ),
					"checkBox" : setName in currentNames,
					"active" : textWidget.getEditable(),
				}
			)

		else : # acceptsSetName

			menuDefinition.prepend(
				"/Sets/{}".format( pathFn( setName ) ),
				{
					"command" : functools.partial( __setValue, plug, setName if currentText != setName else "" ),
					"checkBox" : setName == currentText,
					"active" : textWidget.getEditable(),
				}
			)

GafferUI.PlugValueWidget.popupMenuSignal().connect( __popupMenu, scoped = False )

##########################################################################
# Gadgets
##########################################################################

def __nodeGadget( node ) :

	nodeGadget = GafferUI.StandardNodeGadget( node )
	GafferSceneUI.PathFilterUI.addObjectDropTarget( nodeGadget )
	GafferSceneUI.SetFilterUI.addSetDropTarget( nodeGadget )

	return nodeGadget

GafferUI.NodeGadget.registerNodeGadget( GafferScene.Set, __nodeGadget )
