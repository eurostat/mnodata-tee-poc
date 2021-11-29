# Introduction

This directory contains the integration tests. The main test files are:

* Business cycle test: `businesscycle/client.sh`
* Performance test: `performance/client.sh`

The rest of the files provide the supporting infrastructure for the tests:

* `client-and-server.sh`: The entrypoint called by `ctest`.
  It starts the server through `server.sh` and the correct `client.sh` script depending on the test type.
* `server.sh`: This script starts the `sharemind-hi-server` application using the `dataflow-configuration-description.yaml` file.
* `dataflow-configuration-description.yaml`: This dataflow configuration description mirrors the one that shall be used in the production environment.
  However, it uses debug client certificates and contains placeholders for the enclave fingerprints which are filled out by the `server.sh` script.
* `data-generator.py`: A unifying wrapper around the existing data generators, such that they can be used more easily from the tests.
