# Introduction

This is the source code of the project conducted by Eurostat and Cybernetica
 in 2020-2021 (project reference number ESTAT 2019.0232). 

More information can be found in the project documentation, see
https://ec.europa.eu/eurostat/cros/content/eurostat-cybernetica-project_en

Project source code contains the following components. 

* `data-generator`: The data generator generates artificial H files which serve
  as input for the task enclave tests.
* `nsi-wrapper`: A bash script which provides an easier CLI over the standard
  `sharemind-hi-client` application, suitable for the tasks which the NSI
  department will perform: Uploading report requests and downloading report
  results.
* `prototype`: The prototype implementation of the analytics code. It is
  written in Python and focuses on readability. Its output is compared against
  the output of the task enclaves which are more focused on performance.
* `pseudonymisation-component`: An HTTP server which provides an easier API
  over the standard `sharemind-hi-client` application, suitable for the tasks
  which the MNO-ND department will perform: download periodic pseudonymisation
  keys and pseudonymise identifiers. It is implemented as an HTTP server to make
  it easily consumable by any application, as compared to a C API which might
  be more difficult to integrate.
* `task-enclaves`: The pseudonymisation key enclave and the analytics enclave
  which are run in the production Solution (inside of the Sharemind HI server),
  based on Intel(R) SGX, written in C++. The analysis code is based on the
  prototype implementation, but deviates when beneficial for performance.
  This directory contains integration tests which use all of the other
  components of this list.
* `vad-wrapper`: A bash script which provides an easier CLI over the standard
  `sharemind-hi-client` application, suitable for the tasks which the MNO-VAD
  department will perform: schedule H files for processing and downloading
  report results.
