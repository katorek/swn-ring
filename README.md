Commands

sudo zypper in lam lam-devel
export PATH=/usr/lib64/mpi/gcc/openmpi/bin:$PATH
ln -s /usr/bin/g++-7 /home/user/bin/g++
mpiCC --std=c++11 ring.cpp -o ring
