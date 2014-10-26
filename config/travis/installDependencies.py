import os
import sys
import json
import urllib

# get the prebuilt dependencies package and unpack it into the build directory

platform = "osx" if sys.platform == "darwin" else "linux"

releases = json.load( urllib.urlopen( "https://api.github.com/repos/johnhaddon/gafferDependencies/releases" ) )
release = next( r for r in releases if len( r["assets"] ) )
 
asset = next( a for a in release["assets"] if platform in a["name"] )
tarFileName, headers = urllib.urlretrieve( asset["browser_download_url"] )

buildDir = os.path.expandvars( "./build/$BUILD_NAME" )
os.makedirs( buildDir )
os.system( "tar xf %s -C %s --strip-components=1" % ( tarFileName, buildDir ) )
