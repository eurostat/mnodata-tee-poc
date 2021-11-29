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

# This test fully processes a single report request. The NSI and VAD scripts
# are used for the interaction with the enclaves, but for performance reasons
# the pseudonymisation is done with `generate_pseudonyms.c`. The amount of
# generated data is configurable through the variables in the beginning of
# the test.
#
# The purpose of this test is to make sure that:
#
# * Best-case functionality works: Processing and finishing a single report
#   with all H files.
# * The resulting report data matches the results from the python prototype.
#   This is only done if the amount of data is small.
#
# In addition, various performance metrics are gathered and stored in a
# persistent output directory for later inspection. This includes `atop`
# output, the in-enclave execution time per invocation, data sizes (unless
# Release build), data generation times and all application logs. The various
# files are bundled in a .tar.gz archive.
#
# Multiple H files may be created in parallel, but their generation process
# does not interleave with the enclave execution to not influence its
# performance.

########################
# Configurable Variables
########################

# Data generator variables, you can modify them safely.
num_days=10
fill_subperiod_from_night_tile_probability=0.3
fill_subperiod_from_day_tile_probability=0.15
# Creating more than 1M unique users will consumer a lot of RAM.
user_count=100
area_width=20 # simple generator
tiles_per_user=3 # simple generator
tiles_per_subperiod=5 # generator
duplicate_count=10 # generator
# simple_generator, circle_generator, generator
python_generator=generator

with_calibration=true

# This many H files are created at once. You may modify it to match the amount
# of your cores. If the data generation memory consumption exceeds the
# capabilities of your system you might need to reduce it.
data_generator_parallelism=4

# For later eyeballing of the results and gathered metrics.
persistent_data_output_directory=/tmp/sharemind-hi-eurostat-test-data

###########
# Constants
###########

# holds the path to the directory which contains this file
dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# shellcheck source=task-enclaves/test/common.sh
source "${dir}/../common.sh"

min_period=0
max_period="$((min_period + num_days - 1))"

# This is used at a couple of places.
tile_idx=(unsigned:2 unsigned:2)

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

if [ $(( num_days * total_user_count )) -gt 100000 ]; then
    # If there is too much data, then the analysis.py script will take a lot
    # of time. This much data is only relevant for the benchmark, not for
    # validating the correctness of the application logic.
    compare_against_python=false
    warn "Skipping the output verification (not running analysis.py reference code)."
else
    compare_against_python=true
fi

#####################################
# Load common variables and functions
#####################################

export PATH
PATH="$(dirname "$SHAREMINDHI_CLIENT"):$PATH"
# sharemind-hi-nsi
PATH="$nsi_wrapper_path:$PATH"
# sharemind-hi-vad
PATH="$vad_wrapper_path:$PATH"

current_time() {
    date "+%s"
}

timediff() {
    echo "$(($2 - $1))"
}

displaytime() {
    # $1: in seconds
    local T=$1
    local D=$((T/60/60/24))
    local H=$((T/60/60%24))
    local M=$((T/60%60))
    local S=$((T%60))
    if [ $D -gt 0 ]; then printf '%dd ' $D; fi
    if [ $H -gt 0 ]; then printf '%dh ' $H; fi
    if [ $M -gt 0 ]; then printf '%dm ' $M; fi
    printf '%ds (%s seconds)\n' $S "$T"
}

tep_test_time_start="$(current_time)"

######################################################
# Setup the working directory which contains keys, ...
######################################################

working_directory="$(setup_client_working_directory)"
pushd "$working_directory" &>/dev/null
export TMPDIR="$working_directory"

