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

	def testCreatePluginInstance( self ) :

		scriptNode = Gaffer.ScriptNode()
		node = GafferOFX.OFXImageNode()
		scriptNode.addChild( node )

		self.assertFalse( node.createPluginInstance() )
		node["pluginId"].setValue( "uk.co.thefoundry.OfxInvertExample" )
		self.assertTrue( node.createPluginInstance() )

	def testEffectInstanceProjectSize( self ) :

		scriptNode = Gaffer.ScriptNode()
		node = GafferOFX.OFXImageNode()
		node["pluginId"].setValue( "uk.co.thefoundry.OfxInvertExample" )
		scriptNode.addChild( node )
		node.createPluginInstance()

		with scriptNode.context() :
			self.assertEqual( node.effectInstanceProjectSize(), ( 1920.0, 1080.0 ) )

			defaultFormatPlug = GafferImage.FormatPlug.acquireDefaultFormatPlug( scriptNode )
			f = GafferImage.Format( 100, 200, 2 )
			defaultFormatPlug.setValue( f )

			self.assertEqual( node.effectInstanceProjectSize(), ( 100.0, 200.0 ) )

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

		# The InvertExample describes RGBA output clips, so the output
		# includes an A channel even though the input is RGB-only.
		channels = list( n["out"]["channelNames"].getValue() )
		self.assertEqual( channels, [ "R", "G", "B", "A" ] )
		for ch in [ "R", "G", "B", "A" ] :
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

		# BasicGainPlugin describes RGBA output clips, so the output
		# includes RGB channels even though the input is A-only.
		channels = list( n["out"]["channelNames"].getValue() )
		self.assertEqual( channels, [ "R", "G", "B", "A" ] )
		for ch in [ "R", "G", "B", "A" ] :
			tile = n["out"].channelData( ch, imath.V2i( 0 ) )
			self.assertEqual( len( tile ), n["out"].tileSize() * n["out"].tileSize() )

	# -----------------------------------------------------------------------
	# Plugin-specific tests
	# -----------------------------------------------------------------------

	def testInvertPlugin( self ) :

		# Use the Foundry InvertExample which does not require a mask clip.
		scriptNode = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		scriptNode.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )
		cb["colorA"].setValue( imath.Color4f( 0.2, 0.4, 0.6, 1.0 ) )
		cb["colorB"].setValue( imath.Color4f( 0.8, 0.9, 0.3, 1.0 ) )

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "uk.co.thefoundry.OfxInvertExample" )
		self.assertTrue( n.createPluginInstance() )

		dw = n["out"]["dataWindow"].getValue()
		tileSize = n["out"].tileSize()
		tile = n["out"].channelData( "R", imath.V2i( 0 ) )
		self.assertEqual( len( tile ), tileSize * tileSize )

		# InvertExample: out = 1.0 - in
		sIn = GafferImage.Sampler( cb["out"], "R", dw )
		sOut = GafferImage.Sampler( n["out"], "R", dw )
		for x, y in [ ( 0, 0 ), ( 64, 64 ), ( 128, 128 ), ( 192, 192 ) ] :
			self.assertAlmostEqual( sOut.sample( x, y ), 1.0 - sIn.sample( x, y ), places = 5 )

		# Check the Natron Invert at least doesn't crash (it needs a Mask clip
		# that we don't currently expose, so it acts as passthrough).
		n2 = GafferOFX.OFXImageNode()
		scriptNode.addChild( n2 )
		n2["in"].setInput( cb["out"] )
		n2["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( n2.createPluginInstance() )
		dw2 = n2["out"]["dataWindow"].getValue()
		self.assertGreater( dw2.size().x, 0 )
		self.assertGreater( dw2.size().y, 0 )
		self.assertEqual(
			list( n2["parameters"].keys() ),
			[ "NatronOfxParamProcessR", "NatronOfxParamProcessG", "NatronOfxParamProcessB",
			  "NatronOfxParamProcessA", "premult", "premultChannel", "maskInvert", "mix",
			  "premultChanged" ]
		)

	def testBasicGainScale( self ) :

		scriptNode = Gaffer.ScriptNode()

		const = GafferImage.Constant()
		scriptNode.addChild( const )
		const["format"].setValue( GafferImage.Format( 128, 128 ) )
		const["color"].setValue( imath.Color4f( 0.25, 0.50, 0.75, 1.0 ) )

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["in"].setInput( const["out"] )
		n["pluginId"].setValue( "uk.co.thefoundry.BasicGainPlugin" )
		self.assertTrue( n.createPluginInstance() )

		dw = n["out"]["dataWindow"].getValue()
		tileSize = n["out"].tileSize()

		# scale = 2.0 should double the values
		n["parameters"]["scale"].setValue( 2.0 )
		tileR = n["out"].channelData( "R", imath.V2i( 0 ) )
		self.assertEqual( len( tileR ), tileSize * tileSize )
		sR = GafferImage.Sampler( n["out"], "R", dw )
		sG = GafferImage.Sampler( n["out"], "G", dw )
		sB = GafferImage.Sampler( n["out"], "B", dw )
		sA = GafferImage.Sampler( n["out"], "A", dw )
		self.assertAlmostEqual( sR.sample( 0, 0 ), 0.5, places = 5 )
		self.assertAlmostEqual( sG.sample( 0, 0 ), 1.0, places = 5 )
		self.assertAlmostEqual( sB.sample( 0, 0 ), 1.5, places = 5 )
		self.assertAlmostEqual( sA.sample( 0, 0 ), 2.0, places = 5 )

		# scale = 0.5 should halve the values.
		# Create new samplers to avoid stale tile caches.
		n["parameters"]["scale"].setValue( 0.5 )
		self.assertAlmostEqual(
			GafferImage.Sampler( n["out"], "R", dw ).sample( 0, 0 ), 0.125, places = 5
		)
		self.assertAlmostEqual(
			GafferImage.Sampler( n["out"], "G", dw ).sample( 0, 0 ), 0.25, places = 5
		)
		self.assertAlmostEqual(
			GafferImage.Sampler( n["out"], "B", dw ).sample( 0, 0 ), 0.375, places = 5
		)

	def testBoxBlurPlugin( self ) :

		scriptNode = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		scriptNode.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "uk.co.thefoundry.BoxBlurPlugin" )
		self.assertTrue( n.createPluginInstance() )

		dw = n["out"]["dataWindow"].getValue()
		tileSize = n["out"].tileSize()

		# Default size=5 should produce valid tiles
		tile = n["out"].channelData( "R", imath.V2i( 0 ) )
		self.assertEqual( len( tile ), tileSize * tileSize )

		# Different size values produce different results
		n["parameters"]["size"].setValue( 1 )
		val1 = GafferImage.Sampler( n["out"], "R", dw ).sample( 128, 128 )
		n["parameters"]["size"].setValue( 20 )
		val20 = GafferImage.Sampler( n["out"], "R", dw ).sample( 128, 128 )
		self.assertNotEqual( val1, val20, "Different blur sizes should differ" )

	def testColorBarsGenerator( self ) :

		scriptNode = Gaffer.ScriptNode()

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["pluginId"].setValue( "net.sf.openfx.ColorBars" )
		self.assertTrue( n.createPluginInstance() )

		dw = n["out"]["dataWindow"].getValue()
		fmt = n["out"]["format"].getValue()
		channels = list( n["out"]["channelNames"].getValue() )
		self.assertEqual( channels, [ "R", "G", "B", "A" ] )

		self.assertGreater( dw.size().x, 0 )
		self.assertGreater( dw.size().y, 0 )
		self.assertGreater( fmt.getDisplayWindow().size().x, 0 )
		self.assertGreater( fmt.getDisplayWindow().size().y, 0 )

		tileSize = n["out"].tileSize()
		for ch in [ "R", "G", "B", "A" ] :
			tile = n["out"].channelData( ch, imath.V2i( 0 ) )
			self.assertEqual( len( tile ), tileSize * tileSize )
			nonZero = sum( 1 for v in tile if abs( v ) > 1e-6 )
			self.assertGreater( nonZero, 0 )

		# Bars should produce distinct color stripes
		sR = GafferImage.Sampler( n["out"], "R", dw )
		sG = GafferImage.Sampler( n["out"], "G", dw )
		sB = GafferImage.Sampler( n["out"], "B", dw )
		width = dw.size().x
		colors = set()
		for x in range( 0, width, width // 16 ) :
			colors.add( (
				round( sR.sample( x, dw.min().y + 10 ), 3 ),
				round( sG.sample( x, dw.min().y + 10 ), 3 ),
				round( sB.sample( x, dw.min().y + 10 ), 3 ),
			) )
		self.assertGreaterEqual( len( colors ), 4 )

	def testDespillPlugin( self ) :

		scriptNode = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		scriptNode.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Despill" )
		self.assertTrue( n.createPluginInstance() )

		dw = n["out"]["dataWindow"].getValue()
		tileSize = n["out"].tileSize()

		for ch in [ "R", "G", "B", "A" ] :
			tile = n["out"].channelData( ch, imath.V2i( 0 ) )
			self.assertEqual( len( tile ), tileSize * tileSize )
			nonZero = sum( 1 for v in tile if abs( v ) > 1e-6 )
			self.assertGreater( nonZero, 0 )

		# With defaults (screenType=0 greenscreen, scaleGreen=-1.0)
		# green values should be reduced or unchanged but never increased.
		sInG = GafferImage.Sampler( cb["out"], "G", dw )
		sOutG = GafferImage.Sampler( n["out"], "G", dw )
		for x, y in [ ( 0, 0 ), ( 64, 64 ) ] :
			self.assertLessEqual( sOutG.sample( x, y ), sInG.sample( x, y ) + 0.001 )

	def testGodRaysPlugin( self ) :

		scriptNode = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		scriptNode.addChild( cb )
		# Use a smaller image for this render-heavy plugin
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.GodRays" )
		self.assertTrue( n.createPluginInstance() )

		dw = n["out"]["dataWindow"].getValue()
		tileSize = n["out"].tileSize()
		self.assertEqual( list( n["out"]["channelNames"].getValue() ), [ "R", "G", "B", "A" ] )
		self.assertGreater( dw.size().x, 0 )
		self.assertGreater( dw.size().y, 0 )

		for ch in [ "R", "G", "B", "A" ] :
			tile = n["out"].channelData( ch, imath.V2i( 0 ) )
			self.assertEqual( len( tile ), tileSize * tileSize )

if __name__ == "__main__" :
	unittest.main()


