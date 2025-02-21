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
		node["pluginId"].setValue("net.sf.openfx.invertPlugin")
		self.assertTrue(node.createPluginInstance())


	def testEffectInstanceProjectSize(self):
		import Gaffer
		import GafferOFX
		import GafferImage

		scriptNode = Gaffer.ScriptNode()
		node = GafferOFX.OFXImageNode()
		node["pluginId"].setValue("net.sf.openfx.invertPlugin")
		scriptNode.addChild( node )
		node.createPluginInstance()

		with scriptNode.context():
			self.assertEqual(node.effectInstanceProjectSize(), (1920.0, 1080.0))

			# override default format
			defaultFormatPlug = GafferImage.FormatPlug.acquireDefaultFormatPlug( scriptNode )
			f = GafferImage.Format( 100, 200, 2 )
			defaultFormatPlug.setValue( f )	

			self.assertEqual(node.effectInstanceProjectSize(), (100.0, 200.0))

if __name__ == "__main__" :
	unittest.main()


