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

trap "kill \$server_pid 2>/dev/null || true" SIGINT SIGTERM EXIT


#######
# script-local variables
#######

# holds the path to the directory which contains this file
dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"


#######
# Load common variables and functions
#######

case "$TEST_TYPE" in
    businesscycle)
        export SERVER_PORT="60382"
        ;;
    performance)
        export SERVER_PORT="60383"
        ;;
    *)
        >&2 echo "Unknown test type: <$TEST_TYPE>"
        exit 1
        ;;
esac
export SERVER_ADDRESS="localhost:$SERVER_PORT"

export TEST_DIRECTORY="$dir"


# shellcheck source=task-enclaves/test/common.sh
source "${dir}/common.sh"


#######
# start the server
#######

port_is_open() {
    # $1: port, decimal number.

    # Convert port to hexadecimal number
    local port
    port="$(printf "%04X" "$1")"
    # Use subshell to make sure the exit code of grep is used.
    < <(awk '{print $2}' /proc/net/tcp /proc/net/tcp6) grep -q ":$port"
}

check_server_still_alive() {
    jobs -rp | grep "^$server_pid$" -q
}

# Setup the server working dir in here, so we can tell it to the client which
# extracts some diagnostics from there.
server_working_directory="$(setup_server_working_directory)"
WORKING_DIRECTORY="$server_working_directory" "${dir}/server.sh" &
server_pid=$!

# addr is unused.
# wait for the port to show up
i=0
while ! port_is_open "$SERVER_PORT" && check_server_still_alive; do
    ((i=i+1))
    if [ "$i" -gt 200 ]; then
        >&2 echo "The server failed to start after 20s."
        exit 1
    fi
    sleep 0.1
done

if ! check_server_still_alive; then
    >&2 echo "The server died unexpectedly."
    exit 1
fi


#######
# start the client
#######

export SERVER_WORKING_DIR="$server_working_directory"

"$dir/$TEST_TYPE/client.sh"

kill "$server_pid"
wait
