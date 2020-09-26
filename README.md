![C/C++ CI](https://github.com/aikiriao/AAD/workflows/C/C++%20CI/badge.svg?branch=master)

# AAD

Ayashi Adaptive Differential pulse code modulation encoder / decoder

This is a lossy audio codec inspired by IMA-ADPCM and ITU-T G.726.

# Usage

## How to build

```bash
git clone https://github.com/aikiriao/AAD.git
cd AAD
make
```

## Endode/Decode

### Encode

```bash
./aad -e INPUT.wav OUTPUT.aad
```

By default, AAD convert wav to 4-bit/sample ADPCM. Please use `-b` option to change bit/sample.

Example for 3-bit/sample encode:

```bash
./aad -e -b 3 INPUT.wav OUTPUT.aad
```

### Decode

```bash
./aad -d INPUT.aad OUTPUT.wav
```

## More applications

Type `-h` option to display usages for other modes.

```bash
./aad -h
```

# License

Copyright (c) 2020 aikiriao Licensed under the WTFPL license.
