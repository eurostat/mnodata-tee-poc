#!/usr/bin/env python3
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

import argparse
from base64 import b64encode
import os

from pseudonymisation import (generate_periodic_key, pseudonymise)


def main():
    parser = argparse.ArgumentParser(
        description='Generate pseudonyms for the data generator')
    parser.add_argument(
        '--users', type=int, default=1000,
        help='Number of users. Note that there will be more users ' +
             'when --duplicates is > 0.')
    parser.add_argument(
        '--days', type=int, default=1,
        help='Length of report period in days')
    parser.add_argument(
        '--duplicates', type=int, default=0,
        help='How many times should each user be duplicated')
    args = parser.parse_args()

    id = 1
    for day in range(args.days):
        periodic_key = generate_periodic_key()
        with open(os.path.join('output', f'periodic-key-{day}'), 'wb') as f:
            f.write(periodic_key)

        for user in range(args.users):
            for i in range(args.duplicates + 1):
                id_bytes = id.to_bytes(8, 'little')
                pseudonym = pseudonymise(id_bytes, periodic_key)
                pseudonym = b64encode(pseudonym).decode('ascii')
                print(pseudonym)
                id += 1

if __name__ == "__main__":
    main()
