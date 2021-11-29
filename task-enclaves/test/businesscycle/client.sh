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

# This test processes a series of report requests. The NSI and VAD scripts
# are invoked in an irregular pattern, and some H files are missing or invalid.
#
# The purpose of this test is to make sure that:
#
# * The solution can process successive report requests.
# * The solution automatically handles finishing and canceling pending
#   report processings, i.e. edge cases are handled correctly.
# * The progress reports are correct.

########################
# Configurable Variables
########################

# Data generator variables, can be modified.
fill_subperiod_from_night_tile_probability=0.3
fill_subperiod_from_day_tile_probability=0.15
user_count=100
area_width=20
tiles_per_user=3 # simple generator
tiles_per_subperiod=5 # generator
duplicate_count=0
# simple_generator, circle_generator, generator
python_generator=generator

# This is the maximum amount, not all are used.
num_days=23


# HI provides by default three dummy client configurations within its working
# directory setup.
nd="client1.yaml"
vad="client2.yaml"
nsi="client3.yaml"

if [ "$python_generator" = generator ]; then
    total_user_count="$(( user_count * (duplicate_count + 1) ))"
else
    total_user_count=$user_count
fi

###########
# Constants
###########

# holds the path to the directory which contains this file
dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# shellcheck source=task-enclaves/test/common.sh
source "$dir/../common.sh"

export PATH
PATH="$(dirname "$SHAREMINDHI_CLIENT"):$PATH"
# sharemind-hi-nsi
PATH="$nsi_wrapper_path:$PATH"
# sharemind-hi-vad
PATH="$vad_wrapper_path:$PATH"
# run-prod.sh
PATH="$pseudonymisation_component_path:$PATH"

######################################################
# Setup the working directory which contains keys, ...
######################################################

working_directory="$(setup_client_working_directory)"
pushd "$working_directory" &>/dev/null
export TMPDIR="$working_directory"

function cleanup {
    if [ -f "${pseud_comp_pid_file:-/non/existant/file}" ]; then
        kill "$(<"$pseud_comp_pid_file")" || true
    fi

    rm -rf "$working_directory"
}

trap "cleanup" EXIT

# The wrapper scripts don't not allow "--allow-missing-trust", so we need to
# add the TrustedEnforcers section to the config files. Does not really matter
# which.
add_trusted_enforcer "$nsi" "$vad"
add_trusted_enforcer "$nd" "$vad"
add_trusted_enforcer "$vad" "$nd"

# This is implicitly used by the python prototype.
test_data_dir=data/
mkdir "$test_data_dir"

# This is used by the VAD script, so we can make sure that it only ever sees a
# single H file at once.
h_import_data_dir=h_import_data/
mkdir "$h_import_data_dir"

# The output directory which where all the important data is stored, then
# `tar`ed and then analyzed later. For the report generation.

#################
# Attest, Approve
#################

if [[ "$SGX_MODE" == "HW" ]]; then
    for client in $nd $vad $nsi; do
        say "Running attestation"
        sharemind-hi-client -c $client -a attestation
    done
fi

# Approves the task (required to add data to the task and to run it) Done here
# so the pseudonymisation component can query the pseudonymisation enclave for
# the periodic key.
for client in $nd $vad $nsi; do
    say "Running approve"
    sharemind-hi-client -c $client -a dfcApprove <<< "Y"
done

#################################################
# Prepare the reference areas and residents files
#################################################

say "Generating reference areas and census residents files."
"$dir/../data-generator.py" \
    --setup \
    --prototype-dir "$prototype_path" \
    --data-generator-dir "$data_generator_path" \
    --user-count "$user_count" \
    --days "$num_days" \
    --tiles-per-user "$tiles_per_user" \
    --tiles-per-subperiod "$tiles_per_subperiod" \
    --night-prob "$fill_subperiod_from_night_tile_probability" \
    --day-prob "$fill_subperiod_from_day_tile_probability" \
    --area-width "$area_width" \
    --duplicate-count "$duplicate_count" \
    --generator "$python_generator"

reference_areas="${test_data_dir}/reference-areas.csv"
census_residents="${test_data_dir}/residents.csv"
if ! [ -f "$reference_areas" ] || ! [ -f "$census_residents" ]; then
    die "Failed to generate reference_areas and census_residents files."
fi

###############################################
# Start the pseudonymisation component (server)
###############################################

