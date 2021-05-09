# FO-Sketch


## Requirements

* Git
* Intel SGX (The SGX simulation mode can be used to run our code, but it does not offer the hardware security feature, and
  the performance gain is not significant because it does not have the paging issue)
* Ubuntu 18.04
* g++-7 (7.5.0 in ubuntu 18.04)
* cmake 3.17
* openssl 1.1.1 (SGXSSL is needed)

## Building

```bash
git clone https://github.com/paper2021anonymous/fosketch.git
cd fosketch/FOSketch
make
```
Note that *_HW requires SGX-enabled hardware to execute.

## Usage
`./app -h` for help

`./app` to run the prototype service

## Test
If you want to test the performanc of our FO-Sketch, you should uncomment some test codes in `FOS_test_ecall()` from `FOSketch/App.cpp`, `ecall_fos_test()` from `FOSketch/Enclave/Enclave.cpp`, some test macros in `Common/CommonUtil.h` and macro `COUNT_ACCESS` in `FOSketch/FOSketch.h`.

Enjoy!

