# Introduction

This project consists of the two task enclaves of the Solution: the
pseudonymisation key enclave and the analytics enclave. The source code of the
enclaves is in their respective folders under `./src`. Unit tests are in the
`unit-test/` directory. Larger integration tests are in the `test/` directory.

The pseudonymisation key enclave generates periodic pseudonymisation keys. The
analytics enclave implements the data analysis process of the Solution. More
information about the enclaves can be found in the architecture document.

# Building and Testing

Building this project requires an installation of HI which comes in two flavors
depending on the use case:

* If you want to rebuild production enclaves to verify that the code within this
  project is the source code for the enclave binaries which we distribute
  through a separate channel, you need the auditor bundle. This bundle contains
  a Dockerfile which reproduces the build environment required to reproduce the
  same enclave binaries.
* If you want to build development enclaves to test out modifications and run
  tests, you need the development bundle. This bundle contains a Dockerfile
  which describes the environment you need to setup, how to build the project
  and how to run the tests. Please note that the tests require the other
  components which are part of this source bundle.  
  The process of creating new production enclaves with your modifications is also
  mentioned within this bundle.
