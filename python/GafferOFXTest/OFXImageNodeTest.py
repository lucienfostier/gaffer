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

import unittest

import imath

import IECore
import Gaffer
import GafferImage
import GafferTest
import GafferOFX

class OFXImageNodeTest( GafferTest.TestCase ) :
	def testCreatePluginInstance(self):
		import Gaffer
		import GafferOFX

		scriptNode = Gaffer.ScriptNode()
		node = GafferOFX.OFXImageNode()
		scriptNode.addChild( node )

		self.assertFalse(node.createPluginInstance())
		node["pluginId"].setValue("uk.co.thefoundry.OfxInvertExample")
		self.assertTrue(node.createPluginInstance())


	def testEffectInstanceProjectSize(self):
		import Gaffer
		import GafferOFX
		import GafferImage

		scriptNode = Gaffer.ScriptNode()
		node = GafferOFX.OFXImageNode()
		node["pluginId"].setValue("uk.co.thefoundry.OfxInvertExample")
		scriptNode.addChild( node )
		node.createPluginInstance()

		with scriptNode.context():
			self.assertEqual(node.effectInstanceProjectSize(), (1920.0, 1080.0))

			# override default format
			defaultFormatPlug = GafferImage.FormatPlug.acquireDefaultFormatPlug( scriptNode )
			f = GafferImage.Format( 100, 200, 2 )
			defaultFormatPlug.setValue( f )	

			self.assertEqual(node.effectInstanceProjectSize(), (100.0, 200.0))

			for channel in [ "R", "G", "B", "A" ] :
				channelData = node["out"].channelData( channel, imath.V2i( 0 ) )
				self.assertEqual( len( channelData ), node["out"].tileSize() * node["out"].tileSize() )

				s = GafferImage.Sampler( node["out"], channel, node["out"]["dataWindow"].getValue() )
				s.sample( 12, 12 )
				s.sample( 72, 72 )

	def testGainAt640x640( self ) :

		scriptNode = Gaffer.ScriptNode()
		c = GafferImage.Checkerboard()
		scriptNode.addChild( c )
		c["format"].setValue( GafferImage.Format( 640, 640 ) )

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["in"].setInput( c["out"] )
		n["pluginId"].setValue( "uk.co.thefoundry.BasicGainPlugin" )
		n.createPluginInstance()
		n["parameters"]["scale"].setValue( 2.0 )

		for tx in [ 0, 128, 256, 384, 512 ] :
			for ty in [ 0, 128, 256, 384, 512 ] :
				tile = n["out"].channelData( "R", imath.V2i( tx, ty ) )
				self.assertEqual( len( tile ), n["out"].tileSize() * n["out"].tileSize() )

	def testChainedOFXNodes( self ) :

		scriptNode = Gaffer.ScriptNode()
		c = GafferImage.Checkerboard()
		scriptNode.addChild( c )
		c["format"].setValue( GafferImage.Format( 640, 640 ) )

		invert = GafferOFX.OFXImageNode()
		scriptNode.addChild( invert )
		invert["in"].setInput( c["out"] )
		invert["pluginId"].setValue( "uk.co.thefoundry.OfxInvertExample" )
		invert.createPluginInstance()

		blur = GafferOFX.OFXImageNode()
		scriptNode.addChild( blur )
		blur["in"].setInput( invert["out"] )
		blur["pluginId"].setValue( "uk.co.thefoundry.BoxBlurPlugin" )
		blur.createPluginInstance()
		blur["parameters"]["size"].setValue( 10 )

		gain = GafferOFX.OFXImageNode()
		scriptNode.addChild( gain )
		gain["in"].setInput( blur["out"] )
		gain["pluginId"].setValue( "uk.co.thefoundry.BasicGainPlugin" )
		gain.createPluginInstance()
		gain["parameters"]["scale"].setValue( 2.0 )

		for tx in [ 0, 128, 256, 384, 512 ] :
			for ty in [ 0, 128, 256, 384, 512 ] :
				tile = gain["out"].channelData( "R", imath.V2i( tx, ty ) )
				self.assertEqual( len( tile ), gain["out"].tileSize() * gain["out"].tileSize() )

	def testRGBOnlyInput( self ) :

		scriptNode = Gaffer.ScriptNode()
		c = GafferImage.Checkerboard()
		scriptNode.addChild( c )
		c["format"].setValue( GafferImage.Format( 64, 64 ) )
		c["colorA"].setValue( imath.Color4f( 0.01, 0.47, 0.00, 1.0 ) )
		c["colorB"].setValue( imath.Color4f( 0.50, 0.42, 0.81, 1.0 ) )

		dc = GafferImage.DeleteChannels()
		scriptNode.addChild( dc )
		dc["in"].setInput( c["out"] )
		dc["mode"].setValue( GafferImage.DeleteChannels.Mode.Keep )
		dc["channels"].setValue( "R G B" )
		self.assertEqual( list( dc["out"]["channelNames"].getValue() ), [ "R", "G", "B" ] )

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["in"].setInput( dc["out"] )
		n["pluginId"].setValue( "uk.co.thefoundry.OfxInvertExample" )
		n.createPluginInstance()

		channels = list( n["out"]["channelNames"].getValue() )
		self.assertEqual( channels, [ "R", "G", "B" ] )
		for ch in [ "R", "G", "B" ] :
			tile = n["out"].channelData( ch, imath.V2i( 0 ) )
			self.assertEqual( len( tile ), n["out"].tileSize() * n["out"].tileSize() )

		# Verify channels produce distinct non-gray values
		dw = n["out"]["dataWindow"].getValue()
		r0 = GafferImage.Sampler( n["out"], "R", dw ).sample( 0, 0 )
		g0 = GafferImage.Sampler( n["out"], "G", dw ).sample( 0, 0 )
		b0 = GafferImage.Sampler( n["out"], "B", dw ).sample( 0, 0 )
		self.assertNotAlmostEqual( r0, g0, places = 3 )
		self.assertNotAlmostEqual( g0, b0, places = 3 )

	def testAlphaOnlyInput( self ) :

		scriptNode = Gaffer.ScriptNode()
		c = GafferImage.Checkerboard()
		scriptNode.addChild( c )
		c["format"].setValue( GafferImage.Format( 64, 64 ) )

		dc = GafferImage.DeleteChannels()
		scriptNode.addChild( dc )
		dc["in"].setInput( c["out"] )
		dc["mode"].setValue( GafferImage.DeleteChannels.Mode.Keep )
		dc["channels"].setValue( "A" )
		self.assertEqual( list( dc["out"]["channelNames"].getValue() ), [ "A" ] )

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["in"].setInput( dc["out"] )
		n["pluginId"].setValue( "uk.co.thefoundry.BasicGainPlugin" )
		n.createPluginInstance()
		n["parameters"]["scale"].setValue( 2.0 )

		channels = list( n["out"]["channelNames"].getValue() )
		self.assertEqual( channels, [ "A" ] )
		tile = n["out"].channelData( "A", imath.V2i( 0 ) )
		self.assertEqual( len( tile ), n["out"].tileSize() * n["out"].tileSize() )

if __name__ == "__main__" :
	unittest.main()