# Make sure that the required programs are available / PATH update worked.
command -v gunicorn >/dev/null 2>&1 || die "Could not find gunicorn binary for the pseudonymisation component - did you enter the virtual environment?"
command -v run-prod.sh >/dev/null 2>&1 || die "Could not find pseudonymisation component run-prod.sh"

# Using a unix socket, so we don't risk to collide with already used ports.
pseud_comp_address="$working_directory/pseudonymisation_component.sock"
pseud_comp_pid_file="$working_directory/pscomp.pid"

# We don't capture the ID "$!" of this background process, because it
# exports a PID file instead, which we use to kill it.
# Start the server which provides the pseudonymisation service.
PSEUDONYMISATION_COMPONENT_ADDRESS="unix:$pseud_comp_address"  \
    PSEUDONYMISATION_COMPONENT_PERIODIC_KEY_PATH="pseud-comp-periodic-key" \
    PSEUDONYMISATION_COMPONENT_HI_CLIENT_CONFIGURATION="$(realpath "$nd")" \
    run-prod.sh --pid "$pseud_comp_pid_file" --log-level "warning" 2>&1 &

# Wait until the socket becomes available. The test for the socket or pidfile
# availability did not work... Just wait some seconds, should be sufficient.
say "Waiting until the pseudonymisation component becomes available"
sleep 6
say "The pseudonymisation component became available"

######################
# Generate all H files
######################

# Produce only files for the periods that will be provided during the test.
# Special case:
# *   4: Make sure this key exists in the enclave, although we will provide
#      another H file which thus cannot decrypt: 100
# * 100: To be used as a replacement file for period 4, such that this one
#        fails.
# * 101: To be used for period 6, such that in the Solution no key exists for
#        period 6, yet.
# *  17: Will be replaced with a corrupted file instead. But like 4, make sure that
#        the file exists
for period in 1 2 3 8 10 20     4 100 17 101; do
    say "[Period $period] Preparing the period pseudonymisation key"
    "$dir/nd-dummy.sh" \
        "prepare-keys" \
        "$pseud_comp_address" \
        "$(period_to_date "$period")"

    say "[Period $period] Generating the footprint updates"
    "$dir/nd-dummy.sh" \
        "pseudonymise" \
        "$pseud_comp_address" \
        "$(period_to_date "$period")" \
        "$total_user_count" \
    | "$dir/../data-generator.py" \
        --period "$period" \
        --prototype-dir "$prototype_path" \
        --data-generator-dir "$data_generator_path" \
        --user-count "$user_count" \
        --days "$num_days" \
        --tiles-per-user "$tiles_per_user" \
        --tiles-per-subperiod "$tiles_per_subperiod" \
        --night-prob "$fill_subperiod_from_night_tile_probability" \
        --day-prob "$fill_subperiod_from_day_tile_probability" \
        --area-width "$area_width" \
        --duplicate-count "$duplicate_count" \
        --generator "$python_generator"
done

# Generate the broken files
# Produces a failure due to failure to decrypt.
mv \
    "$test_data_dir/day-$(period_to_date 100)-updates.hdata" \
    "$test_data_dir/day-$(period_to_date 4)-updates.hdata"
# Produces a failure due to no periodic key being available.
mv \
    "$test_data_dir/day-$(period_to_date 101)-updates.hdata" \
    "$test_data_dir/day-$(period_to_date 6)-updates.hdata"
# Produces an error due to the file content being corrupt.
echo "invalid" > "$test_data_dir/day-$(period_to_date 17)-updates.hdata"
# Produces a failure due to no periodic key being available.
echo "invalid" > "$test_data_dir/day-$(period_to_date 21)-updates.hdata"

#############################
# Setup the wrapper functions
#############################

# This is a development variable: If `true`, the test will write the currently
# produced progress reports to `/tmp`, so their correctness can be manually
# verified, and then copied to the test directory as new reference files.
build_expected_progress_reports=false

