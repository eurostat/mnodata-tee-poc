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

source "${BUILD_DIRECTORY}/sharemind-hi-task-enclave-test-helpers.sh"

die() {
    >&2 echo -e "[TEST $(date +"%Y-%m-%dT%H:%M:%S%Z")] Error: $*"
    exit 1
}

# Querying script interactivity does not really work through CTest, hence make
# it output colors unconditionally. These information are anyway meant for
# monitorying and not to analyze later (there is separate output for that).
term_blue="$(tput setaf 4)"
term_orange="$(tput setaf 208)"
term_yellow="$(tput setaf 226)"
term_red="$(tput setaf 196)"
term_normal="$(tput sgr0 4)" # Place this last, so terminal output with `set -x` is quickly fixed.

say() {
    echo "${term_blue}[TEST $(date +"%Y-%m-%dT%H:%M:%S%Z")] $*${term_normal}"
}

warn() {
    echo "${term_orange}[TEST $(date +"%Y-%m-%dT%H:%M:%S%Z")] $*${term_normal}"
}

common_sh_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
proj_root_dir="$(readlink -e "$common_sh_dir/../..")"
data_generator_path="$proj_root_dir/data-generator"
prototype_path="$proj_root_dir/prototype"
nsi_wrapper_path="$proj_root_dir/nsi-wrapper"
vad_wrapper_path="$proj_root_dir/vad-wrapper"
ae_dir="$proj_root_dir/task-enclaves/src/analytics_enclave"
pseudonymisation_component_path="$proj_root_dir/pseudonymisation-component"

day_quantisation_threshold="$(<"$ae_dir/Parameters.h" sed -En 's/.*day_quantisation_threshold = ([\.0-9]+).*/\1/p')"
sub_period_quantisation_threshold="$(<"$ae_dir/Parameters.h" sed -En 's/.*sub_period_quantisation_threshold = ([\.0-9]+).*/\1/p')"
sdc_threshold="$(<"$ae_dir/Parameters.h" sed -En 's/.*sdc_threshold = ([\.0-9]+).*/\1/p')"

# E.g. "3" -> "1970-01-04"
period_to_date() {
    date --utc -Idate --date='@'"$(( "$1" * 86400 ))"
}

# E.g. "1970-01-04" -> "3"
date_to_period() {
    echo "$(( $(date --utc --date "$1" '+%s') / 86400))"
}

add_trusted_enforcer() {
    # $1: Client config to modify
    # $2: Client config from where the certificate path shall be extracted.

    local cert_path
    cert_path="$(sed -En 's/^.*PublicKeyCertificateFile: "(.+)"$/\1/p' "$2")"
    # shellcheck disable=SC1004 # Explicitly stated in the wiki, this is for sed.
    sed -i'' '/SessionStateFile/a \
\  TrustedEnforcers:\
\  - "'"$cert_path"'"' "$1"
}

