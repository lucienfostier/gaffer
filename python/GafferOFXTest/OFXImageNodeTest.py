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
import os

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
		node["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( node.createPluginInstance() )

	def testEffectInstanceProjectSize( self ) :

		scriptNode = Gaffer.ScriptNode()
		node = GafferOFX.OFXImageNode()
		node["pluginId"].setValue( "net.sf.openfx.Invert" )
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
		invert["pluginId"].setValue( "net.sf.openfx.Invert" )
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
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
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

		# The input has no Alpha channel, so readPlugToRGBA injects
		# alpha = 1.0 for the OFX buffer.  Invert does out = 1.0 - in,
		# so the output alpha should be 0.0 everywhere.
		aSampler = GafferImage.Sampler( n["out"], "A", dw )
		self.assertAlmostEqual( aSampler.sample( 0, 0 ), 0.0, places = 5 )
		for x in [ 10, 32, 50 ] :
			self.assertAlmostEqual( aSampler.sample( x, x ), 0.0, places = 5 )

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

		# Use the Natron Invert which supports float bit depth.
		scriptNode = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		scriptNode.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )
		cb["colorA"].setValue( imath.Color4f( 0.2, 0.4, 0.6, 1.0 ) )
		cb["colorB"].setValue( imath.Color4f( 0.8, 0.9, 0.3, 1.0 ) )

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
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

	@unittest.skip("too long to render")
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

	# -----------------------------------------------------------------------
	# Mask / affects / hash isolation tests
	# -----------------------------------------------------------------------

	def testPassThrough( self ) :

		# For a filter with default params, format/dataWindow/channelNames
		# should have the same values as the input (though hashes differ
		# because the render buffer hash includes all input dependencies).

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( n.createPluginInstance() )

		self.assertEqual( cb["out"]["format"].getValue(), n["out"]["format"].getValue() )
		self.assertEqual( cb["out"]["dataWindow"].getValue(), n["out"]["dataWindow"].getValue() )
		self.assertEqual(
			list( cb["out"]["channelNames"].getValue() ),
			list( n["out"]["channelNames"].getValue() )
		)

		# Channel data value should NOT pass through (Invert modifies it)
		dw = n["out"]["dataWindow"].getValue()
		self.assertNotEqual(
			GafferImage.Sampler( cb["out"], "R", dw ).sample( 0, 0 ),
			GafferImage.Sampler( n["out"], "R", dw ).sample( 0, 0 ),
		)

	def testAffects( self ) :

		# Use a connected node to exercise the tiled affects path.
		s = Gaffer.ScriptNode()
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( n.createPluginInstance() )

		rb = n["__ofxRenderBuffer"]
		tb = n["__ofxTileBuffer"]

		# For tiled CPU plugins (net.sf.openfx.Invert is CPU + SupportsTiles=1):
		# Input format/dataWindow/channelNames metadata affects the render buffer.
		self.assertIn( rb, n.affects( n["in"]["format"] ) )
		self.assertIn( rb, n.affects( n["in"]["dataWindow"] ) )
		self.assertIn( rb, n.affects( n["in"]["channelNames"] ) )

		# Input channelData drives __ofxTileBuffer (not render buffer).
		self.assertNotIn( rb, n.affects( n["in"]["channelData"] ) )
		self.assertIn( tb, n.affects( n["in"]["channelData"] ) )

		# Tile buffer drives output channelData.
		self.assertIn( n["out"]["channelData"], n.affects( tb ) )

		# Mask clip plugs' channelData drives __ofxTileBuffer.
		self.assertNotIn( rb, n.affects( n["mask"]["channelData"] ) )
		self.assertIn( tb, n.affects( n["mask"]["channelData"] ) )

		# Mask clip metadata affects the render buffer.
		self.assertIn( rb, n.affects( n["mask"]["format"] ) )
		self.assertIn( rb, n.affects( n["mask"]["dataWindow"] ) )
		self.assertIn( rb, n.affects( n["mask"]["channelNames"] ) )

		# Plugin ID and parameters drive both tile buffer and render buffer.
		self.assertIn( tb, n.affects( n["pluginId"] ) )
		self.assertIn( rb, n.affects( n["pluginId"] ) )
		self.assertIn( tb, n.affects( n["parameters"]["mix"] ) )
		self.assertIn( rb, n.affects( n["parameters"]["mix"] ) )

		# Render buffer affects metadata outputs only (not channelData
		# for tiled path).
		self.assertIn( n["out"]["format"], n.affects( rb ) )
		self.assertIn( n["out"]["dataWindow"], n.affects( rb ) )
		self.assertIn( n["out"]["channelNames"], n.affects( rb ) )
		self.assertNotIn( n["out"]["channelData"], n.affects( rb ) )

	def testMaskPlug( self ) :

		# Plugins with a Mask clip should expose an ImagePlug input
		n = GafferOFX.OFXImageNode()
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( n.createPluginInstance() )
		self.assertTrue( "mask" in n )
		self.assertIsInstance( n["mask"], GafferImage.ImagePlug )

		# Plugins without extra clips should not have a mask plug
		n2 = GafferOFX.OFXImageNode()
		n2["pluginId"].setValue( "uk.co.thefoundry.BasicGainPlugin" )
		self.assertTrue( n2.createPluginInstance() )
		self.assertFalse( "mask" in n2 )

	def testMaskDisconnectedEqualsFullEffect( self ) :

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )
		cb["colorA"].setValue( imath.Color4f( 0.2, 0.4, 0.6, 1.0 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( n.createPluginInstance() )

		dw = n["out"]["dataWindow"].getValue()
		sIn = GafferImage.Sampler( cb["out"], "R", dw )
		sOut = GafferImage.Sampler( n["out"], "R", dw )
		self.assertAlmostEqual( sOut.sample( 0, 0 ), 1.0 - sIn.sample( 0, 0 ), places = 5 )

	def testMaskBlackEqualsPassthrough( self ) :

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )
		cb["colorA"].setValue( imath.Color4f( 0.2, 0.4, 0.6, 1.0 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( n.createPluginInstance() )

		mask = GafferImage.Constant()
		s.addChild( mask )
		mask["format"].setValue( GafferImage.Format( 256, 256 ) )
		mask["color"].setValue( imath.Color4f( 0, 0, 0, 0 ) )
		n["mask"].setInput( mask["out"] )

		dw = n["out"]["dataWindow"].getValue()
		sOut = GafferImage.Sampler( n["out"], "R", dw )
		sIn = GafferImage.Sampler( cb["out"], "R", dw )
		self.assertAlmostEqual( sOut.sample( 0, 0 ), sIn.sample( 0, 0 ), places = 5 )

	def testMaskDisconnectAfterConnect( self ) :

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )
		cb["colorA"].setValue( imath.Color4f( 0.2, 0.4, 0.6, 1.0 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( n.createPluginInstance() )

		mask = GafferImage.Constant()
		s.addChild( mask )
		mask["format"].setValue( GafferImage.Format( 256, 256 ) )

		# Connect black mask → passthrough
		mask["color"].setValue( imath.Color4f( 0, 0, 0, 0 ) )
		n["mask"].setInput( mask["out"] )
		dw = n["out"]["dataWindow"].getValue()
		self.assertAlmostEqual(
			GafferImage.Sampler( n["out"], "R", dw ).sample( 0, 0 ),
			GafferImage.Sampler( cb["out"], "R", dw ).sample( 0, 0 ),
			places = 5
		)

		# Disconnect mask → back to full invert
		n["mask"].setInput( None )
		self.assertAlmostEqual(
			GafferImage.Sampler( n["out"], "R", dw ).sample( 0, 0 ),
			1.0 - GafferImage.Sampler( cb["out"], "R", dw ).sample( 0, 0 ),
			places = 5
		)

	def testMaskGeometryFullResolution( self ) :

		# Regression test: the mask clip's image must report the same
		# components/stride as its buffer layout.  Reporting Alpha (the mask
		# clip's negotiated component) over an RGBA-interleaved buffer made
		# plugins compute pixelBytes = 4 against a rowBytes = width * 16
		# stride, reading every 4th float — masks appeared at quarter
		# horizontal resolution (garbage on square boundaries).
		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 128, 128 ) )
		cb["colorA"].setValue( imath.Color4f( 0.2, 0.4, 0.6, 1.0 ) )
		cb["colorB"].setValue( imath.Color4f( 0.9, 0.1, 0.3, 1.0 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( n.createPluginInstance() )

		# Mask with coverage in the A channel (Gaffer's mask convention)
		mask = GafferImage.Checkerboard()
		s.addChild( mask )
		mask["format"].setValue( GafferImage.Format( 128, 128 ) )
		mask["colorA"].setValue( imath.Color4f( 0.1, 0.1, 0.1, 0.3 ) )
		mask["colorB"].setValue( imath.Color4f( 0.5, 0.5, 0.5, 0.8 ) )
		n["mask"].setInput( mask["out"] )

		dw = n["out"]["dataWindow"].getValue()
		sIn = GafferImage.Sampler( cb["out"], "R", dw )
		sMask = GafferImage.Sampler( mask["out"], "A", dw )
		sOut = GafferImage.Sampler( n["out"], "R", dw )

		# out = m * ( 1 - src ) + ( 1 - m ) * src  (mix = 1, maskInvert = 0)
		# Sample across square boundaries — positions like x = 33 read the
		# mask at x = 8 with the quarter-resolution bug.
		for y in [ 0, 16, 63 ] :
			for x in [ 0, 1, 15, 16, 31, 32, 33, 47, 63, 64, 65, 80, 96, 112, 127 ] :
				m = sMask.sample( x, y )
				src = sIn.sample( x, y )
				self.assertAlmostEqual(
					sOut.sample( x, y ), m * ( 1.0 - src ) + ( 1.0 - m ) * src,
					places = 4, msg = f"mask geometry mismatch at ({x},{y})"
				)

	def testMaskAlphaChannelUsed( self ) :

		# Masks are sampled from the A channel, not R — same Gaffer
		# convention as Grade / ColorCorrect mask inputs.
		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 128, 128 ) )
		cb["colorA"].setValue( imath.Color4f( 0.2, 0.4, 0.6, 1.0 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( n.createPluginInstance() )

		# R = 0 everywhere, but A = 1: must invert (alpha-driven mask)
		mask = GafferImage.Constant()
		s.addChild( mask )
		mask["format"].setValue( GafferImage.Format( 128, 128 ) )
		mask["color"].setValue( imath.Color4f( 0, 0, 0, 1 ) )
		n["mask"].setInput( mask["out"] )

		dw = n["out"]["dataWindow"].getValue()

		# Note: Sampler snapshots its tiles at construction, so a fresh
		# sampler is required after each mask change.
		sIn = GafferImage.Sampler( cb["out"], "R", dw )
		sOut = GafferImage.Sampler( n["out"], "R", dw )
		self.assertAlmostEqual( sOut.sample( 0, 0 ), 1.0 - sIn.sample( 0, 0 ), places = 5 )

		# R = 1 everywhere, but A = 0: must pass through
		mask["color"].setValue( imath.Color4f( 1, 1, 1, 0 ) )
		sOut = GafferImage.Sampler( n["out"], "R", dw )
		self.assertAlmostEqual( sOut.sample( 0, 0 ), sIn.sample( 0, 0 ), places = 5 )

	def testMaskHashChangesOnConnection( self ) :

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( n.createPluginInstance() )

		h1 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )

		mask = GafferImage.Constant()
		s.addChild( mask )
		mask["format"].setValue( GafferImage.Format( 256, 256 ) )
		mask["color"].setValue( imath.Color4f( 0.5, 0.5, 0.5, 1 ) )
		n["mask"].setInput( mask["out"] )

		h2 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		self.assertNotEqual( h1, h2 )

	def testMaskHashChangesOnContent( self ) :

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( n.createPluginInstance() )

		mask = GafferImage.Constant()
		s.addChild( mask )
		mask["format"].setValue( GafferImage.Format( 256, 256 ) )
		mask["color"].setValue( imath.Color4f( 1, 1, 1, 1 ) )
		n["mask"].setInput( mask["out"] )

		hWhite = n["out"].channelDataHash( "R", imath.V2i( 0 ) )

		mask["color"].setValue( imath.Color4f( 0, 0, 0, 0 ) )
		hBlack = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		self.assertNotEqual( hWhite, hBlack )

	def testRgbInputAlphaInjected( self ) :

		# When the input has no Alpha channel, readPlugToRGBA injects
		# alpha=1.0 into the OFX buffer.  Verify that the injected
		# alpha is processed correctly by the plugin.

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 128, 128 ) )
		cb["colorA"].setValue( imath.Color4f( 0.2, 0.4, 0.6, 1.0 ) )

		dc = GafferImage.DeleteChannels()
		s.addChild( dc )
		dc["in"].setInput( cb["out"] )
		dc["mode"].setValue( GafferImage.DeleteChannels.Mode.Keep )
		dc["channels"].setValue( "R G B" )
		self.assertEqual( list( dc["out"]["channelNames"].getValue() ), [ "R", "G", "B" ] )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( dc["out"] )
		n["pluginId"].setValue( "uk.co.thefoundry.BasicGainPlugin" )
		self.assertTrue( n.createPluginInstance() )
		n["parameters"]["scale"].setValue( 2.0 )

		# BasicGain multiplies all channels by scale.
		# Input has no A, so injected A=1.0 → output A should be 2.0.
		dw = n["out"]["dataWindow"].getValue()
		self.assertAlmostEqual(
			GafferImage.Sampler( n["out"], "A", dw ).sample( 0, 0 ), 2.0, places = 5
		)

		# R/G/B should be doubled as usual.
		sR = GafferImage.Sampler( n["out"], "R", dw )
		sInR = GafferImage.Sampler( cb["out"], "R", dw )
		self.assertAlmostEqual( sR.sample( 0, 0 ), sInR.sample( 0, 0 ) * 2.0, places = 5 )

	def testHashChangesWithParameter( self ) :

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "uk.co.thefoundry.BasicGainPlugin" )
		self.assertTrue( n.createPluginInstance() )

		n["parameters"]["scale"].setValue( 1.0 )
		h1 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )

		n["parameters"]["scale"].setValue( 2.0 )
		h2 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		self.assertNotEqual( h1, h2 )


	# -----------------------------------------------------------------------
	# Shadertoy tests — uses OSMesa GL rendering
	# -----------------------------------------------------------------------

	def testShadertoyDefaultShader( self ) :

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Shadertoy" )
		self.assertTrue( n.createPluginInstance() )

		dw = n["out"]["dataWindow"].getValue()
		self.assertGreater( dw.size().x, 0 )
		self.assertGreater( dw.size().y, 0 )
		self.assertEqual( list( n["out"]["channelNames"].getValue() ), [ "R", "G", "B", "A" ] )

		tileSize = n["out"].tileSize()
		for ch in [ "R", "G", "B", "A" ] :
			tile = n["out"].channelData( ch, imath.V2i( 0 ) )
			self.assertEqual( len( tile ), tileSize * tileSize )
			nonZero = sum( 1 for v in tile if abs( v ) > 1e-6 )
			self.assertGreater( nonZero, 0 )

	def testShadertoyFrameVarying( self ) :

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Shadertoy" )
		self.assertTrue( n.createPluginInstance() )

		# The default shader uses iTime in the blue channel only.
		# Blue channel should differ across frames.
		def blueAtFrame( f ) :
			ctx = Gaffer.Context()
			ctx.setFrame( f )
			with ctx :
				return n["out"].channelData( "B", imath.V2i( 0 ) )[0]

		b1 = blueAtFrame( 1 )
		b2 = blueAtFrame( 10 )
		self.assertNotEqual( b1, b2, "iTime should vary across frames" )

		# Red channel does not use iTime in the default shader
		def redAtFrame( f ) :
			ctx = Gaffer.Context()
			ctx.setFrame( f )
			with ctx :
				return n["out"].channelData( "R", imath.V2i( 0 ) )[0]

		r1 = redAtFrame( 1 )
		r2 = redAtFrame( 10 )
		self.assertEqual( r1, r2, "Red channel should be frame-independent" )

	def testShadertoyHashChangesWithSource( self ) :

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Shadertoy" )
		self.assertTrue( n.createPluginInstance() )

		h1 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )

		# Changing the imageShaderSource should invalidate the hash.
		n["parameters"]["imageShaderSource"].setValue(
			"void mainImage( out vec4 f, in vec2 v ) { f = vec4( 1, 0, 0, 1 ); }"
		)
		h2 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		self.assertNotEqual( h1, h2 )

	# -----------------------------------------------------------------------
	# Sapphire S_Blur tests
	# -----------------------------------------------------------------------

	def testSBlurPlugin( self ) :

		if not GafferOFX.GLContextManager.instance().hardwareAvailable() :
			self.skipTest( "S_Blur renders only on a hardware GPU" )

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 128, 128 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "com.genarts.sapphire.BlurSharpen.S_Blur" )
		self.assertTrue( n.createPluginInstance() )

		dw = n["out"]["dataWindow"].getValue()
		self.assertGreater( dw.size().x, 0 )
		self.assertGreater( dw.size().y, 0 )
		self.assertEqual( list( n["out"]["channelNames"].getValue() ), [ "R", "G", "B", "A" ] )

		tileSize = n["out"].tileSize()
		for ch in [ "R", "G", "B", "A" ] :
			tile = n["out"].channelData( ch, imath.V2i( 0 ) )
			self.assertEqual( len( tile ), tileSize * tileSize )
			nonZero = sum( 1 for v in tile if abs( v ) > 1e-6 )
			self.assertGreater( nonZero, 0 )

	def testSBlurPassThrough( self ) :

		if not GafferOFX.GLContextManager.instance().hardwareAvailable() :
			self.skipTest( "S_Blur renders only on a hardware GPU" )

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 128, 128 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "com.genarts.sapphire.BlurSharpen.S_Blur" )
		self.assertTrue( n.createPluginInstance() )

		self.assertEqual( cb["out"]["format"].getValue(), n["out"]["format"].getValue() )
		self.assertEqual( cb["out"]["dataWindow"].getValue(), n["out"]["dataWindow"].getValue() )

	def testSBlurHashChangesWithParam( self ) :

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 128, 128 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "com.genarts.sapphire.BlurSharpen.S_Blur" )
		self.assertTrue( n.createPluginInstance() )

		h1 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )

		# S_Blur Blur_Amount param — changing it should change the hash.
		self.assertIn( "Blur_Amount", n["parameters"] )
		n["parameters"]["Blur_Amount"].setValue( 0.5 )
		h2 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		self.assertNotEqual( h1, h2 )

	def testOpenGLEnabled( self ) :

		mgr = GafferOFX.GLContextManager.instance()
		info = f"GL backend: {mgr.backendName()}, renderer: {mgr.rendererString()}"
		print( f"GL hardware: {info}" )

		# At minimum the backend should be initialized.
		self.assertEqual( mgr.backendName(), "EGL" )
		self.assertNotEqual( mgr.rendererString(), "" )

		if not mgr.hardwareAvailable() :
			self.skipTest( "S_Blur renders only on a hardware GPU" )

		# S_Blur plugin should render with GL path when hardware is available.
		# This verifies the dual-probe gate allows openGLEnabled=1.
		s = Gaffer.ScriptNode()
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "com.genarts.sapphire.BlurSharpen.S_Blur" )
		self.assertTrue( n.createPluginInstance() )

		dw = n["out"]["dataWindow"].getValue()
		self.assertGreater( dw.size().x, 0 )
		self.assertGreater( dw.size().y, 0 )

		for ch in [ "R", "G", "B", "A" ] :
			tile = n["out"].channelData( ch, imath.V2i( 0 ) )
			nonZero = sum( 1 for v in tile if abs( v ) > 1e-6 )
			self.assertGreater( nonZero, 0 )

	# -----------------------------------------------------------------------
	# GL render mode plug tests
	# -----------------------------------------------------------------------

	def __shadertoyNode( self, s, cb ) :

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Shadertoy" )
		self.assertTrue( n.createPluginInstance() )
		return n

	def testGlRenderModePlug( self ) :

		n = GafferOFX.OFXImageNode()
		self.assertEqual( n["GLRenderMode"].getValue(), 0 )
		self.assertEqual( n["GLRenderMode"].minValue(), 0 )
		self.assertEqual( n["GLRenderMode"].maxValue(), 2 )

	def testGlRenderModeHashChanges( self ) :

		s = Gaffer.ScriptNode()
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )
		n = self.__shadertoyNode( s, cb )

		h1 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		n["GLRenderMode"].setValue( 1 )   # CPU
		h2 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		self.assertNotEqual( h1, h2 )
		n["GLRenderMode"].setValue( 0 )   # back to Auto
		h3 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		self.assertNotEqual( h2, h3 )

	def testGlRenderModeCpuAndGpuRender( self ) :

		# Note : the shared context may already be on the software device
		# when this test runs (testGlRenderModeHashChanges leaves CPU mode
		# active), so there is no assumption about the initial device.

		mgr = GafferOFX.GLContextManager.instance()

		s = Gaffer.ScriptNode()
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )
		n = self.__shadertoyNode( s, cb )

		# CPU mode rebuilds the shared context onto the software device and
		# the plugin renders through it.
		n["GLRenderMode"].setValue( 1 )
		tile = n["out"].channelData( "R", imath.V2i( 0 ) )
		nonZero = sum( 1 for v in tile if abs( v ) > 1e-6 )
		self.assertGreater( nonZero, 0 )
		self.assertIn( "llvmpipe", mgr.rendererString() )

		# Restore a hardware device if one exists, so later tests run on the
		# GPU again.  This runs even when the software assertions above fail.
		try :
			n["GLRenderMode"].setValue( 2 )
			tile = n["out"].channelData( "R", imath.V2i( 0 ) )
			self.assertGreater( sum( 1 for v in tile if abs( v ) > 1e-6 ), 0 )
			if mgr.backendName() == "EGL" :
				self.assertNotIn( "llvmpipe", mgr.rendererString() )
		finally :
			n["GLRenderMode"].setValue( 0 )

	def testGlRenderModeSwitchesOnSameNode( self ) :

		# One node rendered across every mode: Auto → CPU → GPU → Auto.
		# Regression for the bugs that only appear when a single plugin
		# instance survives a device switch:
		#   * a context rebuild left the plugin with dead GL state (CPU and
		#     GPU rendered blank / errored) — we now dispatch
		#     contextDetached + contextAttached when the context generation
		#     changes;
		#   * CPU mode for GL-capable plugins rendered through the software
		#     EGL device instead of a non-existent CPU path.

		s = Gaffer.ScriptNode()
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )
		n = self.__shadertoyNode( s, cb )

		for mode in ( 0, 1, 2, 0 ) :
			n["GLRenderMode"].setValue( mode )
			tile = n["out"].channelData( "R", imath.V2i( 0 ) )
			self.assertGreater(
				sum( 1 for v in tile if abs( v ) > 1e-6 ), 0,
				"GLRenderMode=%d produced no output" % mode
			)
			# Non-flat: catches a blank/white render (stale GL state), which
			# the non-zero assert alone would miss for a tile full of 1.0.
			self.assertGreater(
				max( tile ) - min( tile ), 1e-3,
				"GLRenderMode=%d rendered a flat image" % mode
			)

	# -----------------------------------------------------------------------
	# GL plugin chain tests
	# -----------------------------------------------------------------------

	def testMultiInstanceGLChain( self ) :

		if not GafferOFX.GLContextManager.instance().hardwareAvailable() :
			self.skipTest( "S_Blur renders only on a hardware GPU" )

		# Chain two GL-capable plugins: S_Blur → Shadertoy
		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )

		sblur = GafferOFX.OFXImageNode()
		s.addChild( sblur )
		sblur["in"].setInput( cb["out"] )
		sblur["pluginId"].setValue( "com.genarts.sapphire.BlurSharpen.S_Blur" )
		self.assertTrue( sblur.createPluginInstance() )

		shadertoy = GafferOFX.OFXImageNode()
		s.addChild( shadertoy )
		shadertoy["in"].setInput( sblur["out"] )
		shadertoy["pluginId"].setValue( "net.sf.openfx.Shadertoy" )
		self.assertTrue( shadertoy.createPluginInstance() )

		dw = shadertoy["out"]["dataWindow"].getValue()
		self.assertGreater( dw.size().x, 0 )
		self.assertGreater( dw.size().y, 0 )

		tileSize = shadertoy["out"].tileSize()
		for ch in [ "R", "G", "B", "A" ] :
			tile = shadertoy["out"].channelData( ch, imath.V2i( 0 ) )
			self.assertEqual( len( tile ), tileSize * tileSize )
			nonZero = sum( 1 for v in tile if abs( v ) > 1e-6 )
			self.assertGreater( nonZero, 0 )

	def testCPUtoGLChain( self ) :

		# Chain a CPU plugin through a GL plugin: BasicGain → Shadertoy
		s = Gaffer.ScriptNode()

		const = GafferImage.Constant()
		s.addChild( const )
		const["format"].setValue( GafferImage.Format( 64, 64 ) )
		const["color"].setValue( imath.Color4f( 0.1, 0.3, 0.5, 1.0 ) )

		gain = GafferOFX.OFXImageNode()
		s.addChild( gain )
		gain["in"].setInput( const["out"] )
		gain["pluginId"].setValue( "uk.co.thefoundry.BasicGainPlugin" )
		self.assertTrue( gain.createPluginInstance() )
		gain["parameters"]["scale"].setValue( 2.0 )

		shadertoy = GafferOFX.OFXImageNode()
		s.addChild( shadertoy )
		shadertoy["in"].setInput( gain["out"] )
		shadertoy["pluginId"].setValue( "net.sf.openfx.Shadertoy" )
		self.assertTrue( shadertoy.createPluginInstance() )

		dw = shadertoy["out"]["dataWindow"].getValue()
		self.assertGreater( dw.size().x, 0 )
		self.assertGreater( dw.size().y, 0 )

		# CPU output should be correct (0.1*2 = 0.2 for R)
		gainDw = gain["out"]["dataWindow"].getValue()
		self.assertAlmostEqual(
			GafferImage.Sampler( gain["out"], "R", gainDw ).sample( 0, 0 ), 0.2, places = 5
		)

		# Shadertoy should produce non-zero tiles from the CPU plugin's output
		tileSize = shadertoy["out"].tileSize()
		for ch in [ "R", "G", "B", "A" ] :
			tile = shadertoy["out"].channelData( ch, imath.V2i( 0 ) )
			self.assertEqual( len( tile ), tileSize * tileSize )
			nonZero = sum( 1 for v in tile if abs( v ) > 1e-6 )
			self.assertGreater( nonZero, 0 )

	def testColorBarsSpatialVariation( self ) :

		# Verify ColorBars produces different values at different positions.
		# The full-frame generator fallback should produce proper color bars.
		s = Gaffer.ScriptNode()
		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["pluginId"].setValue( "net.sf.openfx.ColorBars" )
		self.assertTrue( n.createPluginInstance() )

		dw = n["out"]["dataWindow"].getValue()
		self.assertGreater( dw.size().x, 128 )
		self.assertGreater( dw.size().y, 128 )

		# Sample at two distant positions — should differ
		sR = GafferImage.Sampler( n["out"], "R", dw )
		v0 = sR.sample( 0, 0 )
		v1 = sR.sample( 500, 500 )
		self.assertNotAlmostEqual( v0, v1, places = 4 )

	def testFrameBlendTemporalCache( self ) :

		# FrameBlend has temporal clip access. Verify that the tiled
		# path correctly pre-fetches the needed frame range into the
		# frame cache, producing different hashes at different frames.
		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )

		fb = GafferOFX.OFXImageNode()
		s.addChild( fb )
		fb["in"].setInput( cb["out"] )
		fb["pluginId"].setValue( "net.sf.openfx.FrameBlend" )
		self.assertTrue( fb.createPluginInstance() )

		# Verify default params loaded from descriptor
		fr = fb["parameters"]["frameRange"].getValue()
		self.assertEqual( fr.x, -5 )
		self.assertEqual( fr.y, 0 )
		self.assertFalse( fb["parameters"]["absolute"].getValue() )

		# Verify format/dataWindow pass through
		fmt = fb["out"]["format"].getValue()
		self.assertEqual( fmt.getDisplayWindow(), imath.Box2i( imath.V2i( 0 ), imath.V2i( 256 ) ) )

		dw = fb["out"]["dataWindow"].getValue()
		self.assertGreater( dw.size().x, 0 )
		self.assertGreater( dw.size().y, 0 )

		# Verify tiles at different positions are readable
		ts = fb["out"].tileSize()
		for tx in [ 0, ts ] :
			for ty in [ 0, ts ] :
				tile = fb["out"].channelData( "R", imath.V2i( tx, ty ) )
				self.assertEqual( len( tile ), ts * ts )

	# -----------------------------------------------------------------------
	# Cancellation tests — verify that a cancelled compute never populates
	# the cache with black/garbage data.
	# -----------------------------------------------------------------------

	def testCancellationDoesNotPoisonCache( self ) :

		# Tiled CPU path: BasicGain is a tiled CPU filter.
		# A cancelled Gaffer pull inside fetchInputImage should propagate
		# IECore.Cancelled, and subsequent renders should return correct data.
		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 128, 128 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "uk.co.thefoundry.BasicGainPlugin" )
		self.assertTrue( n.createPluginInstance() )

		# Warmup render — fill the input cache so the actual test
		# exercises the OFX pull path, not the upstream Gaffer cache.
		warmup = n["out"].channelData( "R", imath.V2i( 0 ) )
		self.assertGreater( len( warmup ), 0 )

		# Cancel the next compute.
		canceller = IECore.Canceller()
		canceller.cancel()
		ctx = Gaffer.Context( s.context(), canceller )
		with ctx :
			with self.assertRaises( IECore.Cancelled ) :
				n["out"].channelData( "R", imath.V2i( 0 ) )

		# After the cancelled compute, the cache should NOT contain a
		# black/stale tile.  A subsequent pull should return correct values.
		tile = n["out"].channelData( "R", imath.V2i( 0 ) )
		nonZero = sum( 1 for v in tile if abs( v ) > 1e-6 )
		self.assertGreater( nonZero, 0 )

		# Verify the node's rendering flag was correctly reset.
		self.assertFalse( n.rendering() )

	def testCancellationFullFrame( self ) :

		# Full-frame path (non-tiled): ColorBars is a generator without
		# tiling.  Cancellation should also leave no stale cache entry.
		s = Gaffer.ScriptNode()

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["pluginId"].setValue( "net.sf.openfx.ColorBars" )
		self.assertTrue( n.createPluginInstance() )

		# Warmup
		dw = n["out"]["dataWindow"].getValue()
		self.assertGreater( dw.size().x, 0 )

		canceller = IECore.Canceller()
		canceller.cancel()
		ctx = Gaffer.Context( s.context(), canceller )
		with ctx :
			with self.assertRaises( IECore.Cancelled ) :
				n["out"].channelData( "R", imath.V2i( 0 ) )

		# Subsequent render should be correct
		tile = n["out"].channelData( "R", imath.V2i( 0 ) )
		nonZero = sum( 1 for v in tile if abs( v ) > 1e-6 )
		self.assertGreater( nonZero, 0 )

		self.assertFalse( n.rendering() )

	def testParallelTileRendering( self ) :

		if os.cpu_count() == 1 :
			self.skipTest( "single-core" )

		s = Gaffer.ScriptNode()
		c = GafferImage.Constant()
		s.addChild( c )
		c["format"].setValue( GafferImage.Format( 2048, 2048 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( c["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Invert" )
		self.assertTrue( n.createPluginInstance() )

		# ImageAlgo.image uses parallelProcessTiles internally.
		GafferImage.ImageAlgo.image( n["out"] )

	def testFrameHoldIdentityTiled( self ) :

		# FrameHold is an identity effect — its render() is a no-op and
		# the host must copy the input clip at identityTime instead of
		# rendering (the tiled path used to skip the identity check and
		# produced black tiles).

		s = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.FrameHold" )
		self.assertTrue( n.createPluginInstance() )

		def nonZero( channel = "R" ) :
			tile = n["out"].channelData( channel, imath.V2i( 0 ) )
			return sum( 1 for v in tile if abs( v ) > 1e-6 )

		def checkIdentity( channel ) :
			dw = n["out"]["dataWindow"].getValue()
			sampN = GafferImage.Sampler( n["out"], channel, dw )
			sampIn = GafferImage.Sampler( cb["out"], channel, dw )
			for y in ( 0, dw.size().y - 1 ) :
				for x in ( 0, dw.size().x - 1 ) :
					self.assertAlmostEqual(
						sampN.sample( x, y ), sampIn.sample( x, y ), places = 5,
						msg = "%s mismatch at (%d,%d)" % ( channel, x, y )
					)

		with Gaffer.Context() as ctx :
			ctx["frame"] = 1.0
			self.assertGreater( nonZero(), 0 )	# not black
			checkIdentity( "R" )
			checkIdentity( "A" )

			# Different output frames still produce the held-frame image.
			ctx["frame"] = 5.0
			self.assertGreater( nonZero(), 0 )
			checkIdentity( "R" )

		# Changing the held frame (a param) invalidates the output hash.
		h1 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		n["parameters"]["firstFrame"].setValue( 42 )
		h2 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		self.assertNotEqual( h1, h2 )

		with Gaffer.Context() as ctx :
			ctx["frame"] = 1.0
			self.assertGreater( nonZero(), 0 )
			checkIdentity( "R" )

	def testFrameHoldNoAlphaInput( self ) :

		# Identity copy of an RGB-only input must not pull the missing
		# alpha channel (readers raise for absent channels) — the output
		# alpha is injected as 1.0, matching the render path.

		s = Gaffer.ScriptNode()

		r = GafferImage.ImageReader()
		s.addChild( r )
		r["fileName"].setValue(
			os.path.dirname( __file__ ) + "/../GafferImageTest/images/rgb.100x100.jpg"
		)
		self.assertEqual( list( r["out"]["channelNames"].getValue() ), [ "R", "G", "B" ] )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( r["out"] )
		n["pluginId"].setValue( "net.sf.openfx.FrameHold" )
		self.assertTrue( n.createPluginInstance() )

		with Gaffer.Context() as ctx :
			ctx["frame"] = 1.0

			tileR = n["out"].channelData( "R", imath.V2i( 0 ) )
			self.assertGreater( sum( 1 for v in tileR if abs( v ) > 1e-6 ), 0 )

			# Injected alpha (sampled inside the data window — pixels
			# outside the window are zero by convention).
			dw = n["out"]["dataWindow"].getValue()
			sampA = GafferImage.Sampler( n["out"], "A", dw )
			for y in ( dw.min().y, dw.max().y - 1 ) :
				for x in ( dw.min().x, dw.max().x - 1 ) :
					self.assertAlmostEqual( sampA.sample( x, y ), 1.0, places = 5 )

			# R data matches the reader's frame at identityTime
			dw = n["out"]["dataWindow"].getValue()
			sampN = GafferImage.Sampler( n["out"], "R", dw )
			sampR = GafferImage.Sampler( r["out"], "R", dw )
			self.assertAlmostEqual(
				sampN.sample( dw.min().x, dw.min().y ),
				sampR.sample( dw.min().x, dw.min().y ), places = 5
			)

	def testColorLookupPlugin( self ) :

		# ColorLookup uses kOfxParamTypeParametric, exercised through the
		# parametric suite. The lookupTable param must surface as a
		# ValuePlug containing RampffPlug children (one per dimension),
		# and the plugin must render as a straight passthrough with the
		# identity LUT.

		scriptNode = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		scriptNode.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )
		cb["colorA"].setValue( imath.Color4f( 0.2, 0.3, 0.4, 1.0 ) )

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["pluginId"].setValue( "net.sf.openfx.ColorLookupPlugin" )
		self.assertTrue( n.createPluginInstance() )
		n["in"].setInput( cb["out"] )

		lt = n["parameters"].getChild( "lookupTable" )
		self.assertIsInstance( lt, Gaffer.ValuePlug )
		# 5 dimensions: master + RGB + alpha
		self.assertEqual( len( lt.children() ), 5 )
		for i in range( 5 ) :
			child = lt.getChild( "curve{}".format( i ) )
			self.assertIsInstance( child, Gaffer.RampffPlug )
			# Each ramp starts with 2 identity control points: (0,0) and (1,1)
			self.assertEqual( child.numPoints(), 2 )
			self.assertAlmostEqual( child.pointXPlug( 0 ).getValue(), 0.0, places = 4 )
			self.assertAlmostEqual( child.pointYPlug( 0 ).getValue(), 0.0, places = 4 )
			self.assertAlmostEqual( child.pointXPlug( 1 ).getValue(), 1.0, places = 4 )
			self.assertAlmostEqual( child.pointYPlug( 1 ).getValue(), 1.0, places = 4 )

		dw = n["out"]["dataWindow"].getValue()
		tile = n["out"].channelData( "R", imath.V2i( 0 ) )
		self.assertAlmostEqual( tile[0], 0.2, places = 4 )  # colorA.x, identity LUT is a passthrough

	def testColorLookupCurveAffectsOutput( self ) :

		# Flipping the master curve must invert the image, and invalidate
		# the output hash so the render buffer is re-computed.

		scriptNode = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		scriptNode.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )
		cb["colorA"].setValue( imath.Color4f( 0.2, 0.3, 0.4, 1.0 ) )

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["pluginId"].setValue( "net.sf.openfx.ColorLookupPlugin" )
		self.assertTrue( n.createPluginInstance() )
		n["in"].setInput( cb["out"] )

		lt = n["parameters"].getChild( "lookupTable" )
		master = lt.getChild( "curve0" )
		h1 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		self.assertAlmostEqual( n["out"].channelData( "R", imath.V2i( 0 ) )[0], 0.2, places = 4 )

		# Flip the master curve: (0,1) -> (1,0)
		with Gaffer.UndoScope( scriptNode ) :
			master.clearPoints()
			master.addPoint()
			master.pointXPlug( 0 ).setValue( 0.0 )
			master.pointYPlug( 0 ).setValue( 1.0 )
			master.addPoint()
			master.pointXPlug( 1 ).setValue( 1.0 )
			master.pointYPlug( 1 ).setValue( 0.0 )

		h2 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		self.assertNotEqual( h1, h2 )
		self.assertAlmostEqual( n["out"].channelData( "R", imath.V2i( 0 ) )[0], 0.8, places = 4 )

	def testColorCorrectDescribedDefaults( self ) :

		# ColorCorrect's toneRanges has described (non-identity) defaults.
		# Verify the RampffPlug children carry these described defaults,
		# not hardcoded identity curves.

		scriptNode = Gaffer.ScriptNode()

		cb = GafferImage.Checkerboard()
		scriptNode.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )

		n = GafferOFX.OFXImageNode()
		scriptNode.addChild( n )
		n["pluginId"].setValue( "net.sf.openfx.ColorCorrectPlugin" )
		self.assertTrue( n.createPluginInstance() )

		tr = n["parameters"].getChild( "toneRanges" )
		self.assertIsInstance( tr, Gaffer.ValuePlug )
		self.assertEqual( len( tr.children() ), 2 )

		# Shadow curve (dimension 0): (0,1)→(0.09,0)→(1,1)
		shadow = tr.getChild( "curve0" )
		self.assertIsInstance( shadow, Gaffer.RampffPlug )
		self.assertEqual( shadow.numPoints(), 3 )
		self.assertAlmostEqual( shadow.pointXPlug( 0 ).getValue(), 0.0, places = 4 )
		self.assertAlmostEqual( shadow.pointYPlug( 0 ).getValue(), 1.0, places = 4 )
		self.assertAlmostEqual( shadow.pointXPlug( 1 ).getValue(), 0.09, places = 4 )
		self.assertAlmostEqual( shadow.pointYPlug( 1 ).getValue(), 0.0, places = 4 )
		self.assertAlmostEqual( shadow.pointXPlug( 2 ).getValue(), 1.0, places = 4 )
		self.assertAlmostEqual( shadow.pointYPlug( 2 ).getValue(), 1.0, places = 4 )

		# Highlight curve (dimension 1): (0,0)→(0.5,0)→(1,1)
		highlight = tr.getChild( "curve1" )
		self.assertIsInstance( highlight, Gaffer.RampffPlug )
		self.assertEqual( highlight.numPoints(), 3 )
		self.assertAlmostEqual( highlight.pointXPlug( 0 ).getValue(), 0.0, places = 4 )
		self.assertAlmostEqual( highlight.pointYPlug( 0 ).getValue(), 0.0, places = 4 )
		self.assertAlmostEqual( highlight.pointXPlug( 1 ).getValue(), 0.5, places = 4 )
		self.assertAlmostEqual( highlight.pointYPlug( 1 ).getValue(), 0.0, places = 4 )
		self.assertAlmostEqual( highlight.pointXPlug( 2 ).getValue(), 1.0, places = 4 )
		self.assertAlmostEqual( highlight.pointYPlug( 2 ).getValue(), 1.0, places = 4 )

	def testStringParam( self ) :

		# Verify HostSupport StringInstance getV uses the base
		# _returnValue correctly (ABI match). Shadertoy's
		# imageShaderSource is kOfxParamTypeString.

		s = Gaffer.ScriptNode()
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )

		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "net.sf.openfx.Shadertoy" )
		self.assertTrue( n.createPluginInstance() )

		srcPlug = n["parameters"]["imageShaderSource"]
		self.assertIsInstance( srcPlug, Gaffer.StringPlug )

		# Default value round-trips via get()
		defaultVal = srcPlug.getValue()
		self.assertGreater( len( defaultVal ), 0 )
		self.assertIn( "mainImage", defaultVal )

		# Set new string, verify plug and hash, and that render
		# (which calls paramGetValue/getV -> _returnValue.c_str())
		# sees the new value.
		newSrc = "void mainImage( out vec4 f, in vec2 v ) { f = vec4( 1, 0, 0, 1 ); }"
		h1 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		srcPlug.setValue( newSrc )
		self.assertEqual( srcPlug.getValue(), newSrc )
		h2 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		self.assertNotEqual( h1, h2 )

		dw = n["out"]["dataWindow"].getValue()
		sR = GafferImage.Sampler( n["out"], "R", dw )
		self.assertAlmostEqual( sR.sample( 0, 0 ), 1.0, places = 4 )

		# Restore default restores hash/output
		srcPlug.setValue( defaultVal )
		h3 = n["out"].channelDataHash( "R", imath.V2i( 0 ) )
		self.assertNotEqual( h2, h3 )

	def testParametricPollutionReverseOrder( self ) :

		# Reproduce ColorLookup test-order pollution: mutate one
		# instance's curves, destroy it, and verify a fresh instance
		# still sees described identity defaults (descriptor must not
		# have been mutated by the previous instance's suite writes).
		s = Gaffer.ScriptNode()
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )
		cb["colorA"].setValue( imath.Color4f( 0.2, 0.3, 0.4, 1.0 ) )

		n1 = GafferOFX.OFXImageNode()
		s.addChild( n1 )
		n1["pluginId"].setValue( "net.sf.openfx.ColorLookupPlugin" )
		self.assertTrue( n1.createPluginInstance() )
		n1["in"].setInput( cb["out"] )

		# Mutate master curve away from identity
		lt1 = n1["parameters"].getChild( "lookupTable" )
		master1 = lt1.getChild( "curve0" )
		with Gaffer.UndoScope( s ) :
			master1.clearPoints()
			master1.addPoint()
			master1.pointXPlug( 0 ).setValue( 0.0 )
			master1.pointYPlug( 0 ).setValue( 1.0 )
			master1.addPoint()
			master1.pointXPlug( 1 ).setValue( 1.0 )
			master1.pointYPlug( 1 ).setValue( 0.0 )
		self.assertAlmostEqual( n1["out"].channelData( "R", imath.V2i( 0 ) )[0], 0.8, places = 4 )

		# Destroy n1 (remove from script, drop instance)
		s.removeChild( n1 )
		del n1

		# Fresh instance must see identity again, not the flipped curve
		n2 = GafferOFX.OFXImageNode()
		s.addChild( n2 )
		n2["pluginId"].setValue( "net.sf.openfx.ColorLookupPlugin" )
		self.assertTrue( n2.createPluginInstance() )
		n2["in"].setInput( cb["out"] )
		lt2 = n2["parameters"].getChild( "lookupTable" )
		master2 = lt2.getChild( "curve0" )
		self.assertEqual( master2.numPoints(), 2 )
		self.assertAlmostEqual( master2.pointXPlug( 0 ).getValue(), 0.0, places = 4 )
		self.assertAlmostEqual( master2.pointYPlug( 0 ).getValue(), 0.0, places = 4 )
		self.assertAlmostEqual( master2.pointXPlug( 1 ).getValue(), 1.0, places = 4 )
		self.assertAlmostEqual( master2.pointYPlug( 1 ).getValue(), 1.0, places = 4 )
		self.assertAlmostEqual( n2["out"].channelData( "R", imath.V2i( 0 ) )[0], 0.2, places = 4 )

	def testParametricDescriptorImmutability( self ) :

		# Two concurrent instances sharing the same context descriptor:
		# mutating one must not affect the other's defaults, and
		# destroying the first must not pollute a later instance.
		s = Gaffer.ScriptNode()
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )

		n1 = GafferOFX.OFXImageNode()
		s.addChild( n1 )
		n1["pluginId"].setValue( "net.sf.openfx.ColorLookupPlugin" )
		self.assertTrue( n1.createPluginInstance() )
		lt1 = n1["parameters"].getChild( "lookupTable" )

		n2 = GafferOFX.OFXImageNode()
		s.addChild( n2 )
		n2["pluginId"].setValue( "net.sf.openfx.ColorLookupPlugin" )
		self.assertTrue( n2.createPluginInstance() )
		lt2 = n2["parameters"].getChild( "lookupTable" )

		# Both start identity
		for lt in ( lt1, lt2 ) :
			m = lt.getChild( "curve0" )
			self.assertEqual( m.numPoints(), 2 )
			self.assertAlmostEqual( m.pointYPlug( 0 ).getValue(), 0.0, places = 4 )
			self.assertAlmostEqual( m.pointYPlug( 1 ).getValue(), 1.0, places = 4 )

		# Mutate n1 only
		m1 = lt1.getChild( "curve0" )
		with Gaffer.UndoScope( s ) :
			m1.clearPoints()
			m1.addPoint()
			m1.pointXPlug( 0 ).setValue( 0.0 )
			m1.pointYPlug( 0 ).setValue( 1.0 )
			m1.addPoint()
			m1.pointXPlug( 1 ).setValue( 1.0 )
			m1.pointYPlug( 1 ).setValue( 0.0 )

		# n2 must remain identity
		m2 = lt2.getChild( "curve0" )
		self.assertAlmostEqual( m2.pointYPlug( 0 ).getValue(), 0.0, places = 4 )
		self.assertAlmostEqual( m2.pointYPlug( 1 ).getValue(), 1.0, places = 4 )

		# Fresh n3 after n1 destruction also identity
		s.removeChild( n1 )
		del n1
		n3 = GafferOFX.OFXImageNode()
		s.addChild( n3 )
		n3["pluginId"].setValue( "net.sf.openfx.ColorLookupPlugin" )
		self.assertTrue( n3.createPluginInstance() )
		m3 = n3["parameters"].getChild( "lookupTable" ).getChild( "curve0" )
		self.assertAlmostEqual( m3.pointYPlug( 0 ).getValue(), 0.0, places = 4 )
		self.assertAlmostEqual( m3.pointYPlug( 1 ).getValue(), 1.0, places = 4 )

	def testLoadPluginExplicit( self ) :

		# loadPlugin(pluginId) is the explicit authoring entry point
		# (Shader::loadShader mold): descriptor walk + setupPlugs +
		# instance creation in one UndoScope.
		s = Gaffer.ScriptNode()
		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		self.assertTrue( n.loadPlugin( "uk.co.thefoundry.BasicGainPlugin", True ) )
		self.assertEqual( n["pluginId"].getValue(), "uk.co.thefoundry.BasicGainPlugin" )
		self.assertIn( "scale", n["parameters"] )
		self.assertIsInstance( n["parameters"]["scale"], Gaffer.FloatPlug )

		# Render works without extra createPluginInstance
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )
		n["in"].setInput( cb["out"] )
		n["parameters"]["scale"].setValue( 2.0 )
		tile = n["out"].channelData( "R", imath.V2i( 0 ) )
		self.assertGreater( sum( 1 for v in tile if abs( v ) > 1e-6 ), 0 )

	def testLoadPluginKeepValues( self ) :

		# keepExistingValues=True preserves user values/connections
		# for compatible params; False resets to defaults.
		s = Gaffer.ScriptNode()
		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		self.assertTrue( n.loadPlugin( "uk.co.thefoundry.BasicGainPlugin", True ) )
		n["parameters"]["scale"].setValue( 3.0 )

		# Reload same plugin keeping values
		self.assertTrue( n.loadPlugin( "uk.co.thefoundry.BasicGainPlugin", True ) )
		self.assertAlmostEqual( n["parameters"]["scale"].getValue(), 3.0, places = 5 )

		# Reload resetting to defaults
		self.assertTrue( n.loadPlugin( "uk.co.thefoundry.BasicGainPlugin", False ) )
		# BasicGain default scale is 1.0 (check descriptor default preserved)
		self.assertAlmostEqual( n["parameters"]["scale"].getValue(), 1.0, places = 5 )

	def testLoadPluginDescribedDefaults( self ) :

		# Fresh loadPlugin must carry described defaults (ColorLookup
		# identity, ColorCorrect non-identity) via setupPlugs + instance.
		s = Gaffer.ScriptNode()
		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		self.assertTrue( n.loadPlugin( "net.sf.openfx.ColorLookupPlugin", True ) )
		lt = n["parameters"].getChild( "lookupTable" )
		self.assertEqual( lt.getChild( "curve0" ).numPoints(), 2 )

		n2 = GafferOFX.OFXImageNode()
		s.addChild( n2 )
		self.assertTrue( n2.loadPlugin( "net.sf.openfx.ColorCorrectPlugin", True ) )
		tr = n2["parameters"].getChild( "toneRanges" )
		shadow = tr.getChild( "curve0" )
		self.assertEqual( shadow.numPoints(), 3 )
		self.assertAlmostEqual( shadow.pointXPlug( 1 ).getValue(), 0.09, places = 4 )

	def testPlugSetDoesNotAutoCreate( self ) :

		# plugSet(pluginId) must only destroy, not create (explicit design).
		# Setting pluginId alone leaves no params; render lazily binds.
		s = Gaffer.ScriptNode()
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )
		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		n["pluginId"].setValue( "uk.co.thefoundry.BasicGainPlugin" )
		# No auto-created params yet
		self.assertNotIn( "scale", n["parameters"] )
		# Lazy ensureInstance on first use creates binding (plugs via ctor weld interim)
		tile = n["out"].channelData( "R", imath.V2i( 0 ) )
		self.assertEqual( len( tile ), n["out"].tileSize() * n["out"].tileSize() )
		self.assertIn( "scale", n["parameters"] )

	def testStalePlugRecovery( self ) :

		# Corrupt a param plug type; loadPlugin must replace with
		# warning (setupTypedPlug semantics) and render correctly.
		# This exercises Phase 3 strict validation + recovery.
		s = Gaffer.ScriptNode()
		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		self.assertTrue( n.loadPlugin( "uk.co.thefoundry.BasicGainPlugin", True ) )
		n["parameters"]["scale"].setValue( 2.0 )

		# Corrupt: replace FloatPlug scale with IntPlug (wrong type)
		bad = Gaffer.IntPlug( "scale", Gaffer.Plug.Direction.In, 0, flags = Gaffer.Plug.Flags.Default | Gaffer.Plug.Flags.Dynamic )
		# Bypass childAdded invalidation for this test by holding instance?
		# Removal triggers destroyInstance (invariant 3) — expected.
		n["parameters"].setChild( "scale", bad )
		# Instance should be destroyed by structural change
		# Reload must fix type and reset to default (user value lost, loud)
		with IECore.CapturingMessageHandler() as mh :
			self.assertTrue( n.loadPlugin( "uk.co.thefoundry.BasicGainPlugin", True ) )
		self.assertIsInstance( n["parameters"]["scale"], Gaffer.FloatPlug )
		# Renders without crash
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )
		n["in"].setInput( cb["out"] )
		tile = n["out"].channelData( "R", imath.V2i( 0 ) )
		self.assertEqual( len( tile ), n["out"].tileSize() * n["out"].tileSize() )

	def testStructuralInvalidation( self ) :

		# Deleting a param plug destroys instance; next use recreates cleanly.
		s = Gaffer.ScriptNode()
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )
		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		self.assertTrue( n.loadPlugin( "uk.co.thefoundry.BasicGainPlugin", True ) )
		n["parameters"]["scale"].setValue( 2.0 )
		tile1 = n["out"].channelData( "R", imath.V2i( 0 ) )
		self.assertGreater( sum( 1 for v in tile1 if abs( v ) > 1e-6 ), 0 )

		# Delete scale plug (structural change → destroyInstance)
		n["parameters"].removeChild( n["parameters"]["scale"] )
		self.assertNotIn( "scale", n["parameters"] )
		# Next render recreates via setup + bind (no dangling crash)
		tile2 = n["out"].channelData( "R", imath.V2i( 0 ) )
		self.assertEqual( len( tile2 ), n["out"].tileSize() * n["out"].tileSize() )
		self.assertIn( "scale", n["parameters"] )

	def testLoadPluginValueMatrix( self ) :

		# Reload semantics across types: keep=True preserves,
		# keep=False resets to described defaults.
		s = Gaffer.ScriptNode()
		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		self.assertTrue( n.loadPlugin( "net.sf.openfx.Despill", True ) )
		# Int, Float, Bool coverage
		n["parameters"]["screenType"].setValue( 1 )
		n["parameters"]["spillmapMix"].setValue( 0.9 )
		n["parameters"]["clampBlack"].setValue( False )

		self.assertTrue( n.loadPlugin( "net.sf.openfx.Despill", True ) )
		self.assertEqual( n["parameters"]["screenType"].getValue(), 1 )
		self.assertAlmostEqual( n["parameters"]["spillmapMix"].getValue(), 0.9, places = 5 )
		self.assertEqual( n["parameters"]["clampBlack"].getValue(), False )

		self.assertTrue( n.loadPlugin( "net.sf.openfx.Despill", False ) )
		self.assertEqual( n["parameters"]["screenType"].getValue(), 0 )
		self.assertAlmostEqual( n["parameters"]["spillmapMix"].getValue(), 0.5, places = 5 )
		self.assertEqual( n["parameters"]["clampBlack"].getValue(), True )

		# Invert choice/int + bool
		n2 = GafferOFX.OFXImageNode()
		s.addChild( n2 )
		self.assertTrue( n2.loadPlugin( "net.sf.openfx.Invert", True ) )
		n2["parameters"]["premultChannel"].setValue( 0 )
		n2["parameters"]["mix"].setValue( 0.5 )
		self.assertTrue( n2.loadPlugin( "net.sf.openfx.Invert", True ) )
		self.assertEqual( n2["parameters"]["premultChannel"].getValue(), 0 )
		self.assertAlmostEqual( n2["parameters"]["mix"].getValue(), 0.5, places = 5 )
		self.assertTrue( n2.loadPlugin( "net.sf.openfx.Invert", False ) )
		self.assertEqual( n2["parameters"]["premultChannel"].getValue(), 3 )
		self.assertAlmostEqual( n2["parameters"]["mix"].getValue(), 1.0, places = 5 )

	def testParallelRenderStress( self ) :

		# InstanceSafe stress: concurrent tiles must match serial.
		# BasicGain is InstanceSafe; parallel pulls must equal serial.
		import threading
		s = Gaffer.ScriptNode()
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 256, 256 ) )
		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["in"].setInput( cb["out"] )
		self.assertTrue( n.loadPlugin( "uk.co.thefoundry.BasicGainPlugin", True ) )
		n["parameters"]["scale"].setValue( 2.0 )

		dw = n["out"]["dataWindow"].getValue()
		serial = [ GafferImage.Sampler( n["out"], "R", dw ).sample( x, y ) for x, y in [ ( 0, 0 ), ( 64, 64 ), ( 128, 128 ) ] ]

		errors = []
		def worker() :
			try :
				for _ in range( 5 ) :
					t = n["out"].channelData( "R", imath.V2i( 0 ) )
					if len( t ) != n["out"].tileSize() * n["out"].tileSize() :
						errors.append( "bad tile len" )
			except Exception as e :
				errors.append( str( e ) )

		threads = [ threading.Thread( target = worker ) for _ in range( 4 ) ]
		for t in threads :
			t.start()
		for t in threads :
			t.join()
		self.assertEqual( errors, [] )
		parallel = [ GafferImage.Sampler( n["out"], "R", dw ).sample( x, y ) for x, y in [ ( 0, 0 ), ( 64, 64 ), ( 128, 128 ) ] ]
		for a, b in zip( serial, parallel ) :
			self.assertAlmostEqual( a, b, places = 5 )

	def testDestroyWindowDispatch( self ) :

		# Invariant 4: kOfxActionDestroyInstance dispatches while param
		# instances are still registered and the back-pointer is valid.
		# The DestroyWindow probe plugin reads its testValue param during
		# destroy and writes "<value> status=<stat>" to a probe file.
		# Instance dispatch yields the plug value (42); descriptor
		# fallback would yield the default (0); unregistered handles
		# would yield an error status.
		import os, shutil, tempfile

		# DestroyWindow probe ships with the dependencies (OfxMiscPlugins
		# Examples, like the other openfx-misc test plugins), so it is
		# on the standard search path — no rescan needed.
		if "com.gaffer.DestroyWindow" not in GafferOFX.Host.pluginIDs() :
			self.skipTest( "DestroyWindow probe plugin not installed" )

		workDir = tempfile.mkdtemp( prefix = "ofxDestroyWindow" )
		self.addCleanup( shutil.rmtree, workDir, True )
		probePath = os.path.join( workDir, "probe" )
		os.environ["OFX_DESTROY_WINDOW_PROBE"] = probePath
		self.addCleanup( os.environ.pop, "OFX_DESTROY_WINDOW_PROBE", None )

		s = Gaffer.ScriptNode()
		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		self.assertTrue( n.loadPlugin( "com.gaffer.DestroyWindow", True ) )
		n["parameters"]["testValue"].setValue( 42 )
		# Destroy via pluginId change (plugSet → destroyInstance → shutdown)
		n["pluginId"].setValue( "" )
		with open( probePath ) as f :
			self.assertEqual( f.read().strip(), "42 status=0" )

	def testActionGate( self ) :

		# Suite writes allowed only during create/instanceChanged/
		# syncPrivateData/interact; blocked during render.
		s = Gaffer.ScriptNode()
		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		self.assertTrue( n.loadPlugin( "uk.co.thefoundry.BasicGainPlugin", True ) )

		# Empty action (UI/host path) allows
		self.assertEqual( GafferOFX._currentTestAction(), "" )
		self.assertTrue( n._testAllowParamSet( "scale" ) )

		# Render blocks (warnings expected — capture them)
		GafferOFX._pushTestAction( "OfxImageEffectActionRender" )
		try :
			self.assertEqual( GafferOFX._currentTestAction(), "OfxImageEffectActionRender" )
			with IECore.CapturingMessageHandler() as mh :
				self.assertFalse( n._testAllowParamSet( "scale" ) )
			# Plug untouched by gate itself
			before = n["parameters"]["scale"].getValue()
			with IECore.CapturingMessageHandler() as mh :
				self.assertFalse( n._testAllowParamSet( "scale" ) )
			self.assertEqual( n["parameters"]["scale"].getValue(), before )
		finally :
			GafferOFX._popTestAction()
		self.assertEqual( GafferOFX._currentTestAction(), "" )

		# Legal writers allow
		for action in (
			"OfxActionCreateInstance",
			"OfxActionBeginInstanceChanged",
			"OfxActionInstanceChanged",
			"OfxActionEndInstanceChanged",
			"OfxActionSyncPrivateData",
			"OfxInteractActionPenMotion",
			"OfxInteractActionPenDown",
		) :
			GafferOFX._pushTestAction( action )
			try :
				self.assertTrue( n._testAllowParamSet( "scale" ), "action %s should allow" % action )
			finally :
				GafferOFX._popTestAction()

		# Render still works after gate probes (no state corruption)
		cb = GafferImage.Checkerboard()
		s.addChild( cb )
		cb["format"].setValue( GafferImage.Format( 64, 64 ) )
		n["in"].setInput( cb["out"] )
		tile = n["out"].channelData( "R", imath.V2i( 0 ) )
		self.assertEqual( len( tile ), n["out"].tileSize() * n["out"].tileSize() )

if __name__ == "__main__" :
	unittest.main()


