#!/bin/bash

make -C ../ rebuild
../aad -e sin300Hz.wav sin300Hz.aad
../aad -e sin300Hz_mono.wav sin300Hz_mono.aad
../aad -d sin300Hz.aad sin300Hz_decoded.wav
../aad -d sin300Hz_mono.aad sin300Hz_mono_decoded.wav
