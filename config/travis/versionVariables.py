#!/usr/bin/env python
#
# Sets GAFFER_*_VERSION environment variables based on SConstruct

import re

for line in open( "SConstruct" ).readlines() :
	m = re.search( "gaffer([A-Za-z]+)Version = ([0-9]+)", line  )
	if m :
		print "export GAFFER_%s_VERSION=%s" % ( m.group( 1 ).upper(), m.group( 2 ) )
