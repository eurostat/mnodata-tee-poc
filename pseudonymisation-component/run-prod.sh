#!/usr/bin/env bash
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

dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Load .env file if it exists
set -o allexport
[[ -f .env ]] && source .env
set +o allexport

address="${PSEUDONYMISATION_COMPONENT_ADDRESS:-127.0.0.1:5000}"
additional_flags=("$@")

# Make sure $PWD is in this directory, as the `wsgi.py` file is referenced.
cd "$dir"

# Workers must be 1!
exec gunicorn \
    --workers=1 \
    --bind "$address" \
    ${additional_flags[@]+"${additional_flags[@]}"} \
    wsgi:app
