#!/usr/bin/env python
# Copyright(C) 2013, 2014, 2015 Open Information Security Foundation

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

import urllib, urllib2, cookielib
import simplejson as json
import time
import argparse
import sys
# variables
#  - github user
#  - buildbot user and password

BASE_URI="https://buildbot.openinfosecfoundation.org/"
GITHUB_BASE_URI = "https://api.github.com/repos/"
GITHUB_MASTER_URI = "https://api.github.com/repos/inliniac/suricata/commits?sha=master"

parser = argparse.ArgumentParser(prog='prscript', description='Script checking validity of branch before PR')
parser.add_argument('-u', '--username', dest='username', help='github and buildbot user')
parser.add_argument('-p', '--password', dest='password', help='buildbot password')
parser.add_argument('-c', '--check', action='store_const', const=True, help='only check last build', default=False)
parser.add_argument('-v', '--verbose', action='store_const', const=True, help='verbose output', default=False)
parser.add_argument('--norebase', action='store_const', const=True, help='do not test if branch is in sync with master', default=False)
parser.add_argument('-r', '--repository', dest='repository', default='suricata', help='name of suricata repository on github')
parser.add_argument('-d', '--docker', action='store_const', const=True, help='use docker based testing', default=False)
parser.add_argument('-l', '--local', action='store_const', const=True, help='local testing before github push', default=False)
parser.add_argument('branch', metavar='branch', help='github branch to build')
args = parser.parse_args()
username = args.username
password = args.password
cookie = None

if args.docker:
    BASE_URI="http://localhost:8010/"
    BUILDERS_LIST = ["build", "clang", "debug", "features", "profiling", "pcaps"]
else:
    BUILDERS_LIST = [username, username + "-pcap"]


BUILDERS_URI=BASE_URI+"builders/"
JSON_BUILDERS_URI=BASE_URI+"json/builders/"

def TestRepoSync(branch):
    request = urllib2.Request(GITHUB_MASTER_URI)
    page = urllib2.urlopen(request)
    json_result = json.loads(page.read())
    sha_orig = json_result[0]["sha"]
    request = urllib2.Request(GITHUB_BASE_URI + username + "/" + args.repository + "/commits?sha=" + branch + "&per_page=100")
    page = urllib2.urlopen(request)
    json_result = json.loads(page.read())
    found = -1
    for commit in json_result:
        if commit["sha"] == sha_orig:
            found = 1
            break
    return found

def OpenBuildbotSession():
    auth_params = { 'username':username,'passwd':password, 'name':'login'}
    cookie = cookielib.LWPCookieJar()
    params = urllib.urlencode(auth_params)
    opener = urllib2.build_opener(urllib2.HTTPCookieProcessor(cookie))
    urllib2.install_opener(opener)
    request = urllib2.Request(BASE_URI + 'login', params)
    page = urllib2.urlopen(request)
    return cookie


def SubmitBuild(branch, extension = "", builder_name = None):
    raw_params = {'branch':branch,'reason':'Testing ' + branch, 'name':'force_build', 'forcescheduler':'force'}
    params = urllib.urlencode(raw_params)
    if not args.docker:
        opener = urllib2.build_opener(urllib2.HTTPCookieProcessor(cookie))
        urllib2.install_opener(opener)
    if builder_name == None:
        builder_name = username + extension
    request = urllib2.Request(BUILDERS_URI + builder_name + '/force', params)
    page = urllib2.urlopen(request)

    result = page.read()
    if args.verbose:
        print "=== response ==="
        print result
        print "=== end of response ==="
    if args.docker:
        if "<h2>Pending Build Requests:</h2>" in result:
            print "Build '" + builder_name + "' submitted"
            return 0
        else:
            return -1
    if "Current Builds" in result:
        print "Build '" + builder_name + "' submitted"
        return 0
    else:
        return -1

# TODO honor the branch argument
def FindBuild(branch, extension = "", builder_name = None):
    if builder_name == None:
        request = urllib2.Request(JSON_BUILDERS_URI + username + extension + '/')
    else:
        request = urllib2.Request(JSON_BUILDERS_URI + builder_name + '/')
    page = urllib2.urlopen(request)
    json_result = json.loads(page.read())
    # Pending build is unnumbered
    if json_result["pendingBuilds"]:
        return -1
    if json_result["currentBuilds"]:
        return json_result["currentBuilds"][0]
    if json_result["cachedBuilds"]:
        return json_result["cachedBuilds"][-1]
    return -2

def GetBuildStatus(builder, buildid, extension="", builder_name = None):
    if builder_name == None:
        # https://buildbot.suricata-ids.org/json/builders/build%20deb6/builds/11
        request = urllib2.Request(JSON_BUILDERS_URI + username + extension + '/builds/' + str(buildid))
    else:
        request = urllib2.Request(JSON_BUILDERS_URI + builder_name + '/builds/' + str(buildid))
    page = urllib2.urlopen(request)
    result = page.read()
    if args.verbose:
        print "=== response ==="
        print result
        print "=== end of response ==="
    json_result = json.loads(result)
    if json_result["currentStep"]:
        return 1
    if 'successful' in json_result["text"]:
        return 0
    return -1

def WaitForBuildResult(builder, buildid, extension="", builder_name = None):
    # fetch result every 10 secs till task is over
    if builder_name == None:
        builder_name = username + extension
    res = 1
    while res == 1:
        res = GetBuildStatus(username,buildid, builder_name = builder_name)
        if res == 1:
            time.sleep(10)

    # return the result
    if res == 0:
        print "Build successful for " + builder_name
    else:
        print "Build failure for " + builder_name + ": " + BUILDERS_URI + builder_name + '/builds/' + str(buildid)
    return res

    # check that github branch and inliniac master branch are sync
if not args.local and TestRepoSync(args.branch) == -1:
    if args.norebase:
        print "Branch " + args.branch + " is not in sync with inliniac's master branch. Continuing due to --norebase option."
    else:
        print "Branch " + args.branch + " is not in sync with inliniac's master branch. Rebase needed."
        sys.exit(-1)

# submit buildbot form to build current branch on the devel builder
if not args.check:
    if not args.docker:
        cookie = OpenBuildbotSession()
        if cookie == None:
            print "Unable to connect to buildbot with provided credentials"
            sys.exit(-1)
    for build in BUILDERS_LIST:
        res = SubmitBuild(args.branch, builder_name = build)
        if res == -1:
            print "Unable to start build. Check command line parameters"
            sys.exit(-1)

buildids = {}

if args.docker:
    from time import sleep
    print "Waiting 2 seconds"
    sleep(2)

# get build number and exit if we don't have
for build in BUILDERS_LIST:
    buildid = FindBuild(args.branch, builder_name = build)
    if buildid == -1:
        print "Pending build tracking is not supported. Follow build by browsing " + BUILDERS_URI + build
    elif buildid == -2:
        print "No build found for " + BUILDERS_URI + build
        sys.exit(0)
    else:
        print "You can watch build progress at " + BUILDERS_URI + build + "/builds/" + str(buildid)
        buildids[build] = buildid

if len(buildids):
    print "Waiting for build completion"
else:
    sys.exit(0)

res = 0
for build in buildids:
    res = WaitForBuildResult(build, buildids[build], builder_name = build)

if res == 0:
    if not args.norebase and not args.docker:
        print "You can copy/paste following lines into github PR"
        for build in buildids:
            print "- PR " + build + ": " + BUILDERS_URI + build + "/builds/" + str(buildids[build])
    sys.exit(0)
else:
    sys.exit(-1)
