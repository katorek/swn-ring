Commands Centos
```
sudo zypper in lam lam-devel
export PATH=/usr/lib64/mpi/gcc/openmpi/bin:$PATH
ln -s /usr/bin/g++-7 /home/user/bin/g++
mpiCC --std=c++11 ring.cpp -o ring  
```

Commands Ubuntu
```
sudo apt install libopenmpi-dev
mpicc ring.c -o ring.o -Wall
mpirun -np <N> ring.o

mpirun -np <N> --hostfile hosts ring.o
```