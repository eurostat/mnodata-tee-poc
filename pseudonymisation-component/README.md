
# Installing

Create virtualenv:

`$ python3 -m venv venv`

Activate virtualenv:

`$ . venv/bin/activate`

Install requirements:

`$ pip install -r requirements.txt`

If you use Ubuntu and prefer apt you can install the dependencies using:

`$ sudo apt install python3-flask python3-gunicorn python3-dotenv python3-cryptography python3-pydantic python3-yaml`

Copy `example.env` to `.env` and edit it. You can leave `PERIODIC_KEY_PATH` as
is but `HI_CLIENT_CONFIGURATION` should point to the sharemind-hi-client
configuration file.

# Running

Run development server:

`$ ./run-dev.sh`

Run production server:

`$ ./run-prod.sh`