function cleanup {
    result="$?"

    for worker_idx in $(seq 1 "$data_generator_parallelism"); do
        if [ -f "${pseud_comp_pid_file:-/non/existant/file}$worker_idx" ]; then
            kill "$(<"$pseud_comp_pid_file$worker_idx")" || true
        fi
    done

    if [ -n "${atop_pid:-}" ]; then
        kill "${atop_pid}" || true
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

# Create the directory where to store the downloaded csvs for later eyeballing.
mkdir -p "$persistent_data_output_directory"

# This is implicitly used by the python prototype.
test_data_dir=data/
mkdir "$test_data_dir"

# This is used by the VAD script, so we can make sure that it only ever sees a
# single H file at once.
h_import_data_dir=h_import_data/
mkdir "$h_import_data_dir"

# The output directory which where all the important data is stored, then
# `tar`ed and then analyzed later. For the report generation.
gathered_output_timestamp="$(date +"%Y_%m_%d-%H_%M_%S")"
gathered_output_dir="output-$gathered_output_timestamp"
mkdir "$gathered_output_dir"

if command -v atop >/dev/null 2>&1; then
    atop -w "$gathered_output_dir/atop.bin" &
    atop_pid="$!"
fi

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

##########################################
# Start a background data generator worker
##########################################

# Use a separate C executable for pseudonym generation.
generate_pseudonyms_bin=generate-pseudonyms
# It's nearly like a script, compiled just-in-time. Anyway, the other components
# require OpenSSL, so it is rather safe to assume that it is available.
gcc -O2 "$dir/generate_pseudonyms.c" -lcrypto -o "$generate_pseudonyms_bin"

generate_h_file() {
    # $1 period
    # $2 worker index [0, data_generator_parallelism)

    local period="$1"

    say "[Period $period] Preparing the period pseudonymisation key"
    # This part communicates with the core enclave, thus needs to be
    # synchronized.
    local output
    output="$(flock data-gen-prepare-key-lock \
        sharemind-hi-client \
        -c "$nd" \
        -a taskRun -- \
                --task pseudonymisation_key_enclave --wait -- \
                --period "$period")"
    [[ "$output" =~ Topic:\ periodic_pseudonymisation_key$'\n'\ .*Id:\ ([0-9]+) ]] || die "ups\n$output"
    flock data-gen-prepare-key-lock \
        sharemind-hi-client \
        -c "$nd" -a dataDownload -- \
            --topic periodic_pseudonymisation_key --dataid "${BASH_REMATCH[1]}" \
            --datafile "periodic_key$2"

    say "[Period $period] Generating the footprint updates (background worker)"
    time_gen_start="$(current_time)"
    "./$generate_pseudonyms_bin" \
        "$(<"periodic_key$2" tail -c 16 | od -A x -t x1 -v | head -n1 | sed 's/ //g' | tail -c 33)" \
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
    time_gen_end="$(current_time)"
    time_gen="$(timediff "$time_gen_start" "$time_gen_end")"
    say "[Period $period] Finished the footprint updates (background worker) (${time_gen}s)"
}

generate_h_files() {
    # $@ periods

    # Sanity check
    if [ "$#" -gt "$data_generator_parallelism" ]; then
        die "Too many periods supplied: $* (allowed $data_generator_parallelism)"
    fi

    local -a pids
    local worker_idx=1 # starting with 1, sorry

    # Kick of the workers in the background ..
    for period in "$@"; do
        generate_h_file "$period" "$worker_idx" &
        pids+=("$!")
        ((++worker_idx))
    done

    # .. and then await them.
    for pid in "${pids[@]}"; do
        wait "$pid"
    done
}

#################################
# Upload & digest the NSI request
#################################

say "Uploading the NSI report request"
if ! $with_calibration; then
    sharemind-hi-nsi \
        send-report-request \
        --sharemind-client-config "$nsi" \
        --date-first "$(period_to_date "$min_period")" \
        --date-last "$(period_to_date "$max_period")" \
        --reference-areas-csv "$reference_areas" \
        --use-case 1
else
    sharemind-hi-nsi \
        send-report-request \
        --sharemind-client-config "$nsi" \
        --date-first "$(period_to_date "$min_period")" \
        --date-last "$(period_to_date "$max_period")" \
        --reference-areas-csv "$reference_areas" \
        --use-case 2 \
        --census-residents-csv "$census_residents"
fi

say "Digesting the NSI report request"
    sharemind-hi-vad automatic-h-file-import \
        --sharemind-client-config "$vad" \
        --h-file-import-dir "$h_import_data_dir" \
        --old-h-files "delete" \
        --output-dir "$PWD" \
        --progress-callback cat

################
# Main test part
################

declare -a taskRun_execution_times dataGen_execution_times

file_size() {
    # Human readable, not just bytes.

    if [ -f "$1" ]; then
        du -h "$1" | awk -F " " '{print $1}'
    else
        echo 0
    fi
}

total_dataGen_time=0
total_enclaveExecution_time=0
for i in $(seq $min_period $data_generator_parallelism $max_period); do
    mapfile -t periods < <(seq "$i" "$max_period" | head -n "$data_generator_parallelism")

    time_gen_start="$(current_time)"
    generate_h_files "${periods[@]}"
    time_gen_end="$(current_time)"
    time_gen="$(timediff "$time_gen_start" "$time_gen_end")"
    (( total_dataGen_time += time_gen )) || true
    dataGen_execution_times+=("$(displaytime "$time_gen")")

    for period in "${periods[@]}"; do
        from_data_file="$test_data_dir/day-$(period_to_date "$period")-updates.hdata"
        data_file="$h_import_data_dir/day-$(period_to_date "$period")-updates.hdata"

        # Move the CSV file into a controlled staging directory.
        if $compare_against_python; then
            # We keep a copy for the python analysis.
            cp "$from_data_file" "$data_file"
        else
            # Not comparing, so make space for the next file.
            mv "$from_data_file" "$data_file"
        fi
    done

    say "[Period ${periods[*]}] Running the analytics enclave"

    sharemind-hi-vad automatic-h-file-import \
        --sharemind-client-config "$vad" \
        --h-file-import-dir "$h_import_data_dir" \
        --old-h-files "delete" \
        --output-dir "$PWD" \
        --progress-callback cat

    for period in "${periods[@]}"; do
        # Get the application log.
        say "Downloading application log"
        sharemind-hi-client -c "$nsi" -a dataDownload \
            -- \
            --topic application_log \
            --dataid "$((period - min_period + 1))" \
            --datafile "$persistent_data_output_directory/application_log-$period" \
            --allow-missing-trust
        cp "$persistent_data_output_directory/application_log-$period" "$gathered_output_dir"

        records_count_file="$SERVER_WORKING_DIR/records_count$((period - periods[0]))"
        records_count="$(<"$records_count_file")"
        rm -f "$records_count_file"
        [[ "$records_count" =~ H:\ ([0-9]+),\ S:\ ([0-9]+)\ "->"\ ([0-9]+),\ Y:\ ([0-9]+), ]] || die "ups"

        # The constants are taken from the Entities.h file.
        size_information="$(printf "H: %s, S: %s -> %s, Y: %s" \
                "$(numfmt --to=iec "$((36 * BASH_REMATCH[1]))")" \
                "$(numfmt --to=iec "$((32 * BASH_REMATCH[2]))")" \
                "$(numfmt --to=iec "$((32 * BASH_REMATCH[3]))")" \
                "$(numfmt --to=iec "$((48 * BASH_REMATCH[4]))")")"
        in_enclave_runtime="$(tail -n1 "$persistent_data_output_directory/application_log-$period" | awk '{print $6}')"
        # `${in_enclave_runtime%?}`: remove the `s` suffix.
        in_enclave_runtime="${in_enclave_runtime%?}"
        taskRun_execution_times+=("$period: enclave time: $(displaytime "$in_enclave_runtime") ($size_information) $records_count")
        (( total_enclaveExecution_time += in_enclave_runtime )) || true
    done

    say "Enclave taskRun time updates:"
    # Print the immediate report: First the old data ..
    num_elements=$((${#taskRun_execution_times[@]} - ${#periods[@]}))
    if [ "$num_elements" -gt 0 ]; then
        # Copy the formatting of the `say` function to match the indentation.
        printf "\ttaskRun    (old): %s\n" "${taskRun_execution_times[@]:0:$num_elements}"
    fi
    printf "\ttaskRun (newest): %s\n" "${taskRun_execution_times[@]:$num_elements}"
done

# The import work has been done, stop `atop`.
if [ -n "${atop_pid:-}" ]; then
    kill "${atop_pid}" || true
    unset atop_pid
fi

#######################
# Output data retrieval
#######################

# Get output data. This converts the output to .csv files, so we can be sure
# that the output is (kind of) valid. Only the comparison against the
# analysis.py reference code can verify that, but this is only run
# conditionally (rather time consuming for larger amounts of data).
# (Note: overwrites older data)
sharemind-hi-nsi \
    download \
    --sharemind-client-config "$nsi" \
    --output-dir "$gathered_output_dir/nsi-output" \
    --progress-callback cat

# Trailing "/." important, see https://stackoverflow.com/a/4645159/2140959
cp -R "$gathered_output_dir/nsi-output/." "$persistent_data_output_directory/"

##################################
# Prepare some performance metrics
##################################

say "dataGen execution times (per full batch):"
{
    printf "%s\n" "${dataGen_execution_times[@]}";
    echo "Total: $(displaytime "$total_dataGen_time")";
} | tee "$gathered_output_dir/dataGen_execution_times" | sed 's/^/\t/'

say "taskRun execution times:"
{
    printf "%s\n" "${taskRun_execution_times[@]}";
    echo "Total: $(displaytime "$total_enclaveExecution_time")";
    echo "Total - last: $(displaytime "$((total_enclaveExecution_time - in_enclave_runtime))")";
} | tee "$gathered_output_dir/taskRun_execution_times" | sed 's/^/\t/'

num_periods=0
h_count=0
for line in "${taskRun_execution_times[@]}"; do
    [[ "$line" =~ H:\ ([0-9]+),\ S:\ ([0-9]+)\ -\>\ ([0-9]+),\ Y:\ ([0-9]+), ]] || true
    h="${BASH_REMATCH[1]}"
    #s_old="${BASH_REMATCH[2]}"
    s_new="${BASH_REMATCH[3]}"
    y="${BASH_REMATCH[4]}"
    ((++num_periods))
    h_count=$((h_count + h))
done

{
    echo "Output for the spreadsheet:";
    echo "Daily H users: $total_user_count";
    echo "Daily H recs: $((h_count / num_periods))";
    echo "Days: $num_days";
    echo "S recs: $s_new";
    echo "Y recs: $y";
} | tee "$gathered_output_dir/spreadsheet_output" | tee "$persistent_data_output_directory/spreadsheet_output"

tep_test_time_end="$(current_time)"
tep_test_time="$(timediff "$tep_test_time_start" "$tep_test_time_end")"

echo "$(basename "$gathered_output_dir").tar.gz:" # For easier copying into the outputs.README.md
{
    echo "day: $fill_subperiod_from_day_tile_probability"
    echo "night: $fill_subperiod_from_night_tile_probability"
    echo "day_quantisation_threshold: $day_quantisation_threshold"
    echo "sub_period_quantisation_threshold: $sub_period_quantisation_threshold"
    echo "sdc_threshold: $sdc_threshold"
    echo "unique_users: $(numfmt --to=si "$user_count") ($user_count)"
    echo "duplicate_count: $duplicate_count"
    echo "total_users: $(numfmt --to=si "$total_user_count") ($total_user_count)"
    echo "tiles_per_subperiod: $tiles_per_subperiod"
    echo "days: $num_days"
    echo "full test runtime: $(displaytime "$tep_test_time")"
} | tee "$gathered_output_dir/parameters" | tee "$persistent_data_output_directory/parameters"

tar czf "$persistent_data_output_directory/$(basename "$gathered_output_dir").tar.gz" "$gathered_output_dir"

############################
# Python analysis comparison
############################

if ! $compare_against_python; then
    # Nice, we can exit here.
    warn "Skipping output verification (not running analysis.py reference code)."
    exit 0
fi

# `awk` is used in a second, read the next comment block.
if command -v mawk >/dev/null 2>&1; then
    # mawk is a lot faster than gawk
    AWK_BIN="mawk"
else
    AWK_BIN="awk"
fi

say "Preparing the H files for the python analysis code"
for period in $(seq "$min_period" "$max_period"); do
    from_data_file="$test_data_dir/day-$(period_to_date "$period")-updates.hdata"
    to_csv_file="$test_data_dir/day-$period-updates.csv"
    sharemind-hi-data-converter \
        -m bin2csv \
        -c "tmp_file" \
        -b "$from_data_file" \
        --sep ',' \
        -t base64:16 "${tile_idx[@]}" float:4 float:4 float:4 float:4
    echo "header1,header2,header3,header4,header5,header6,header7" > "$to_csv_file"

    # We need to remove the pseudonyms as the analysis.py script does not have
    # access to the periodic keys. This could be implemented here but would be
    # code duplication, instead just manually put sequential IDs back into the
    # files. This is possible because we know that the base IDs for every day
    # are starting from 1 (or whatever constant), incrementing without gaps,
    # and the pseudonyms for the same ID on the same day are equal, so we can
    # swap out the pseudonym blocks block-by-block against incrementing
    # numbers.
    # shellcheck disable=SC2016
    <tmp_file $AWK_BIN -v IDs=<(seq 1 $total_user_count) \
    '
        # csv field separators
        BEGIN { FS=","; OFS="," }

        last != $1 {
            # Read next ID from file - we entered the next group.
            getline ID <IDs
            last=$1
        }

        {
            # Change the value of the first field against the read ID.
            $1=ID
            print
        }
    ' >> "$to_csv_file"
    rm -f tmp_file
done

# Make sure we will look at only-new reports
rm -rf "reports"
mkdir "reports"

say "Running the python analysis code for comparison"
"$prototype_path/analysis.py" \
    --day-quantisation-threshold "$day_quantisation_threshold" \
    --sub-period-quantisation-threshold "$sub_period_quantisation_threshold" \
    --sdc-threshold "$sdc_threshold"

# We check all relevant output files, and make the exit code depending on
# whether any of the checks failed.
result=0

validate_output() {
    # $1: topic name
    # $2: reference filename
    # $@: columns
    #
    # Compares the HI output against the python output.

    local topic_name="$1"
    # Reference files as created by the python prototype.
    local reference_filepath
    if $with_calibration; then
        reference_filepath="reports/$2"
    else
        reference_filepath="reports/${2#calibrated-}"
    fi
    shift; shift;
    local columns=("$@")

    say "Validating output <$topic_name>"

    hi_csv_file="$persistent_data_output_directory/$(period_to_date "$min_period")-$(period_to_date "$max_period")/$topic_name.csv"
    local headers
    headers="$(head -n1 "$hi_csv_file")"

    local hi_csv_lines reference_csv_lines
    hi_csv_lines="$(<"$hi_csv_file" wc -l)"
    reference_csv_lines="$(<"$reference_filepath" wc -l)"
    
    if [ "$hi_csv_lines" -lt 2 ] && [ "$reference_csv_lines" -lt 2 ]; then
        warn "The file for <$topic_name> is empty, hence the correctness of this part of the algorithm cannot be verified. Maybe you need to decrease the \`day_quantisation_threshold\` in <Parameters.h>."
    fi
    # Prepare a sorted version of the downloaded data (this was downloaded by
    # the nsi wrapper script).
    {
        echo "$headers";
        tail -n +2 "$hi_csv_file" | sort;
    } > "$persistent_data_output_directory/sorted-$topic_name.csv"

    # Streamline the floating point number representation which is done
    # differently by python and C++: import the python CSV file to achieve
    # equal results (python adds more precision).
    # So, first make the reference file a binary file, ..
    sharemind-hi-data-converter \
        -m csv2bin \
        -c "$reference_filepath" \
        -b tmp_file.data \
        --skip 1 \
        --sep ',' \
        -t "${columns[@]}"

    # .. and then transform it back to a csv file.
    {
        # CSV file header (column names)
        echo "$headers"

        sharemind-hi-data-converter \
            -m bin2csv \
            -c tmp_file \
            -b tmp_file.data \
            --sep ',' \
            -t "${columns[@]}"
        # Nicely sort and make sure there are no MS-DOS line endings.
        <tmp_file tr -d '\r' | sort
        rm -f tmp_file tmp_file.data >/dev/null 2>&1
    } > "$persistent_data_output_directory/reference-$topic_name.csv"

    local different_lines num_different_lines
    different_lines="$(comm -3 \
        "$persistent_data_output_directory/sorted-$topic_name.csv" \
        "$persistent_data_output_directory/reference-$topic_name.csv")"
    num_different_lines="$(printf "%s" "$different_lines" | wc -l)"

    # We allow a small amount of different results which is expected due to the
    # nature of floating point numbers, and since the enclave uses float (to
    # save on space), while python uses double.
    #
    # Now, given the following variables:
    # k: absolute number of lines wrong
    # N: Total number of rows (minimum of the two compared files)
    # p = (k / N): percent of lines wrong
    # B = 2 * k: actually counted number of lines wrong ($num_different_lines)
    #            comm displays both, and due to missing / excess lines we
    #            cannot just take one or the other.
    #
    # We accept differences if the following equation holds:
    # p < 0.0003 + 2 / N
    # if N =   10, then 20.03% are allowed to be wrong.
    # if N = 1000, then  0.23% are allowed to be wrong.
    #
    # We rewrite it:
    #            (k / N) <  0.0003 + 2 / N
    # <==>             k <  0.0003 * N + 2
    # <==>       (B / 2) <  0.0003 * N + 2
    # <==>             B <  0.0006 * N + 4

    local N=$((hi_csv_lines < reference_csv_lines ? hi_csv_file : reference_csv_lines))
    local B="$num_different_lines"
    if ! perl -e "if ($B < (0.0006 * $N + 4)) { exit 0 } else { exit 1 }"; then
        result=1
        >&2 printf \
            "${term_red}Wrong topic <%s> output, have a look:${term_normal} meld \"%s\" \"%s\"\n" \
            "$topic_name" \
            "$persistent_data_output_directory/sorted-$topic_name.csv" \
            "$persistent_data_output_directory/reference-$topic_name.csv"
    elif [ "$num_different_lines" -gt 0 ]; then
        {
        printf \
            "${term_yellow}Topic <%s> output is slightly different but within the margin.${term_normal} This is expected and due to floating point issues. The differences are as follows (N == $N):\n" \
            "$topic_name";
        echo "";
        echo "Enclave output";
        echo $'\t'"Python output";
        echo "$different_lines";
        echo "";
        printf "You may have a look: meld \"%s\" \"%s\"\n" \
            "$persistent_data_output_directory/sorted-$topic_name.csv" \
            "$persistent_data_output_directory/reference-$topic_name.csv";
        } >&2
    else
        echo "All good (N == $N)"
    fi
}

validate_output \
    fingerprint_report \
    "calibrated-total-footprint.csv" \
    "${tile_idx[@]}" float:8 float:8 float:8 float:8

validate_output \
    top_anchor_distribution_report \
    "top-anchor-distribution.csv" \
    "${tile_idx[@]}" unsigned:4

validate_output \
    functional_urban_fingerprint_report \
    "calibrated-functional-urban-fingerprint.csv" \
    unsigned:1 "${tile_idx[@]}" float:8

exit $result
