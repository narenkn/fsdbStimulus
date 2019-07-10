# FSDB Stimulus
Use FSDB as your stimulus. Only the FSDB and design is needed. A sample test is provided for users to understand the utility.

## Example
```bash
cd tests/xor
make
./build_pp +fsdb2rtl+fsdb=stim.fsdb +fsdb2rtl+test_xor1.rx1=test_xor1.rx1
```

## Todo
1. Is it possible to speedup repeated runs?
