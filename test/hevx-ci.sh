#!/bin/bash

# Build configuration to test
BUILD_CONFIG="RelWithDebInfo"

# URL of CTest Script
URL=https://raw.githubusercontent.com/usnistgov/hevx/master/test/CTestScript.cmake

##
## Nothing below here should have to be changed
##

if [ -z $1 ]; then
  HTML_ROOT="$HOME/public_html/hevx/ci"
else
  HTML_ROOT="$1"
fi

if [ -z $2 ]; then
  TEST_ROOT="/local/tmp/$USER/hevx-ci"
else
  if [ -a $2 ]; then
    echo "hevx-ci: $2 exists: exiting."
    exit 1
  fi
  TEST_ROOT="$2"
fi

function cleanup {
  \rm -fv $scriptfile
  \rm -fv $lockfile
  echo rm -rf $TEST_ROOT
  \rm -rf $TEST_ROOT
}

trap "cleanup" 0
lockfile=/tmp/hevx-ci.lockfile

if [ -f $lockfile ]; then
  echo -n "hevx-ci: $lockfile exists "
  pid=$(cat $lockfile)
  if [ -e /proc/$pid ]; then echo "and pid $pid is running: exiting."
  else echo "but pid $pid is dead: updating lock and continuing."
  fi
fi
(\umask 0; echo $$ >> $lockfile)

scriptfile=$(\mktemp)
\curl -s $URL | sed -e 's/Experimental/Nightly/' > $scriptfile

starttime_arg="-DCTEST_NIGHTLY_START_TIME=\"20:00:00 EST\""
dashboardroot_arg="-DCTEST_DASHBOARD_ROOT=$TEST_ROOT"
testtype_arg="-DCTEST_CONFIGURATION_TYPE=$BUILD_CONFIG"
CTEST_ARGS="-VV $starttime_arg $dashboardroot_arg $testtype_arg -S $scriptfile"

REPORT_ARGS="-C $BUILD_CONFIG -O $HTML_ROOT $TEST_ROOT"

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

echo "scl enable devtoolset-7 rh-python36 -- ctest3 $CTEST_ARGS"
\scl enable devtoolset-7 rh-python36 -- ctest3 $CTEST_ARGS

echo "scl enable rh-python36 -- python $SCRIPT_DIR/create-report.py $REPORT_ARGS"
\scl enable rh-python36 -- python $SCRIPT_DIR/create-report.py $REPORT_ARGS
