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
#  NONINFRINGEMENT) OR OTHERWISE ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
##########################################################################

import unittest

import IECore

import GafferTest
import GafferOFX

class HostTest( GafferTest.TestCase ) :

	def testPluginIDs( self ) :

		ids = GafferOFX.Host.pluginIDs()
		self.assertIsInstance( ids, list )
		self.assertGreater( len( ids ), 0 )
		for pluginId in ids :
			self.assertIsInstance( pluginId, str )
			self.assertGreater( len( pluginId ), 0 )

	def testPluginIDsContainExpected( self ) :

		ids = GafferOFX.Host.pluginIDs()
		self.assertIn( "net.sf.openfx.Invert", ids )
		self.assertIn( "uk.co.thefoundry.BasicGainPlugin", ids )

	def testMessageLevels( self ) :

		# Fatal / Error -> Error, Warning -> Warning, Log -> Debug, Message -> Info
		cases = [
			( "OfxMessageFatal", IECore.MessageHandler.Level.Error ),
			( "OfxMessageError", IECore.MessageHandler.Level.Error ),
			( "OfxMessageWarning", IECore.MessageHandler.Level.Warning ),
			( "OfxMessageLog", IECore.MessageHandler.Level.Debug ),
			( "OfxMessageMessage", IECore.MessageHandler.Level.Info ),
			( "", IECore.MessageHandler.Level.Info ),
		]
		for msgType, expectedLevel in cases :
			with IECore.CapturingMessageHandler() as mh :
				status = GafferOFX.Host.message( msgType, "TestID", "hello %s" % msgType )
				self.assertEqual( status, 0 ) # kOfxStatOK
			self.assertEqual( len( mh.messages ), 1 )
			self.assertEqual( mh.messages[0].level, expectedLevel )
			self.assertIn( "hello", mh.messages[0].message )
			self.assertIn( "TestID", mh.messages[0].context )
			self.assertIn( "GafferOFX::Host", mh.messages[0].context )

	def testMessageFormatting( self ) :

		with IECore.CapturingMessageHandler() as mh :
			# messageFormatted forwards printf-style %s
			status = GafferOFX.Host._messageFormatted( "OfxMessageMessage", "FmtID", "hello %s", "world" )
			self.assertEqual( status, 0 )
		self.assertEqual( len( mh.messages ), 1 )
		self.assertEqual( mh.messages[0].message, "hello world" )
		self.assertIn( "FmtID", mh.messages[0].context )

		# Anything but a single plain %s is rejected instead of reading
		# missing or mistyped arguments.
		for badFmt in ( "hello %s %s", "hello %d", "hello" ) :
			with self.assertRaises( Exception ) :
				GafferOFX.Host._messageFormatted( "OfxMessageMessage", "FmtID", badFmt, "world" )

	def testMessageQuestion( self ) :

		with IECore.CapturingMessageHandler() as mh :
			status = GafferOFX.Host.message( "OfxMessageQuestion", "", "are you sure?" )
			# kOfxStatReplyYes == 12
			self.assertEqual( status, 12 )
		self.assertEqual( len( mh.messages ), 1 )
		# Question still logs at Info
		self.assertEqual( mh.messages[0].level, IECore.MessageHandler.Level.Info )

	def testPersistentMessage( self ) :

		GafferOFX.Host.clearPersistentMessage()
		self.assertEqual( GafferOFX.Host.persistentMessage(), "" )

		with IECore.CapturingMessageHandler() as mh :
			status = GafferOFX.Host.setPersistentMessage( "OfxMessageError", "PersistID", "persist error" )
			self.assertEqual( status, 0 )

		self.assertEqual( len( mh.messages ), 1 )
		self.assertEqual( mh.messages[0].level, IECore.MessageHandler.Level.Error )
		self.assertEqual( GafferOFX.Host.persistentMessage(), "persist error" )

		GafferOFX.Host.clearPersistentMessage()
		self.assertEqual( GafferOFX.Host.persistentMessage(), "" )

	def testMessageHook( self ) :

		# Hook contract: returns True (reply yes), False (reply no) or
		# None (default handling). kOfxStatReplyYes == 12, ReplyNo == 13.
		calls = []
		try :
			GafferOFX.Host.setMessageHook( lambda t, i, m : calls.append( ( t, i, m ) ) or True )
			with IECore.CapturingMessageHandler() :
				status = GafferOFX.Host.message( "OfxMessageQuestion", "QID", "sure?" )
			self.assertEqual( status, 12 )
			self.assertEqual( calls, [ ( "OfxMessageQuestion", "QID", "sure?" ) ] )

			GafferOFX.Host.setMessageHook( lambda t, i, m : False )
			with IECore.CapturingMessageHandler() :
				status = GafferOFX.Host.message( "OfxMessageQuestion", "", "sure?" )
			self.assertEqual( status, 13 )

			GafferOFX.Host.setMessageHook( lambda t, i, m : None )
			with IECore.CapturingMessageHandler() :
				self.assertEqual( GafferOFX.Host.message( "OfxMessageQuestion", "", "sure?" ), 12 )
				self.assertEqual( GafferOFX.Host.message( "OfxMessageError", "", "oops" ), 0 )

			def badHook( t, i, m ) :
				raise RuntimeError( "hook failed" )
			GafferOFX.Host.setMessageHook( badHook )
			with IECore.CapturingMessageHandler() :
				self.assertEqual( GafferOFX.Host.message( "OfxMessageQuestion", "", "sure?" ), 12 )
				self.assertEqual( GafferOFX.Host.message( "OfxMessageError", "", "oops" ), 0 )
		finally :
			GafferOFX.Host.setMessageHook( None )

		# Hook cleared: defaults restored
		with IECore.CapturingMessageHandler() :
			self.assertEqual( GafferOFX.Host.message( "OfxMessageQuestion", "", "sure?" ), 12 )

	def testPluginMessageViaHook( self ) :

		import Gaffer

		# Shadertoy's Help pushbutton drive sendMessage on the main thread.
		calls = []
		s = Gaffer.ScriptNode()
		n = GafferOFX.OFXImageNode()
		s.addChild( n )
		n["pluginId"].setValue( "net.sf.openfx.Shadertoy" )
		self.assertTrue( n.createPluginInstance() )

		try :
			GafferOFX.Host.setMessageHook( lambda t, i, m : calls.append( ( t, i, m ) ) )
			with IECore.CapturingMessageHandler() :
				p = n["parameters"]["helpButton"]
				p.setValue( not p.getValue() )
		finally :
			GafferOFX.Host.setMessageHook( None )

		messages = [ c for c in calls if c[0] == "OfxMessageMessage" ]
		self.assertGreater( len( messages ), 0 )
		self.assertIn( "Shadertoy", messages[0][2] )

if __name__ == "__main__" :
	unittest.main()
