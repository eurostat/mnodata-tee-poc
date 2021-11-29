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

import hashlib
import hmac
import secrets

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

HASH_BYTES = 12
HMAC_BYTES = 4


def generate_periodic_key():
    return secrets.token_bytes(16)

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
    cipher = Cipher(algorithms.AES(periodic_key), modes.CTR(nonce))
    encryptor = cipher.encryptor()

    return encryptor.update(plaintext) + encryptor.finalize()
