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

# This script is provided as a callback to the NSI and MNO-VAD applications to
# handle the progress report. It stores the progress reports at a location
# which is controlled by the `client.sh` script through environment variables.
# The progress reports are stored with increasing number suffixes. Therefore,
# it iterates from 0 to find a number which has not been used. The created
# files are then verified against the pre-verified copies in this test directory.
# This test is not done in here to keep the testing logic in one file.

i=0
while true; do
    name="$PROGRESS_CALLBACK_OUT_DIR/$PROGRESS_CALLBACK_NAME-progress-report-$i"
    if ! [ -f "$name" ]; then
        # Print the contents such that they are visible during the test execution.
        >&2 echo "Progress callback $PROGRESS_CALLBACK_NAME: Found a new progress report $i, with the following content:"
        >&2 <"$1" sed 's/^/> /'

        # Copy the progress report to its destination.
        cp "$1" "$name"
        exit 0
    fi
    ((++i)) || true
done
