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

# This script generates IDs (each ID in a separate line, no quotes, like `seq 1
# 3`) which can be fed into the data generators. It communicates with the
# pseudonymisation component to pseudonymise the IDs, then prints them.
# Pseudonymisation happens in batches as the required volumes might be too
# large.

set -euo pipefail

die() {
    >&2 echo "Error: $*"
    exit 1
}

action="${1:?Missing first argument action \"prepare-keys\" or \"pseudonymise\".}"
url="${2:?Missing second argument url (unix socket) of pseudonymisation component.}"
case "$action" in
    prepare-keys)
        date_begin="${3:?Missing third argument date to prepare key for.}"
        [[ "$date_begin" =~ ^[0-9]* ]] || die "period begin is not a number."
        ;;
    pseudonymise)
        in_date="${3:?Missing third argument date to use for pseudonymisation.}"
        num_ids="${4:?Missing fourth argument number of ids.}"
        [[ "$num_ids" =~ ^[1-9][0-9]* ]] || die "num_ids is not a number > 1."
        ;;
    *)
        >&2 echo "First argument action needs to be \"prepare-keys\" or \"pseudonymise\", but is <$action>."
        exit 1
esac

batch_size=1000

if [ "$action" = "prepare-keys" ]; then
    # Request the pseud.comp. to retrieve the pseudonymisation key for that period.
	curl \
		-sS \
		-X POST\
		--unix-socket "$url" \
		"http://localhost/v1/key/$date_begin" \
		>/dev/null \
		|| die "Announcing the period failed."
    exit 0
fi

# There are only two actions, so do the actual pseudonymisation now.

min() {
    # $1: first number
    # $2: second number

    echo "$(( $1 < $2 ? $1 : $2 ))"
}

for (( i=0; i < num_ids; i=i+batch_size )); do
    limit="$(min "$(( i + batch_size - 1 ))" "$(( num_ids - 1))")"

    # We use a file to pass the POST data to curl, and the file is created
    # inline by bash using Process Substitution (`<( ... )`, evaluates to some
    # absolute filesystem path which can be read by curl). In there, a possibly
    # long json expression is created, with incremental identifiers created by
    # `seq`.
    # `jq` is then used to transform the json response into output that is one
    # identifier per line (`-r` removes the quotes (`"..."`) around the
    # identifiers).
    curl -sS -d '@'<(
        printf '{"period": "%s", "identifiers":["' "$in_date"
        seq -s '", "' "$i" "$limit"
        printf '"]}'
        ) -H "Content-Type: application/json" -X POST --unix-socket "$url" "http://localhost/v1/pseudonymise" | jq -r '.pseudonyms[]'
done
