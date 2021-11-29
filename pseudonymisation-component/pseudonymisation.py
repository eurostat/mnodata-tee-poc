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

import hmac
import hashlib
import os
import re
import subprocess

from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

HASH_BYTES = 12
HMAC_BYTES = 4


def hmac_sha256(key, data):
    return hmac.new(key, data, hashlib.sha256).digest()

def sha256(x):
    m = hashlib.sha256()
    m.update(x)
    return m.digest()

def pseudonymise(id, periodic_key):
    h = sha256(id)[:HASH_BYTES]
    mac = hmac_sha256(periodic_key, h)[:HMAC_BYTES]
    plaintext = h + mac

    nonce = b'\x00' * 16
    cipher = Cipher(algorithms.AES(periodic_key),
                    modes.CTR(nonce),
                    backend=default_backend())
    encryptor = cipher.encryptor()

    return encryptor.update(plaintext) + encryptor.finalize()

def get_periodic_key(config, period):
    periodic_key_path = config['PERIODIC_KEY_PATH']
    client_conf_dir = config['HI_CLIENT_CONFIGURATION_DIR']
    client_conf = config['HI_CLIENT_CONFIGURATION']
    old_working_dir = os.getcwd()

    try:
        os.chdir(client_conf_dir)

        # First, query the DFC (`displayDfc`) and look for an earlier
        # "pseudonymisation_key_enclave" Instance which "Finished" successfully, and
        # was supplied the {period} we want to query right now. From that one, we
        # take the data Id of its only Output.
        # This has the following assumptions:
        #   * There will only ever be a single successfully Finished instance for
        #     a given {period}. If a new instance detects that the {period} was
        #     already queried, it fails execution (and has a different Status).
        #   * An instance only has a single output.
        #   * A caller only ever supplies a single argument. The enclave checks
        #     this and fails if there is more than one argument, and that its
        #     name is "period" (which is not tested here, though). Keeps the
        #     query more concise.
        # Not very pythonic to use a big shell oneliner, but declarative
        # querying is so much easier to write than the equivalent of python
        # code. Using perl in between because it is rather concise (yq is not in
        # the apt package repository, so we cannot use it here).
        # Formatting is non-existent here, sorry for that.
        #
        # `check=False`: We treat any errors here as a sign that we need to
        #   do `taskRun` and so forth. If there is a more severe reason, then
        #   that call will also fail, and there we will also print out this
        #   instances stderr string for sufficient context.
        data_id_proc = subprocess.run(
            f'sharemind-hi-client -c {client_conf} -a displayDfc | python3 -c "import sys, yaml, json; print(json.dumps(yaml.safe_load(sys.stdin.read())))" | jq -r \'.Tasks[] | select(.Name == "pseudonymisation_key_enclave") | .Instances[] | select((.Status == "Finished") and (.Arguments[0].Value == {period})) | .Outputs[0].Id\'',
            shell=True, check=False, text=True, capture_output=True)

        if data_id_proc.returncode != 0 or not data_id_proc.stdout.strip().isnumeric():
            # The previous command failed, which could mean that the key does not
            # exist, so we generate the periodic key here. If this fails, too,
            # then maybe there is a more deeply rooted problem. For diagnostics,
            # the original stderr will be printed, too.
            proc = subprocess.run(
                f"""sharemind-hi-client -c {client_conf} -a taskRun -- \
                --task pseudonymisation_key_enclave --wait -- \
                --period {period}""",
                shell=True, check=False, text=True, capture_output=True)
            if proc.returncode != 0:
                # One of the stderrs should contain sufficient information.
                raise Exception('\n'.join([
                    'stdout1:',
                    data_id_proc.stdout,
                    'stderr1:',
                    data_id_proc.stderr,
                    'stdout2:',
                    proc.stdout,
                    'stderr2:',
                    proc.stderr,
                ]))
            # Not using the `jq` shell stuff here, because it is rather trivial
            # to parse with a regex.
            pattern = r'Topic: periodic_pseudonymisation_key\n *Id: ([0-9]+)'
            match = re.search(pattern, proc.stdout)
            if match is None:
                raise Exception(
                    'Could not parse periodic_pseudonymisation_key topic data ID')
            data_id = int(match[1])
        else:
            data_id = int(data_id_proc.stdout.strip())

        # Here we know from what data-id we want to download.
        # Download key and parse it from the binary result file
        proc = subprocess.run(
            f"""umask 177 &&
            sharemind-hi-client -c {client_conf} -a dataDownload -- \
            --topic periodic_pseudonymisation_key --dataid {data_id} \
            --datafile {periodic_key_path}""",
            shell=True, check=False, capture_output=True)
        if proc.returncode != 0:
            # One of the stderrs should contain sufficient information.
            raise Exception('\n'.join([
                'stdout1:',
                data_id_proc.stdout,
                'stderr1:',
                data_id_proc.stderr,
                'stdout2:',
                proc.stdout,
                'stderr2:',
                proc.stderr,
            ]))

        with open(periodic_key_path, 'rb') as f:
            period_bytes = f.read(4)
            key_file_period = int.from_bytes(
                period_bytes, byteorder='little', signed=False)
            assert key_file_period == period
            key = f.read(16)

        # Delete key file
        os.remove(periodic_key_path)

    except subprocess.CalledProcessError as e:
        raise Exception('\n'.join([
            str(e),
            'stdout:', e.stdout,
            'stderr:', e.stderr
        ]))

    finally:
        os.chdir(old_working_dir)

    return key
