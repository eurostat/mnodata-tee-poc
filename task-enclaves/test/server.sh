#!/bin/bash
#
# Copyright 2021 European Union
#
# Licensed under the EUPL, Version 1.2 or – as soon they will be approved by 
# the European Commission - subsequent versions of the EUPL (the "Licence");
# You may not use this work except in compliance with the Licence.
# You may obtain a copy of the Licence at:
#
# https://joinup.ec.europa.eu/software/page/eupl
#
# Unless required by applicable law or agreed to in writing, software 
# distributed under the Licence is distributed on an "AS IS" basis,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the Licence for the specific language governing permissions and 
# limitations under the Licence.
#

set -euo pipefail

#######
# script-local variables
#######

# holds the path to the directory which contains this file
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"


#######
# Load common variables and functions
#######

source "${DIR}/common.sh"


#######
# Setup the working directory which contains keys, dfc description, ...
#######

# Set as env variable by the parent script.
pushd "$WORKING_DIRECTORY" &>/dev/null

function cleanup {
    # reset trap
    trap - SIGINT SIGTERM EXIT

    # stop server
    kill "$server_pid"
    # wait, such that all open file handles in the working directory will be closed.
    wait "$server_pid"

    # remove temporary working directory
    rm -rf "$WORKING_DIRECTORY"
}

trap "cleanup" SIGINT SIGTERM EXIT

#######
# Running the server
#######

ulimit -n 17000
"${SHAREMINDHI_SERVER}" -c server.yaml --ignore-sgx-capability &
server_pid=$!
wait
