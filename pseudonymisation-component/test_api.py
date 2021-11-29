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

import requests

URL = 'http://127.0.0.1:5000'


class PseudonymisationException(BaseException):
    pass

def pseudonymise(identifiers):
    req = {
        'period': '2021-01-01',
        'identifiers': identifiers
    }
    res = requests.post(f'{URL}/v1/pseudonymise', json=req)

    if res.status_code != 200:
        raise PseudonymisationException()

    return res.json()['pseudonyms']

def main():
    # Get periodic key first
    requests.post(f'{URL}/v1/key/2021-01-01')

    # Pseudonymise
    ids1 = ['310170845466094', '502130123456789']
    ids2 = ['1', '2']
    res1 = pseudonymise(ids1)
    res2 = pseudonymise(ids1)
    res3 = pseudonymise(ids2)

    assert len(res1) == len(res2)

    for i in range(len(res1)):
        assert res1[i] == res2[i]
        assert res1[i] != res3[i]

    # Delete key
    requests.delete(f'{URL}/v1/key/2021-01-01')

    # Try again, should error
    try:
        pseudonymise(ids1)
    except PseudonymisationException:
        pass
    else:
        raise Exception('Pseudonymisation key was not deleted!')

    print('OK')

if __name__ == "__main__":
    main()
