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
import csv
import fileinput

from parameters import day_count, user_count

def write_csv(filepath, data, header):
    with open(filepath, 'w') as f:
        writer = csv.writer(f)
        writer.writerow(header)
        for row in data:
            writer.writerow(row)

def stdin_id_generator():
    stdin = fileinput.input('-')
    for line in stdin:
        yield line.strip()

def builtin_id_generator():
    for day in range(day_count):
        for user in range(user_count):
            yield user

def get_id_generator(data_generator_description):
    parser = argparse.ArgumentParser(description=data_generator_description)
    parser.add_argument(
        '--builtin-ids', action='store_true',
        help='Use built-in id generator instead of reading them from stdin')
    if parser.parse_args().builtin_ids:
        return builtin_id_generator()
    else:
        return stdin_id_generator()
