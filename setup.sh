#!/bin/bash

echo setup for openSUSE

sudo zypper addrepo https://download.opensuse.org/repositories/openSUSE:Leap:15.1/standard/openSUSE:Leap:15.1.repo
sudo zypper refresh
 
sudo zypper in libtool lam lam-devel automake autoconf

export $PATH:/usr/lib64/mpi/gcc/openmpi/bin

