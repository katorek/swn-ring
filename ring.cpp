#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <mpi.h>
using namespace std;


//define multivariate function F(x1, x2, ...xk)

double f(double x[], int n)
{
    double y;
    int j;
    y = 0.0;

    for (j = 0; j < n-1; j = j+1)
      {
         y = y + exp(-pow((1-x[j]),2)-100*(pow((x[j+1] - pow(x[j],2)),2)));

      }

    y = y;
    return y;
}

//define function for Monte Carlo Multidimensional integration

double int_mcnd(double(*fn)(double[],int),double a[], double b[], int n, int m)

{
    double r, x[n], v;
    int i, j;
    r = 0.0;
    v = 1.0;
    // initial seed value (use system time)
    //srand(time(NULL));


    // step 1: calculate the common factor V
    for (j = 0; j < n; j = j+1)
      {
         v = v*(b[j]-a[j]);
      }

    // step 2: integration
    for (i = 1; i <= m; i=i+1)
    {
        // calculate random x[] points
        for (j = 0; j < n; j = j+1)
        {
            x[j] = a[j] +  (rand()) /( (RAND_MAX/(b[j]-a[j])));
        }
        r = r + fn(x,n);
    }
    r = r*v/m;

    return r;
}




double f(double[], int);
double int_mcnd(double(*)(double[],int), double[], double[], int, int);



int main(int argc, char **argv)
{

    int rank, size;

    MPI_Init (&argc, &argv);      // initializes MPI
    MPI_Comm_rank (MPI_COMM_WORLD, &rank); // get current MPI-process ID. O, 1, ...
    MPI_Comm_size (MPI_COMM_WORLD, &size); // get the total number of processes


    /* define how many integrals */
    const int n = 10;

    double b[n] = {5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0,5.0};
    double a[n] = {-5.0, -5.0, -5.0, -5.0, -5.0, -5.0, -5.0, -5.0, -5.0,-5.0};

    double result, mean;
    int m;

    const unsigned int N = 5;
    double max = -1;


    cout.precision(6);
    cout.setf(ios::fixed | ios::showpoint);


    srand(time(NULL) * rank);  // each MPI process gets a unique seed

    m = 4;                // initial number of intervals

    // convert command-line input to N = number of points
    //N = atoi( argv[1] );


    for (unsigned int  i=0; i <=N; i++)
    {
        result = int_mcnd(f, a, b, n, m);
        mean = result/(pow(10,10));

        if( mean > max)
        {
         max = mean;
        }
        //cout << setw(10)  << m << setw(10) << max << setw(10) << mean << setw(10) << rank << setw(10) << size <<endl;
        m = m*4;
    }

    //cout << setw(30)  << m << setw(30) << result << setw(30) << mean <<endl;
    printf("Process %d of %d mean = %1.5e\n and local max = %1.5e\n", rank, size, mean, max );


    double max_store[4] = {4.43095e-02, 5.76586e-02, 3.15962e-02, 4.23079e-02};

    double send_junk = max_store[0];
    double rec_junk;
    MPI_Status status;


  // This next if-statment implemeents the ring topology
  // the last process ID is size-1, so the ring topology is: 0->1, 1->2, ... size-1->0
  // rank 0 starts the chain of events by passing to rank 1
  if(rank==0) {
    // only the process with rank ID = 0 will be in this block of code.
    MPI_Send(&send_junk, 1, MPI_DOUBLE, 1, 0, MPI_COMM_WORLD); //  send data to process 1
    MPI_Recv(&rec_junk, 1, MPI_DOUBLE, size-1, 0, MPI_COMM_WORLD, &status); // receive data from process size-1
  }
  else if( rank == size-1) {
    MPI_Recv(&rec_junk, 1, MPI_DOUBLE, rank-1, 0, MPI_COMM_WORLD, &status); // recieve data from process rank-1 (it "left" neighbor")
    MPI_Send(&send_junk, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD); // send data to its "right neighbor", rank 0
  }
  else {
    MPI_Recv(&rec_junk, 1, MPI_DOUBLE, rank-1, 0, MPI_COMM_WORLD, &status); // recieve data from process rank-1 (it "left" neighbor")
    MPI_Send(&send_junk, 1, MPI_DOUBLE, rank+1, 0, MPI_COMM_WORLD); // send data to its "right neighbor" (rank+1)
  }
  printf("Process %d send %1.5e\n and recieved %1.5e\n", rank, send_junk, rec_junk );


  MPI_Finalize(); // programs should always perform a "graceful" shutdown
    return 0;
}