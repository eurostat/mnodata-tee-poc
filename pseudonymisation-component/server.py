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

from base64 import b64encode
import datetime as dt
import logging
import os
import re
from typing import Dict, List

from flask import Flask, abort, make_response, request
from pydantic import BaseModel, ValidationError, validator
import werkzeug.routing as routing

import pseudonymisation

def get_env(var):
    val = os.environ.get(var)
    if val is None:
        raise Exception(f'Environment variable {var} is undefined')
    return val

hi_client_conf = get_env('PSEUDONYMISATION_COMPONENT_HI_CLIENT_CONFIGURATION')
hi_client_conf_dir, hi_client_conf = os.path.split(hi_client_conf)
periodic_key_path = get_env('PSEUDONYMISATION_COMPONENT_PERIODIC_KEY_PATH')

app = Flask(__name__)
app.config.from_mapping(
    PERIODIC_KEY_PATH=periodic_key_path,
    HI_CLIENT_CONFIGURATION=hi_client_conf,
    HI_CLIENT_CONFIGURATION_DIR=hi_client_conf_dir)

periodic_keys: Dict[int, bytes] = {}

iso8601_regex = r'\d{4}-\d{2}-\d{2}'

class DateConverter(routing.BaseConverter):
    regex = iso8601_regex

    def to_python(self, value):
        try:
            return dt.date.fromisoformat(value)
        except ValueError:
            raise routing.ValidationError()

    def to_url(self, value):
        return value.isoformat()

app.url_map.converters['date'] = DateConverter

def date_to_period(date: dt.date):
    datetime = dt.datetime(date.year, date.month, date.day)
    timestamp = (datetime - dt.datetime(1970, 1, 1)) / dt.timedelta(seconds=1)
    return int(timestamp / 86400)

@app.route('/v1/key/<date:date>', methods=['POST', 'DELETE'])
def pseudonymisation_key(date: dt.date):
    period = date_to_period(date)

    if request.method == 'POST':
        if period not in periodic_keys:
            periodic_keys[period] = \
                pseudonymisation.get_periodic_key(app.config, period)
            app.logger.info(
                'Retrieved pseudonymisation key for period %d.', period)
        return 'Pseudonymisation key retrieved'
    elif request.method == 'DELETE':
        if period in periodic_keys:
            del periodic_keys[period]
            app.logger.info(
                'Deleted pseudonymisation key of period %d.', period)
            return 'Pseudonymisation key deleted'
        return ''

class PseudonymisationRequest(BaseModel):
    period: str
    identifiers: List[str]

    @validator('period')
    def period_is_iso8601(cls, v):
        if not re.fullmatch(iso8601_regex, v):
            raise ValueError('must be in YYYY-MM-DD format')
        return v

def bad_request(msg):
    app.logger.warning(msg)
    abort(make_response({'message': msg}, 400))

@app.route('/v1/pseudonymise', methods=['POST'])
def pseudonymise():
    json = request.json

    if json is None:
        bad_request('Malformed payload.')

    try:
        req = PseudonymisationRequest(**json)
    except ValidationError as exc:
        bad_request(str(exc))

    period = date_to_period(dt.date.fromisoformat(req.period))

    if period not in periodic_keys:
        bad_request('No key for period {0}.'.format(req.period))

    key = periodic_keys[period]
    pseudonyms = []
    for identifier in req.identifiers:
        if type(identifier) != str:
            bad_request('Malformed payload')
        pseudonym = b64encode(
            pseudonymisation.pseudonymise(bytes(identifier, 'ascii'), key)) \
            .decode('ascii')
        pseudonyms.append(pseudonym)

    app.logger.info('Pseudonymised %d identifiers in period %d.',
                    len(req.identifiers), req.period)

    return {
        'pseudonyms': pseudonyms
    }

if __name__ != '__main__':
    gunicorn_logger = logging.getLogger('gunicorn.error')
    app.logger.handlers = gunicorn_logger.handlers
    app.logger.setLevel(gunicorn_logger.level)
