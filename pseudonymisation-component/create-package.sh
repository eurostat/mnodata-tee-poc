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

out_dir="${1:?Missing arg output directory}"
out_dir="$(realpath "$out_dir")"
hi_version="${2:?Missing 2nd arg HI version}"
eurostat_version="${3:?Missing 3rd arg eurostat version}"

package_name=sharemind-hi-nd
src_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
py_dir=usr/share/sharemind-hi-nd
entry_script=usr/bin/sharemind-hi-nd
dir="$(mktemp -d --tmpdir eurostat-package-generation.XXXXXXXXXX)"

cleanup() {
    rm -rf "$dir"
}

trap "cleanup" EXIT

pushd "$dir" > /dev/null

# Create the directory structure in the package
mkdir DEBIAN
mkdir -p usr/bin
mkdir -p $py_dir

for py_file in server.py wsgi.py pseudonymisation.py; do
    cp "$src_dir/$py_file" $py_dir
done

set +e
cat <<EOF >$entry_script
#!/usr/bin/env bash

set -euo pipefail

# Load .env file if it exists
set -o allexport
[[ -f .env ]] && source .env
set +o allexport

address="\${PSEUDONYMISATION_COMPONENT_ADDRESS:-127.0.0.1:5000}"
cd /$py_dir
exec gunicorn --workers=1 --bind "\$address" wsgi:app

EOF
set -e

chmod u+x $entry_script

files="$entry_script $(find $py_dir -name '*.py')"
file_size_B=$(stat --printf="%s\n" $files | jq -s add)
file_size_KiB=$(( (file_size_B + 1023) / 1024 )) # rounded up
md5sum $files > DEBIAN/md5sums

set +e
cat <<EOF >DEBIAN/prerm
#!/bin/sh
rm -fr /$py_dir/__pycache__
EOF

chmod 0755 DEBIAN/prerm

cat <<EOF >DEBIAN/control
Architecture: amd64
Depends: sharemind-hi-client (= $hi_version), jq, python3-flask, gunicorn, python3-cryptography, python3-pydantic, python3-yaml
Description: Eurostat pseudonymisation component
Maintainer: Sharemind packaging <sharemind-eurostat-request@lists.cyber.ee>
Package: $package_name
Priority: optional
Section: devel
Version: $eurostat_version
Installed-Size: $file_size_KiB

EOF
set -e

dpkg-deb -b "$dir" "$out_dir/${package_name}_${eurostat_version}_amd64.deb"