check_equal_files() {
    # $1: nsi | vad
    # $2: Some context

    # Progress report files are added from call to call (to the NSI and VAD
    # scripts), it is easier to just check the full set of all produced
    # progress reports each time something happens.
    local i=0
    while true; do
        local basename="$1-progress-report-$i"
        local in_file="$PWD/$basename"
        if ! [ -f "$in_file" ]; then
            return 0
        fi

        if $build_expected_progress_reports; then
            cp "$in_file" "/tmp/$basename"
        else
            # Verify that the progress report is correct.
            good_file="$dir/$basename"
            if ! [ -f "$in_file" ] && ! [ -f "$good_file" ]; then
                return 0
            elif ! [ -f "$in_file" ] || ! [ -f "$good_file" ]; then
                die "$good_file: only one of the two files exist.\nFile1: $(cat "$in_file" || echo "<missing>")\nFile2: $(cat "$good_file" || echo "<missing>")"
            elif ! cmp "$in_file" "$good_file"; then
                die "$good_file: The files contain different contents.\nFile1: $(cat "$in_file" || echo "<missing>")\nFile2: $(cat "$good_file" || echo "<missing>")"
            fi
        fi
        ((++i)) || true
    done
}
check_all_progress_reports_were_produced() {
    if $build_expected_progress_reports; then return 0; fi

    for who in nsi vad; do
        local i=0
        while true; do
            local basename="$who-progress-report-$i"
            good_file="$dir/$basename"
            in_file="$PWD/$basename"
            if ! [ -f "$in_file" ] && ! [ -f "$good_file" ]; then
                # We have looked at all.
                return 0
            elif ! [ -f "$in_file" ] || ! [ -f "$good_file" ]; then
                # Something is missing.
                die "$good_file: only one of the two files exist.\nFile1: $(cat "$in_file" || echo "<missing>")\nFile2: $(cat "$good_file" || echo "<missing>")"
            fi
            ((++i)) || true
        done
    done
}

nsi_upload_request() {
    # $1: first period
    # $2: last period

    say "NSI: Uploading the report request $1 - $2"
    sharemind-hi-nsi \
        send-report-request \
        --sharemind-client-config "$nsi" \
        --date-first "$(period_to_date "$1")" \
        --date-last "$(period_to_date "$2")" \
        --use-case 2 \
        --census-residents-csv "$census_residents" \
        --reference-areas-csv "$reference_areas"
}

# Used in `check_equal_files` through nameref
nsi_query() {
    say "NSI: Downloading report results"
    PROGRESS_CALLBACK_OUT_DIR="$PWD" \
    PROGRESS_CALLBACK_NAME="nsi" \
        sharemind-hi-nsi \
        download \
        --sharemind-client-config "$nsi" \
        --output-dir "$PWD" \
        --progress-callback "$dir/callback.sh"

    check_equal_files "nsi" "NSI query"
}

H_to_import() {
    # $@: periods. If a period is prefixed with `~`, then it will be invalid.

    for period; do
        if [[ "$period" =~ ^~([0-9]+)$ ]]; then
            period="${BASH_REMATCH[1]}"
            say "Putting invalid H file $period in place"
        else
            say "Putting H file $period in place"
        fi
        cp \
            "$test_data_dir/day-$(period_to_date "$period")-updates.hdata" \
            "$h_import_data_dir/day-$(period_to_date "$period")-updates.hdata"
    done
}

vad_run() {
    say "VAD: Running"
    PROGRESS_CALLBACK_OUT_DIR="$PWD" \
    PROGRESS_CALLBACK_NAME="vad" \
        sharemind-hi-vad automatic-h-file-import \
        --sharemind-client-config "$vad" \
        --h-file-import-dir "$h_import_data_dir" \
        --old-h-files "delete" \
        --progress-callback "$dir/callback.sh" \
        --output-dir "$PWD"

    check_equal_files "vad" "VAD run"
}

vad_finish() {
    say "VAD: Forcing report finish"
    PROGRESS_CALLBACK_OUT_DIR="$PWD" \
    PROGRESS_CALLBACK_NAME="vad" \
        sharemind-hi-vad finish-report \
        --sharemind-client-config "$vad" \
        --progress-callback "$dir/callback.sh" \
        --output-dir "$PWD"

    check_equal_files "vad" "VAD finish"
}

################
# Main test part
################

vad_run # 1 - Nothing

# Will be finished manually, 5 not supplied.
nsi_upload_request 0 5
vad_run # 2 - New Request
H_to_import 1 2 ~4

vad_run # 3 - Processing update

# Will be finished automatically due last period being supplied
nsi_upload_request 6 10
# Will be canceled: Only the ~17 will be supplied.
nsi_upload_request 12 17
H_to_import 3 ~6 8 10 ~17 20 ~21 # the 3 will be skipped

vad_run # 4 - finished report, one canceled

# Will be finished, although ~21 failed.
nsi_upload_request 20 21
# Pending.
nsi_upload_request 22 23

vad_run # 5 - finished report, new request
vad_finish

nsi_query # A bundled report.

check_all_progress_reports_were_produced
